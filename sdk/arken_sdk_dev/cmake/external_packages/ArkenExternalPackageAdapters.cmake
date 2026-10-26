include_guard(GLOBAL)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

function(_arken_extpkg_component_selected out_var component_name selected_components)
    if(selected_components STREQUAL "")
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()
    list(FIND selected_components "${component_name}" _index)
    if(_index EQUAL -1)
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_arken_extpkg_component_paths out_var component_json field_name base_root subject)
    _arken_extpkg_json_array_to_list(_relative_paths "${component_json}" "${subject}" "${field_name}")
    set(_absolute_paths "")
    foreach(_relative IN LISTS _relative_paths)
        if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "(^|/)\\.\\.(/|$)")
            _arken_extpkg_error(
                "arken.external_packages.component.path_unsafe"
                "adapter"
                "${subject}"
                "Component field '${field_name}' must contain source-root-relative paths: ${_relative}"
            )
        endif()
        cmake_path(ABSOLUTE_PATH _relative BASE_DIRECTORY "${base_root}" NORMALIZE OUTPUT_VARIABLE _absolute)
        list(APPEND _absolute_paths "${_absolute}")
    endforeach()
    set(${out_var} "${_absolute_paths}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_validate_target_name target recipe_id)
    if(NOT target MATCHES "^arken::external_[A-Za-z0-9_]+$")
        _arken_extpkg_error(
            "arken.external_packages.component.target_invalid"
            "adapter"
            "${recipe_id}"
            "Component target '${target}' must use the arken::external_<name> namespace."
        )
    endif()
    if(TARGET "${target}")
        _arken_extpkg_error(
            "arken.external_packages.component.target_conflict"
            "adapter"
            "${recipe_id}"
            "Component target '${target}' already exists."
        )
    endif()
endfunction()

function(_arken_extpkg_validate_relative_component_path value recipe_id field_name)
    if(value STREQUAL ""
            OR IS_ABSOLUTE "${value}"
            OR value MATCHES "(^|/)\\.\\.(/|$)"
            OR value MATCHES "[|;\n\r]")
        _arken_extpkg_error(
            "arken.external_packages.component.path_unsafe"
            "adapter"
            "${recipe_id}"
            "Component field '${field_name}' must be a non-empty package-root-relative path: ${value}"
        )
    endif()
endfunction()

function(_arken_extpkg_component_common out_name out_target out_kind component_json recipe_id component_index)
    set(_subject "${recipe_id}:components[${component_index}]")
    _arken_extpkg_validate_object_keys(
        "${component_json}"
        "${_subject}"
        "name;target;kind;includeDirectories;systemIncludeDirectories;buildIncludeDirectories;buildSystemIncludeDirectories;buildFiles;sources;compileFeatures;compileDefinitions;compileOptions;compileOptionsByCompilerId;linkLibraries;linkOptions;runtimeFiles;libraryPath;upstreamTarget;systemTarget;executablePath;programNames;expectedSha256;expectedVersionRegex;versionArguments"
    )
    _arken_extpkg_json_required_string(_name "${component_json}" "${_subject}" name)
    _arken_extpkg_json_required_string(_target "${component_json}" "${_subject}" target)
    _arken_extpkg_json_optional_string(_kind "${component_json}" "interface" "${_subject}" kind)
    _arken_extpkg_validate_target_name("${_target}" "${recipe_id}")
    set(${out_name} "${_name}" PARENT_SCOPE)
    set(${out_target} "${_target}" PARENT_SCOPE)
    set(${out_kind} "${_kind}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_attach_build_usage target component_json build_root subject)
    _arken_extpkg_component_paths(
        _build_include_dirs
        "${component_json}"
        buildIncludeDirectories
        "${build_root}"
        "${subject}"
    )
    _arken_extpkg_component_paths(
        _build_system_include_dirs
        "${component_json}"
        buildSystemIncludeDirectories
        "${build_root}"
        "${subject}"
    )
    if(NOT _build_include_dirs STREQUAL "")
        set_property(
            TARGET "${target}"
            APPEND
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_build_include_dirs}"
        )
    endif()
    if(NOT _build_system_include_dirs STREQUAL "")
        set_property(
            TARGET "${target}"
            APPEND
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                "${_build_system_include_dirs}"
        )
        set_property(
            TARGET "${target}"
            APPEND
            PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                "${_build_system_include_dirs}"
        )
    endif()
endfunction()

function(_arken_extpkg_component_compiler_options out_var component_json subject)
    _arken_extpkg_json_type(
        _options_by_compiler_type
        "${component_json}"
        compileOptionsByCompilerId
    )
    if(_options_by_compiler_type STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    if(NOT _options_by_compiler_type STREQUAL "OBJECT")
        _arken_extpkg_error(
            "arken.external_packages.component.compiler_options_invalid"
            "schema"
            "${subject}"
            "compileOptionsByCompilerId must be an object."
        )
    endif()
    string(JSON _compiler_count LENGTH "${component_json}" compileOptionsByCompilerId)
    if(_compiler_count GREATER 0)
        math(EXPR _compiler_last "${_compiler_count} - 1")
        foreach(_index RANGE 0 ${_compiler_last})
            string(
                JSON
                _compiler_id
                MEMBER
                "${component_json}"
                compileOptionsByCompilerId
                ${_index}
            )
            if(NOT _compiler_id STREQUAL "AppleClang"
                    AND NOT _compiler_id STREQUAL "Clang"
                    AND NOT _compiler_id STREQUAL "GNU"
                    AND NOT _compiler_id STREQUAL "MSVC")
                _arken_extpkg_error(
                    "arken.external_packages.component.compiler_id_invalid"
                    "schema"
                    "${subject}"
                    "Unsupported compileOptionsByCompilerId key '${_compiler_id}'."
                )
            endif()
            _arken_extpkg_json_array_to_list(
                _validated_options
                "${component_json}"
                "${subject}"
                compileOptionsByCompilerId
                "${_compiler_id}"
            )
            _arken_extpkg_require_unique_list(
                "${_validated_options}"
                "${subject}"
                "compileOptionsByCompilerId.${_compiler_id}"
            )
        endforeach()
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    _arken_extpkg_json_array_to_list(
        _selected_options
        "${component_json}"
        "${subject}"
        compileOptionsByCompilerId
        "${CMAKE_CXX_COMPILER_ID}"
    )
    set(${out_var} "${_selected_options}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_attach_usage target component_json root subject)
    _arken_extpkg_component_paths(_include_dirs "${component_json}" includeDirectories "${root}" "${subject}")
    _arken_extpkg_component_paths(
        _system_include_dirs
        "${component_json}"
        systemIncludeDirectories
        "${root}"
        "${subject}"
    )
    set(_all_include_dirs ${_include_dirs} ${_system_include_dirs})
    if(NOT _all_include_dirs STREQUAL "")
        set_property(
            TARGET "${target}"
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_all_include_dirs}"
        )
    endif()
    if(NOT _system_include_dirs STREQUAL "")
        set_property(
            TARGET "${target}"
            PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_system_include_dirs}"
        )
    endif()
    _arken_extpkg_json_array_to_list(
        _features
        "${component_json}"
        "${subject}"
        compileFeatures
    )
    if(NOT _features STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_FEATURES "${_features}")
    endif()
    _arken_extpkg_json_array_to_list(_definitions "${component_json}" "${subject}" compileDefinitions)
    if(NOT _definitions STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_DEFINITIONS "${_definitions}")
    endif()
    _arken_extpkg_json_array_to_list(_options "${component_json}" "${subject}" compileOptions)
    _arken_extpkg_component_compiler_options(
        _compiler_options
        "${component_json}"
        "${subject}"
    )
    list(APPEND _options ${_compiler_options})
    if(NOT _options STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_OPTIONS "${_options}")
    endif()
    _arken_extpkg_json_array_to_list(_links "${component_json}" "${subject}" linkLibraries)
    if(NOT _links STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_LINK_LIBRARIES "${_links}")
    endif()
    _arken_extpkg_json_array_to_list(
        _link_options
        "${component_json}"
        "${subject}"
        linkOptions
    )
    if(NOT _link_options STREQUAL "")
        set_property(
            TARGET "${target}"
            PROPERTY INTERFACE_LINK_OPTIONS "${_link_options}"
        )
    endif()
endfunction()

function(_arken_extpkg_add_selected_source_receipt
    internal_target
    receipt_path
    recipe_id
    recipe_sha
    instance_id
    adapter_id
    source_identity
)
    add_custom_command(
        TARGET "${internal_target}"
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}"
            "-DRECEIPT_PATH=${receipt_path}"
            "-DRECIPE_ID=${recipe_id}"
            "-DRECIPE_SHA256=${recipe_sha}"
            "-DINSTANCE_ID=${instance_id}"
            "-DADAPTER_ID=${adapter_id}"
            "-DSOURCE_IDENTITY=${source_identity}"
            "-DOUTPUT_FILES_JOINED=$<TARGET_FILE:${internal_target}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
        VERBATIM
    )
endfunction()

function(_arken_extpkg_materialize_interface_components
    recipe_json
    recipe_id
    source_root
    selected_components
)
    string(JSON _component_count LENGTH "${recipe_json}" components)
    if(_component_count EQUAL 0)
        _arken_extpkg_error(
            "arken.external_packages.recipe.components_missing"
            "adapter"
            "${recipe_id}"
            "Recipe declares no components."
        )
    endif()
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        if(NOT _kind STREQUAL "interface")
            _arken_extpkg_error(
                "arken.external_packages.component.kind_invalid"
                "adapter"
                "${recipe_id}"
                "Header-only components must use kind 'interface': ${_name}"
            )
        endif()
        add_library("${_target}" INTERFACE IMPORTED GLOBAL)
        _arken_extpkg_attach_usage("${_target}" "${_component}" "${source_root}" "${recipe_id}:${_name}")
    endforeach()
endfunction()

function(_arken_extpkg_materialize_selected_source_components
    recipe_json
    recipe_id
    recipe_sha
    instance_id
    source_identity
    source_root
    selected_components
    receipt_root
    adapter_id
)
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        if(NOT _kind STREQUAL "static" AND NOT _kind STREQUAL "shared")
            _arken_extpkg_error(
                "arken.external_packages.component.kind_invalid"
                "adapter"
                "${recipe_id}"
                "Selected-source components must use kind 'static' or 'shared': ${_name}"
            )
        endif()
        _arken_extpkg_component_paths(_sources "${_component}" sources "${source_root}" "${recipe_id}:${_name}")
        if(_sources STREQUAL "")
            _arken_extpkg_error(
                "arken.external_packages.component.sources_missing"
                "adapter"
                "${recipe_id}"
                "Selected-source component '${_name}' has no sources."
            )
        endif()
        foreach(_source IN LISTS _sources)
            if(NOT EXISTS "${_source}" OR IS_DIRECTORY "${_source}")
                _arken_extpkg_error(
                    "arken.external_packages.component.source_missing"
                    "adapter"
                    "${recipe_id}"
                    "Selected source is missing: ${_source}"
                )
            endif()
        endforeach()
        _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
        _arken_extpkg_identifier(_component_ident "${_name}")
        set(_internal "arken_external_${_recipe_ident}_${_component_ident}")
        if(_kind STREQUAL "static")
            add_library("${_internal}" STATIC ${_sources})
        else()
            add_library("${_internal}" SHARED ${_sources})
        endif()
        add_library("${_target}" ALIAS "${_internal}")
        _arken_extpkg_component_paths(_include_dirs "${_component}" includeDirectories "${source_root}" "${recipe_id}:${_name}")
        if(NOT _include_dirs STREQUAL "")
            target_include_directories("${_internal}" PUBLIC ${_include_dirs})
        endif()
        _arken_extpkg_component_paths(
            _system_include_dirs
            "${_component}"
            systemIncludeDirectories
            "${source_root}"
            "${recipe_id}:${_name}"
        )
        if(NOT _system_include_dirs STREQUAL "")
            target_include_directories("${_internal}" SYSTEM PUBLIC ${_system_include_dirs})
        endif()
        _arken_extpkg_json_array_to_list(
            _features
            "${_component}"
            "${recipe_id}:${_name}"
            compileFeatures
        )
        if(NOT _features STREQUAL "")
            target_compile_features("${_internal}" PUBLIC ${_features})
        endif()
        _arken_extpkg_json_array_to_list(_definitions "${_component}" "${recipe_id}:${_name}" compileDefinitions)
        if(NOT _definitions STREQUAL "")
            target_compile_definitions("${_internal}" PRIVATE ${_definitions})
        endif()
        _arken_extpkg_json_array_to_list(_options "${_component}" "${recipe_id}:${_name}" compileOptions)
        _arken_extpkg_component_compiler_options(
            _compiler_options
            "${_component}"
            "${recipe_id}:${_name}"
        )
        list(APPEND _options ${_compiler_options})
        if(NOT _options STREQUAL "")
            target_compile_options("${_internal}" PRIVATE ${_options})
        endif()
        _arken_extpkg_json_array_to_list(_links "${_component}" "${recipe_id}:${_name}" linkLibraries)
        if(NOT _links STREQUAL "")
            target_link_libraries("${_internal}" PUBLIC ${_links})
        endif()
        set(_receipt "${receipt_root}/${_name}.build.receipt.json")
        _arken_extpkg_add_selected_source_receipt(
            "${_internal}"
            "${_receipt}"
            "${recipe_id}"
            "${recipe_sha}"
            "${instance_id}"
            "${adapter_id}"
            "${source_identity}"
        )
    endforeach()
endfunction()

function(_arken_extpkg_materialize_cmake_source
    recipe_json
    recipe_id
    recipe_sha
    instance_id
    source_identity
    source_root
    selected_components
    package_build_root
    receipt_root
)
    _arken_extpkg_json_optional_string(_subdirectory "${recipe_json}" "" "${recipe_id}" adapter cmakeSubdirectory)
    set(_cmake_root "${source_root}")
    if(NOT _subdirectory STREQUAL "")
        set(_cmake_root "${_cmake_root}/${_subdirectory}")
    endif()
    if(NOT EXISTS "${_cmake_root}/CMakeLists.txt")
        _arken_extpkg_error(
            "arken.external_packages.cmake_source.missing"
            "adapter"
            "${recipe_id}"
            "CMake source root does not contain CMakeLists.txt: ${_cmake_root}"
        )
    endif()
    _arken_extpkg_json_type(_options_type "${recipe_json}" adapter options)
    set(_configured_options "")
    set(_configuration_material
        "adapter=cmake-source-v1"
        "cmake=${CMAKE_VERSION}"
        "generator=${CMAKE_GENERATOR}"
        "system=${CMAKE_SYSTEM_NAME}"
        "processor=${CMAKE_SYSTEM_PROCESSOR}"
        "cCompiler=${CMAKE_C_COMPILER}"
        "cCompilerId=${CMAKE_C_COMPILER_ID}"
        "cCompilerVersion=${CMAKE_C_COMPILER_VERSION}"
        "cxxCompiler=${CMAKE_CXX_COMPILER}"
        "cxxCompilerId=${CMAKE_CXX_COMPILER_ID}"
        "cxxCompilerVersion=${CMAKE_CXX_COMPILER_VERSION}"
        "sysroot=${CMAKE_OSX_SYSROOT}"
        "deploymentTarget=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "architectures=${CMAKE_OSX_ARCHITECTURES}"
        "vcsParentDiscovery=disabled"
    )
    if(_options_type STREQUAL "OBJECT")
        string(JSON _option_count LENGTH "${recipe_json}" adapter options)
        if(_option_count GREATER 0)
            math(EXPR _option_last "${_option_count} - 1")
            foreach(_index RANGE 0 ${_option_last})
                string(JSON _option MEMBER "${recipe_json}" adapter options ${_index})
                string(JSON _value GET "${recipe_json}" adapter options "${_option}")
                string(JSON _value_type TYPE "${recipe_json}" adapter options "${_option}")
                if(NOT _option MATCHES "^[A-Za-z_][A-Za-z0-9_]*$"
                        OR _option MATCHES "^(CMAKE|ARKEN)_")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.option_name_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source option '${_option}' is not an upstream-scoped typed option."
                    )
                endif()
                if(_value_type STREQUAL "BOOLEAN")
                    set(_cache_type BOOL)
                elseif(_value_type STREQUAL "STRING" OR _value_type STREQUAL "NUMBER")
                    set(_cache_type STRING)
                else()
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.option_invalid"
                        "adapter"
                        "${recipe_id}"
                        "Typed CMake options accept boolean, string, or number values only: ${_option}"
                    )
                endif()
                get_property(
                    _cache_was_set
                    CACHE "${_option}"
                    PROPERTY TYPE
                    SET
                )
                set(
                    "_arken_extpkg_cache_was_set_${_option}"
                    "${_cache_was_set}"
                )
                if(_cache_was_set)
                    foreach(_property IN ITEMS TYPE VALUE HELPSTRING ADVANCED)
                        get_property(
                            _prior
                            CACHE "${_option}"
                            PROPERTY "${_property}"
                        )
                        set(
                            "_arken_extpkg_cache_${_property}_${_option}"
                            "${_prior}"
                        )
                    endforeach()
                endif()
                set(
                    "${_option}"
                    "${_value}"
                    CACHE "${_cache_type}"
                    "Arken external package ${recipe_id} reviewed option"
                    FORCE
                )
                list(APPEND _configured_options "${_option}")
                list(APPEND
                    _configuration_material
                    "option.${_option}=${_value}"
                )
            endforeach()
        endif()
    elseif(NOT _options_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.cmake_source.options_invalid"
            "adapter"
            "${recipe_id}"
            "adapter.options must be an object."
        )
    endif()
    _arken_extpkg_json_type(_tool_options_type "${recipe_json}" adapter toolOptions)
    if(_tool_options_type STREQUAL "OBJECT")
        string(JSON _tool_option_count LENGTH "${recipe_json}" adapter toolOptions)
        if(_tool_option_count GREATER 0)
            math(EXPR _tool_option_last "${_tool_option_count} - 1")
            foreach(_index RANGE 0 ${_tool_option_last})
                string(JSON _option MEMBER "${recipe_json}" adapter toolOptions ${_index})
                string(JSON _tool_target GET "${recipe_json}" adapter toolOptions "${_option}")
                if(NOT _option MATCHES "^[A-Za-z_][A-Za-z0-9_]*$"
                        OR _option MATCHES "^(CMAKE|ARKEN)_")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.tool_option_name_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source tool option '${_option}' is not an upstream-scoped cache option."
                    )
                endif()
                list(FIND _configured_options "${_option}" _duplicate_option_index)
                if(NOT _duplicate_option_index EQUAL -1)
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.tool_option_duplicate"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source tool option '${_option}' is also declared in adapter.options."
                    )
                endif()
                if(NOT _tool_target MATCHES "^arken::external_[A-Za-z0-9_]+$"
                        OR NOT TARGET "${_tool_target}")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.tool_target_missing"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source tool option '${_option}' requires a materialized Arken external tool target: ${_tool_target}"
                    )
                endif()
                get_target_property(
                    _tool_location
                    "${_tool_target}"
                    IMPORTED_LOCATION
                )
                get_target_property(
                    _tool_sha256
                    "${_tool_target}"
                    ARKEN_EXTERNAL_TOOL_SHA256
                )
                get_target_property(
                    _tool_version_identity
                    "${_tool_target}"
                    ARKEN_EXTERNAL_TOOL_VERSION_IDENTITY
                )
                string(LENGTH "${_tool_sha256}" _tool_sha_length)
                string(LENGTH "${_tool_version_identity}" _tool_version_length)
                if(NOT IS_ABSOLUTE "${_tool_location}"
                        OR NOT EXISTS "${_tool_location}"
                        OR IS_DIRECTORY "${_tool_location}"
                        OR NOT _tool_sha256 MATCHES "^[0-9a-f]+$"
                        OR NOT _tool_sha_length EQUAL 64
                        OR NOT _tool_version_identity MATCHES "^[0-9a-f]+$"
                        OR NOT _tool_version_length EQUAL 64)
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.tool_target_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source tool option '${_option}' requires an identity-recorded imported executable."
                    )
                endif()
                file(SHA256 "${_tool_location}" _observed_tool_sha256)
                if(NOT "${_observed_tool_sha256}" STREQUAL "${_tool_sha256}")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.tool_identity_changed"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source tool '${_tool_location}' changed after package resolution: expected ${_tool_sha256}, observed ${_observed_tool_sha256}."
                    )
                endif()
                get_property(
                    _cache_was_set
                    CACHE "${_option}"
                    PROPERTY TYPE
                    SET
                )
                set(
                    "_arken_extpkg_cache_was_set_${_option}"
                    "${_cache_was_set}"
                )
                if(_cache_was_set)
                    foreach(_property IN ITEMS TYPE VALUE HELPSTRING ADVANCED)
                        get_property(
                            _prior
                            CACHE "${_option}"
                            PROPERTY "${_property}"
                        )
                        set(
                            "_arken_extpkg_cache_${_property}_${_option}"
                            "${_prior}"
                        )
                    endforeach()
                endif()
                set(
                    "${_option}"
                    "${_tool_location}"
                    CACHE FILEPATH
                    "Arken external package ${recipe_id} identity-recorded build tool"
                    FORCE
                )
                list(APPEND _configured_options "${_option}")
                list(APPEND
                    _configuration_material
                    "toolOption.${_option}.target=${_tool_target}"
                    "toolOption.${_option}.path=${_tool_location}"
                    "toolOption.${_option}.sha256=${_tool_sha256}"
                    "toolOption.${_option}.versionIdentity=${_tool_version_identity}"
                )
            endforeach()
        endif()
    elseif(NOT _tool_options_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.cmake_source.tool_options_invalid"
            "adapter"
            "${recipe_id}"
            "adapter.toolOptions must be an object."
        )
    endif()
    _arken_extpkg_json_type(_path_options_type "${recipe_json}" adapter pathOptions)
    if(_path_options_type STREQUAL "OBJECT")
        string(JSON _path_option_count LENGTH "${recipe_json}" adapter pathOptions)
        if(_path_option_count GREATER 0)
            math(EXPR _path_option_last "${_path_option_count} - 1")
            foreach(_index RANGE 0 ${_path_option_last})
                string(JSON _option MEMBER "${recipe_json}" adapter pathOptions ${_index})
                string(JSON _path_target GET "${recipe_json}" adapter pathOptions "${_option}")
                if(NOT _option MATCHES "^[A-Za-z_][A-Za-z0-9_]*$"
                        OR _option MATCHES "^(CMAKE|ARKEN)_")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.path_option_name_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source path option '${_option}' is not an upstream-scoped cache option."
                    )
                endif()
                list(FIND _configured_options "${_option}" _duplicate_option_index)
                if(NOT _duplicate_option_index EQUAL -1)
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.path_option_duplicate"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source path option '${_option}' is already declared by another adapter option."
                    )
                endif()
                if(NOT _path_target MATCHES "^arken::external_[A-Za-z0-9_]+$"
                        OR NOT TARGET "${_path_target}")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.path_target_missing"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source path option '${_option}' requires a materialized Arken external path target: ${_path_target}"
                    )
                endif()
                get_target_property(
                    _path_location
                    "${_path_target}"
                    ARKEN_EXTERNAL_PACKAGE_DIRECTORY
                )
                get_target_property(
                    _path_sha256
                    "${_path_target}"
                    ARKEN_EXTERNAL_DIRECTORY_SHA256
                )
                string(LENGTH "${_path_sha256}" _path_sha_length)
                if(NOT IS_ABSOLUTE "${_path_location}"
                        OR NOT IS_DIRECTORY "${_path_location}"
                        OR IS_SYMLINK "${_path_location}"
                        OR NOT _path_sha256 MATCHES "^[0-9a-f]+$"
                        OR NOT _path_sha_length EQUAL 64)
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.path_target_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source path option '${_option}' requires an identity-recorded package directory."
                    )
                endif()
                _arken_extpkg_tree_identity(
                    _observed_path_sha256
                    "${_path_location}"
                    "${recipe_id}:${_option}"
                )
                if(NOT "${_observed_path_sha256}" STREQUAL "${_path_sha256}")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.path_identity_changed"
                        "adapter"
                        "${recipe_id}"
                        "CMake-source package directory '${_path_location}' changed after resolution: expected ${_path_sha256}, observed ${_observed_path_sha256}."
                    )
                endif()
                get_property(
                    _cache_was_set
                    CACHE "${_option}"
                    PROPERTY TYPE
                    SET
                )
                set(
                    "_arken_extpkg_cache_was_set_${_option}"
                    "${_cache_was_set}"
                )
                if(_cache_was_set)
                    foreach(_property IN ITEMS TYPE VALUE HELPSTRING ADVANCED)
                        get_property(
                            _prior
                            CACHE "${_option}"
                            PROPERTY "${_property}"
                        )
                        set(
                            "_arken_extpkg_cache_${_property}_${_option}"
                            "${_prior}"
                        )
                    endforeach()
                endif()
                set(
                    "${_option}"
                    "${_path_location}"
                    CACHE PATH
                    "Arken external package ${recipe_id} identity-recorded package directory"
                    FORCE
                )
                list(APPEND _configured_options "${_option}")
                list(APPEND
                    _configuration_material
                    "pathOption.${_option}.target=${_path_target}"
                    "pathOption.${_option}.path=${_path_location}"
                    "pathOption.${_option}.sha256=${_path_sha256}"
                )
            endforeach()
        endif()
    elseif(NOT _path_options_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.cmake_source.path_options_invalid"
            "adapter"
            "${recipe_id}"
            "adapter.pathOptions must be an object."
        )
    endif()
    list(SORT _configuration_material)
    string(JOIN "|" _configuration_text ${_configuration_material})
    string(SHA256 _configuration_sha256 "${_configuration_text}")
    set(
        _configuration_identity
        "cmake-source-v1:${_configuration_sha256}"
    )
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_required_string(_upstream "${_component}" "${recipe_id}:${_name}" upstreamTarget)
        if(TARGET "${_upstream}")
            _arken_extpkg_error(
                "arken.external_packages.cmake_source.ambient_target_conflict"
                "adapter"
                "${recipe_id}"
                "Upstream target '${_upstream}' existed before the reviewed source package was added."
            )
        endif()
    endforeach()
    set(_prior_git_ceiling "$ENV{GIT_CEILING_DIRECTORIES}")
    set(_git_ceiling_was_defined FALSE)
    if(DEFINED ENV{GIT_CEILING_DIRECTORIES})
        set(_git_ceiling_was_defined TRUE)
    endif()
    set(ENV{GIT_CEILING_DIRECTORIES} "${source_root}")
    add_subdirectory(
        "${_cmake_root}"
        "${package_build_root}/cmake-source"
        EXCLUDE_FROM_ALL
    )
    if(_git_ceiling_was_defined)
        set(ENV{GIT_CEILING_DIRECTORIES} "${_prior_git_ceiling}")
    else()
        unset(ENV{GIT_CEILING_DIRECTORIES})
    endif()
    foreach(_option IN LISTS _configured_options)
        if(_arken_extpkg_cache_was_set_${_option})
            set(_prior_type
                "${_arken_extpkg_cache_TYPE_${_option}}")
            if(_prior_type STREQUAL "BOOL"
                    OR _prior_type STREQUAL "FILEPATH"
                    OR _prior_type STREQUAL "PATH"
                    OR _prior_type STREQUAL "STRING"
                    OR _prior_type STREQUAL "INTERNAL")
                set(_restore_type "${_prior_type}")
            else()
                set(_restore_type STRING)
            endif()
            set(
                "${_option}"
                "${_arken_extpkg_cache_VALUE_${_option}}"
                CACHE "${_restore_type}"
                "${_arken_extpkg_cache_HELPSTRING_${_option}}"
                FORCE
            )
            if(NOT _prior_type STREQUAL _restore_type)
                set_property(
                    CACHE "${_option}"
                    PROPERTY TYPE "${_prior_type}"
                )
            endif()
            if(_arken_extpkg_cache_ADVANCED_${_option})
                mark_as_advanced(FORCE "${_option}")
            else()
                mark_as_advanced(CLEAR "${_option}")
            endif()
        else()
            unset("${_option}" CACHE)
        endif()
    endforeach()
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_required_string(_upstream "${_component}" "${recipe_id}:${_name}" upstreamTarget)
        if(NOT TARGET "${_upstream}")
            _arken_extpkg_error(
                "arken.external_packages.cmake_source.target_missing"
                "adapter"
                "${recipe_id}"
                "Upstream CMake target '${_upstream}' is missing."
            )
        endif()
        _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
        _arken_extpkg_identifier(_component_ident "${_name}")
        set(_wrapper "arken_external_${_recipe_ident}_${_component_ident}")
        add_library("${_wrapper}" INTERFACE)
        target_link_libraries("${_wrapper}" INTERFACE "${_upstream}")
        add_library("${_target}" ALIAS "${_wrapper}")
        get_target_property(_upstream_type "${_upstream}" TYPE)
        # These are declared package build/usage requirements. Applying them
        # only to consumers leaves the upstream archive compiled incorrectly
        # (notably Luau's protected-call catches in WebAssembly).
        _arken_extpkg_json_array_to_list(_component_options "${_component}"
            "${recipe_id}:${_name}" compileOptions)
        _arken_extpkg_component_compiler_options(_compiler_options
            "${_component}" "${recipe_id}:${_name}")
        list(APPEND _component_options ${_compiler_options})
        if(NOT _component_options STREQUAL "")
            target_compile_options("${_wrapper}" INTERFACE ${_component_options})
            if(_upstream_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options("${_upstream}" INTERFACE ${_component_options})
            elseif(NOT _upstream_type STREQUAL "UTILITY")
                target_compile_options("${_upstream}" PRIVATE ${_component_options})
            else()
                _arken_extpkg_error("arken.external_packages.cmake_source.compile_options_invalid"
                    "adapter" "${recipe_id}" "Utility targets cannot consume compileOptions.")
            endif()
        endif()
        if(NOT _upstream_type STREQUAL "INTERFACE_LIBRARY"
                AND NOT _upstream_type STREQUAL "UTILITY")
            set(_receipt "${receipt_root}/${_name}.build.receipt.json")
            set(_receipt_target "${_wrapper}_receipt")
            add_custom_command(
                OUTPUT "${_receipt}"
                COMMAND
                    "${CMAKE_COMMAND}"
                    "-DRECEIPT_PATH=${_receipt}"
                    "-DRECIPE_ID=${recipe_id}"
                    "-DRECIPE_SHA256=${recipe_sha}"
                    "-DINSTANCE_ID=${instance_id}"
                    "-DADAPTER_ID=cmake-source-v1"
                    "-DSOURCE_IDENTITY=${source_identity}"
                    "-DCONFIGURATION_IDENTITY=${_configuration_identity}"
                    "-DOUTPUT_FILES_JOINED=$<TARGET_FILE:${_upstream}>"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
                DEPENDS "${_upstream}"
                VERBATIM
            )
            add_custom_target("${_receipt_target}" DEPENDS "${_receipt}")
            add_dependencies("${_wrapper}" "${_receipt_target}")
            set_property(
                TARGET "${_wrapper}"
                PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_RECEIPT_PATH
                    "${_receipt}"
            )
            set_property(
                TARGET "${_wrapper}"
                PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_OUTPUT_ROOT
                    "${package_build_root}/cmake-source"
            )
            set_property(
                TARGET "${_wrapper}"
                PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_BUILD_TARGET
                    "${_receipt_target}"
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_measure_tool_component
    out_sha256
    out_version_identity
    program
    component
    recipe_id
    component_name
)
    if(NOT IS_ABSOLUTE "${program}"
            OR NOT EXISTS "${program}"
            OR IS_DIRECTORY "${program}")
        _arken_extpkg_error(
            "arken.external_packages.tool.missing"
            "adapter"
            "${recipe_id}"
            "Resolved tool is unavailable: ${program}"
        )
    endif()
    file(SHA256 "${program}" _observed_sha)
    _arken_extpkg_json_type(
        _expected_sha_type
        "${component}"
        expectedSha256
    )
    set(_expected_shas "")
    if(_expected_sha_type STREQUAL "STRING")
        _arken_extpkg_json_optional_string(
            _expected_sha
            "${component}"
            ""
            "${recipe_id}:${component_name}"
            expectedSha256
        )
        if(NOT _expected_sha STREQUAL "")
            list(APPEND _expected_shas "${_expected_sha}")
        endif()
    elseif(_expected_sha_type STREQUAL "ARRAY")
        _arken_extpkg_json_array_to_list(
            _expected_shas
            "${component}"
            "${recipe_id}:${component_name}"
            expectedSha256
        )
        if(_expected_shas STREQUAL "")
            _arken_extpkg_error(
                "arken.external_packages.tool.hash_set_empty"
                "schema"
                "${recipe_id}"
                "Tool component '${component_name}' expectedSha256 array must contain at least one exact digest."
            )
        endif()
    elseif(NOT _expected_sha_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.tool.hash_set_invalid"
            "schema"
            "${recipe_id}"
            "Tool component '${component_name}' expectedSha256 must be one exact digest or an array of exact digests."
        )
    endif()
    set(_unique_expected_shas ${_expected_shas})
    list(REMOVE_DUPLICATES _unique_expected_shas)
    list(LENGTH _expected_shas _expected_sha_count)
    list(LENGTH _unique_expected_shas _unique_expected_sha_count)
    if(NOT _expected_sha_count EQUAL _unique_expected_sha_count)
        _arken_extpkg_error(
            "arken.external_packages.tool.hash_set_duplicate"
            "schema"
            "${recipe_id}"
            "Tool component '${component_name}' expectedSha256 contains a duplicate digest."
        )
    endif()
    foreach(_expected_sha IN LISTS _expected_shas)
        string(LENGTH "${_expected_sha}" _expected_sha_length)
        if(NOT _expected_sha_length EQUAL 64
                OR NOT _expected_sha MATCHES "^[0-9a-f]+$")
            _arken_extpkg_error(
                "arken.external_packages.tool.hash_set_invalid"
                "schema"
                "${recipe_id}"
                "Tool component '${component_name}' expectedSha256 contains a malformed digest '${_expected_sha}'."
            )
        endif()
    endforeach()
    list(FIND _expected_shas "${_observed_sha}" _observed_sha_index)
    if(_expected_sha_count GREATER 0 AND _observed_sha_index EQUAL -1)
        string(JOIN ", " _expected_sha_summary ${_expected_shas})
        _arken_extpkg_error(
            "arken.external_packages.tool.hash_mismatch"
            "adapter"
            "${recipe_id}"
            "Tool '${program}' expected one of SHA-256 [${_expected_sha_summary}], observed ${_observed_sha}."
        )
    endif()
    _arken_extpkg_json_array_to_list(_version_arguments "${component}" "${recipe_id}:${component_name}" versionArguments)
    _arken_extpkg_json_optional_string(_version_regex "${component}" "" "${recipe_id}:${component_name}" expectedVersionRegex)
    set(_version_output "")
    if(NOT _version_regex STREQUAL "")
        execute_process(
            COMMAND "${program}" ${_version_arguments}
            RESULT_VARIABLE _version_result
            OUTPUT_VARIABLE _version_stdout
            ERROR_VARIABLE _version_stderr
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        set(_version_output "${_version_stdout}\n${_version_stderr}")
        if(NOT _version_result EQUAL 0 OR NOT _version_output MATCHES "${_version_regex}")
            _arken_extpkg_error(
                "arken.external_packages.tool.version_mismatch"
                "adapter"
                "${recipe_id}"
                "Tool '${program}' did not match expected version expression '${_version_regex}'."
            )
        endif()
    endif()
    string(SHA256 _version_identity "${_version_output}")
    set(${out_sha256} "${_observed_sha}" PARENT_SCOPE)
    set(${out_version_identity} "${_version_identity}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_resolve_tool_component
    out_program
    out_sha256
    out_version_identity
    component
    recipe_id
    component_name
)
    _arken_extpkg_json_optional_string(_explicit_path "${component}" "" "${recipe_id}:${component_name}" executablePath)
    if(NOT _explicit_path STREQUAL "")
        if(NOT IS_ABSOLUTE "${_explicit_path}")
            _arken_extpkg_error(
                "arken.external_packages.tool.path_invalid"
                "adapter"
                "${recipe_id}"
                "Tool executablePath must be absolute."
            )
        endif()
        set(_program "${_explicit_path}")
    else()
        _arken_extpkg_json_array_to_list(_program_names "${component}" "${recipe_id}:${component_name}" programNames)
        if(_program_names STREQUAL "")
            _arken_extpkg_error(
                "arken.external_packages.tool.names_missing"
                "adapter"
                "${recipe_id}"
                "Tool profile requires executablePath or programNames."
            )
        endif()
        find_program(_program NAMES ${_program_names} NO_CACHE REQUIRED)
    endif()
    cmake_path(NORMAL_PATH _program OUTPUT_VARIABLE _program)
    _arken_extpkg_measure_tool_component(
        _observed_sha
        _version_identity
        "${_program}"
        "${component}"
        "${recipe_id}"
        "${component_name}"
    )
    set(${out_program} "${_program}" PARENT_SCOPE)
    set(${out_sha256} "${_observed_sha}" PARENT_SCOPE)
    set(${out_version_identity} "${_version_identity}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_tool_profile_identity out_var recipe_json recipe_id selected_components)
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    set(_identity_input "")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_json_required_string(_name "${_component}" "${recipe_id}" name)
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_optional_string(_kind "${_component}" "interface" "${recipe_id}:${_name}" kind)
        if(NOT _kind STREQUAL "executable")
            _arken_extpkg_error(
                "arken.external_packages.tool.kind_invalid"
                "adapter"
                "${recipe_id}"
                "Tool-profile components must use kind 'executable'."
            )
        endif()
        _arken_extpkg_resolve_tool_component(
            _program
            _sha256
            _version_identity
            "${_component}"
            "${recipe_id}"
            "${_name}"
        )
        string(APPEND _identity_input "${_name}:${_program}:${_sha256}:${_version_identity}\n")
    endforeach()
    string(SHA256 _identity "${_identity_input}")
    set(${out_var} "tool-profile:${_identity}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_materialize_tool_profile recipe_json recipe_id selected_components)
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        if(NOT _kind STREQUAL "executable")
            _arken_extpkg_error(
                "arken.external_packages.tool.kind_invalid"
                "adapter"
                "${recipe_id}"
                "Tool-profile components must use kind 'executable'."
            )
        endif()
        _arken_extpkg_resolve_tool_component(
            _program
            _observed_sha
            _version_identity
            "${_component}"
            "${recipe_id}"
            "${_name}"
        )
        add_executable("${_target}" IMPORTED GLOBAL)
        set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_program}")
        set_property(TARGET "${_target}" PROPERTY ARKEN_EXTERNAL_TOOL_SHA256 "${_observed_sha}")
        set_property(TARGET "${_target}" PROPERTY ARKEN_EXTERNAL_TOOL_VERSION_IDENTITY "${_version_identity}")
    endforeach()
endfunction()

function(_arken_extpkg_materialize_system_sdk recipe_json recipe_id selected_components)
    _arken_extpkg_json_optional_string(_find_package "${recipe_json}" "" "${recipe_id}" adapter findPackage)
    if(NOT _find_package STREQUAL "")
        find_package("${_find_package}" REQUIRED)
    endif()
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_required_string(_system_target "${_component}" "${recipe_id}:${_name}" systemTarget)
        if(NOT TARGET "${_system_target}")
            _arken_extpkg_error(
                "arken.external_packages.system_sdk.target_missing"
                "adapter"
                "${recipe_id}"
                "System SDK target '${_system_target}' is unavailable."
            )
        endif()
        _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
        _arken_extpkg_identifier(_component_ident "${_name}")
        set(_wrapper "arken_external_${_recipe_ident}_${_component_ident}")
        add_library("${_wrapper}" INTERFACE)
        target_link_libraries("${_wrapper}" INTERFACE "${_system_target}")
        add_library("${_target}" ALIAS "${_wrapper}")
    endforeach()
endfunction()

function(_arken_extpkg_materialize_imported_components
    recipe_json
    recipe_id
    root
    selected_components
    build_dependency
)
    set(_usage_root "${root}")
    if(ARGN)
        list(GET ARGN 0 _usage_root)
    endif()
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(_name _target _kind "${_component}" "${recipe_id}" "${_index}")
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        if(_kind STREQUAL "interface")
            add_library("${_target}" INTERFACE IMPORTED GLOBAL)
            _arken_extpkg_attach_usage("${_target}" "${_component}" "${_usage_root}" "${recipe_id}:${_name}")
            if(NOT root STREQUAL _usage_root)
                _arken_extpkg_attach_build_usage(
                    "${_target}"
                    "${_component}"
                    "${root}"
                    "${recipe_id}:${_name}"
                )
            endif()
            if(NOT build_dependency STREQUAL "")
                add_dependencies("${_target}" "${build_dependency}")
            endif()
            continue()
        elseif(_kind STREQUAL "executable")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" executablePath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" executablePath
            )
            set(_location "${root}/${_relative}")
            cmake_path(NORMAL_PATH _location OUTPUT_VARIABLE _location)
            add_executable("${_target}" IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
            if(build_dependency STREQUAL "")
                _arken_extpkg_measure_tool_component(
                    _tool_sha256
                    _tool_version_identity
                    "${_location}"
                    "${_component}"
                    "${recipe_id}"
                    "${_name}"
                )
                set_property(
                    TARGET "${_target}"
                    PROPERTY ARKEN_EXTERNAL_TOOL_SHA256 "${_tool_sha256}"
                )
                set_property(
                    TARGET "${_target}"
                    PROPERTY ARKEN_EXTERNAL_TOOL_VERSION_IDENTITY
                        "${_tool_version_identity}"
                )
            endif()
        elseif(_kind STREQUAL "static")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" libraryPath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" libraryPath
            )
            set(_location "${root}/${_relative}")
            add_library("${_target}" STATIC IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
            _arken_extpkg_attach_usage("${_target}" "${_component}" "${_usage_root}" "${recipe_id}:${_name}")
            if(NOT root STREQUAL _usage_root)
                _arken_extpkg_attach_build_usage(
                    "${_target}"
                    "${_component}"
                    "${root}"
                    "${recipe_id}:${_name}"
                )
            endif()
        elseif(_kind STREQUAL "shared")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" libraryPath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" libraryPath
            )
            set(_location "${root}/${_relative}")
            add_library("${_target}" SHARED IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
            _arken_extpkg_attach_usage("${_target}" "${_component}" "${_usage_root}" "${recipe_id}:${_name}")
            if(NOT root STREQUAL _usage_root)
                _arken_extpkg_attach_build_usage(
                    "${_target}"
                    "${_component}"
                    "${root}"
                    "${recipe_id}:${_name}"
                )
            endif()
        else()
            _arken_extpkg_error(
                "arken.external_packages.component.kind_invalid"
                "adapter"
                "${recipe_id}"
                "Imported component '${_name}' has unsupported kind '${_kind}'."
            )
        endif()
        if(NOT build_dependency STREQUAL "")
            add_dependencies("${_target}" "${build_dependency}")
        elseif(NOT EXISTS "${_location}")
            _arken_extpkg_error(
                "arken.external_packages.prebuilt.output_missing"
                "adapter"
                "${recipe_id}"
                "Verified prebuilt component output is missing: ${_location}"
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_materialize_configure_make
    recipe_json
    recipe_id
    recipe_sha
    instance_id
    source_identity
    source_root
    selected_components
    package_build_root
    cache_root
)
    _arken_extpkg_json_required_string(_make_tool_target "${recipe_json}" "${recipe_id}" adapter makeToolTarget)
    if(NOT TARGET "${_make_tool_target}")
        _arken_extpkg_error(
            "arken.external_packages.configure_make.tool_missing"
            "adapter"
            "${recipe_id}"
            "Configured make tool target is unavailable: ${_make_tool_target}"
        )
    endif()
    get_target_property(_make_program "${_make_tool_target}" IMPORTED_LOCATION)
    if(NOT _make_program OR NOT EXISTS "${_make_program}")
        _arken_extpkg_error(
            "arken.external_packages.configure_make.tool_invalid"
            "adapter"
            "${recipe_id}"
            "Make tool target '${_make_tool_target}' has no valid imported executable."
        )
    endif()
    _arken_extpkg_json_array_to_list(_configure_arguments "${recipe_json}" "${recipe_id}" adapter configureArguments)
    foreach(_argument IN LISTS _configure_arguments)
        if(_argument MATCHES "[|\n\r;]")
            _arken_extpkg_error(
                "arken.external_packages.configure_make.argument_invalid"
                "adapter"
                "${recipe_id}"
                "Configure arguments may not contain command separators or newlines."
            )
        endif()
    endforeach()
    _arken_extpkg_json_optional_string(
        _install_mode
        "${recipe_json}"
        "direct"
        "${recipe_id}:adapter"
        adapter
        installMode
    )
    if(NOT _install_mode STREQUAL "direct"
            AND NOT _install_mode STREQUAL "destdir")
        _arken_extpkg_error(
            "arken.external_packages.configure_make.install_mode_invalid"
            "adapter"
            "${recipe_id}"
            "Configure/Make installMode must be 'direct' or 'destdir'."
        )
    endif()
    if(DEFINED ARKEN_EXTERNAL_PACKAGE_BUILD_JOBS
            AND NOT ARKEN_EXTERNAL_PACKAGE_BUILD_JOBS STREQUAL "")
        set(_build_jobs "${ARKEN_EXTERNAL_PACKAGE_BUILD_JOBS}")
    elseif(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL}
            AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
        set(_build_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
    else()
        cmake_host_system_information(
            RESULT _logical_core_count
            QUERY NUMBER_OF_LOGICAL_CORES
        )
        if(_logical_core_count GREATER 8)
            set(_build_jobs 8)
        else()
            set(_build_jobs "${_logical_core_count}")
        endif()
    endif()
    if(NOT _build_jobs MATCHES "^[1-9][0-9]*$")
        _arken_extpkg_error(
            "arken.external_packages.configure_make.build_jobs_invalid"
            "adapter"
            "${recipe_id}"
            "ARKEN_EXTERNAL_PACKAGE_BUILD_JOBS must be a positive integer."
        )
    endif()
    set(_install_root "${cache_root}/packages/${instance_id}/install")
    set(_materialization_root "${package_build_root}/materialized")
    file(MAKE_DIRECTORY "${cache_root}/locks" "${cache_root}/packages" "${_materialization_root}")
    set(_external_target "arken-external-package-${recipe_id}")
    _arken_extpkg_identifier(_external_target "${_external_target}")
    set(_byproducts "")
    set(_include_relative_paths "")
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _last "${_component_count} - 1")
    set(_output_relative_paths "")
    foreach(_index RANGE 0 ${_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_json_required_string(_name "${_component}" "${recipe_id}" name)
        _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_optional_string(_library_path "${_component}" "" "${recipe_id}:${_name}" libraryPath)
        _arken_extpkg_json_optional_string(_executable_path "${_component}" "" "${recipe_id}:${_name}" executablePath)
        _arken_extpkg_json_array_to_list(
            _component_include_paths
            "${_component}"
            "${recipe_id}:${_name}"
            includeDirectories
        )
        list(APPEND _include_relative_paths ${_component_include_paths})
        if(NOT _library_path STREQUAL "")
            _arken_extpkg_validate_relative_component_path(
                "${_library_path}" "${recipe_id}:${_name}" libraryPath
            )
            list(APPEND _output_relative_paths "${_library_path}")
            list(APPEND _byproducts "${_install_root}/${_library_path}")
        endif()
        if(NOT _executable_path STREQUAL "")
            _arken_extpkg_validate_relative_component_path(
                "${_executable_path}" "${recipe_id}:${_name}" executablePath
            )
            list(APPEND _output_relative_paths "${_executable_path}")
            list(APPEND _byproducts "${_install_root}/${_executable_path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _include_relative_paths)
    foreach(_include_relative IN LISTS _include_relative_paths)
        if(IS_ABSOLUTE "${_include_relative}"
                OR _include_relative MATCHES "(^|/)\\.\\.(/|$)")
            _arken_extpkg_error(
                "arken.external_packages.component.path_unsafe"
                "adapter"
                "${recipe_id}"
                "Configure/Make include path must be install-root relative: ${_include_relative}"
            )
        endif()
        set(
            _materialized_include
            "${_materialization_root}/${_include_relative}"
        )
        if(IS_SYMLINK "${_materialized_include}"
                AND NOT IS_DIRECTORY "${_materialized_include}")
            file(REMOVE "${_materialized_include}")
        endif()
        if(EXISTS "${_materialized_include}"
                AND NOT IS_DIRECTORY "${_materialized_include}")
            _arken_extpkg_error(
                "arken.external_packages.configure_make.materialization_conflict"
                "adapter"
                "${recipe_id}"
                "Configure/Make include materialization is not a directory: "
                "${_materialized_include}"
            )
        elseif(NOT IS_DIRECTORY "${_materialized_include}")
            file(MAKE_DIRECTORY "${_materialized_include}")
        endif()
    endforeach()
    string(REPLACE ";" "|" _configure_arguments_joined "${_configure_arguments}")
    string(REPLACE ";" "|" _output_relative_joined "${_output_relative_paths}")
    string(REPLACE ";" "|" _include_relative_joined "${_include_relative_paths}")
    ExternalProject_Add(
        "${_external_target}"
        SOURCE_DIR "${source_root}"
        BINARY_DIR "${package_build_root}/configure-make-driver"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
            "${CMAKE_COMMAND}"
            "-DPACKAGE_CACHE_ROOT=${cache_root}"
            "-DINSTANCE_ID=${instance_id}"
            "-DRECIPE_ID=${recipe_id}"
            "-DRECIPE_SHA256=${recipe_sha}"
            "-DSOURCE_IDENTITY=${source_identity}"
            "-DSOURCE_ROOT=${source_root}"
            "-DMAKE_PROGRAM=${_make_program}"
            "-DBUILD_JOBS=${_build_jobs}"
            "-DINSTALL_MODE=${_install_mode}"
            "-DCONFIGURE_ARGUMENTS_JOINED=${_configure_arguments_joined}"
            "-DOUTPUT_RELATIVE_PATHS_JOINED=${_output_relative_joined}"
            "-DINCLUDE_RELATIVE_PATHS_JOINED=${_include_relative_joined}"
            "-DMATERIALIZATION_ROOT=${_materialization_root}"
            "-DBUILD_RECEIPT_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
            "-DVERIFY_RECEIPTS_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageVerifyReceipts.cmake"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageConfigureMakeBuild.cmake"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${_byproducts}
        BUILD_ALWAYS ON
        LOG_BUILD ON
        USES_TERMINAL_BUILD TRUE
    )
    _arken_extpkg_materialize_imported_components(
        "${recipe_json}"
        "${recipe_id}"
        "${_install_root}"
        "${selected_components}"
        "${_external_target}"
        "${_materialization_root}"
    )
endfunction()

function(_arken_extpkg_materialize_zig_package_cache
    recipe_json
    recipe_id
    source_root
    selected_components
    cache_root
)
    _arken_extpkg_json_required_string(
        _zig_tool_target
        "${recipe_json}"
        "${recipe_id}"
        adapter
        zigToolTarget
    )
    if(NOT TARGET "${_zig_tool_target}")
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.tool_missing"
            "adapter"
            "${recipe_id}"
            "Zig package cache requires a materialized Zig tool target: ${_zig_tool_target}"
        )
    endif()
    get_target_property(_zig_program "${_zig_tool_target}" IMPORTED_LOCATION)
    get_target_property(
        _zig_sha256
        "${_zig_tool_target}"
        ARKEN_EXTERNAL_TOOL_SHA256
    )
    get_target_property(
        _zig_version_identity
        "${_zig_tool_target}"
        ARKEN_EXTERNAL_TOOL_VERSION_IDENTITY
    )
    string(LENGTH "${_zig_sha256}" _zig_sha_length)
    string(LENGTH "${_zig_version_identity}" _zig_version_length)
    if(NOT IS_ABSOLUTE "${_zig_program}"
            OR NOT EXISTS "${_zig_program}"
            OR IS_DIRECTORY "${_zig_program}"
            OR NOT _zig_sha256 MATCHES "^[0-9a-f]+$"
            OR NOT _zig_sha_length EQUAL 64
            OR NOT _zig_version_identity MATCHES "^[0-9a-f]+$"
            OR NOT _zig_version_length EQUAL 64)
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.tool_invalid"
            "adapter"
            "${recipe_id}"
            "Zig package cache requires an identity-recorded imported Zig executable."
        )
    endif()
    file(SHA256 "${_zig_program}" _observed_zig_sha256)
    if(NOT "${_observed_zig_sha256}" STREQUAL "${_zig_sha256}")
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.tool_identity_changed"
            "adapter"
            "${recipe_id}"
            "Zig executable changed after package resolution: expected ${_zig_sha256}, observed ${_observed_zig_sha256}."
        )
    endif()

    _arken_extpkg_json_required_string(
        _lock_relative
        "${recipe_json}"
        "${recipe_id}"
        adapter
        lockFile
    )
    _arken_extpkg_validate_relative_component_path(
        "${_lock_relative}"
        "${recipe_id}"
        lockFile
    )
    set(_lock_path "${source_root}/${_lock_relative}")
    if(NOT EXISTS "${_lock_path}"
            OR IS_DIRECTORY "${_lock_path}"
            OR IS_SYMLINK "${_lock_path}")
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.lock_missing"
            "adapter"
            "${recipe_id}"
            "Zig package cache lock file is missing: ${_lock_relative}"
        )
    endif()
    _arken_extpkg_add_configure_dependency("${_lock_path}")
    file(READ "${_lock_path}" _lock_json)
    file(SHA256 "${_lock_path}" _lock_sha256)
    _arken_extpkg_validate_object_keys(
        "${_lock_json}"
        "${recipe_id}:zig-package-lock"
        "schemaVersion;packages"
    )
    string(
        JSON
        _lock_schema
        ERROR_VARIABLE
        _lock_schema_error
        GET
        "${_lock_json}"
        schemaVersion
    )
    if(NOT _lock_schema_error STREQUAL "NOTFOUND"
            OR NOT _lock_schema EQUAL 1)
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.lock_schema_invalid"
            "adapter"
            "${recipe_id}"
            "Zig package cache lock schemaVersion must be 1."
        )
    endif()
    string(
        JSON
        _packages_type
        ERROR_VARIABLE
        _packages_type_error
        TYPE
        "${_lock_json}"
        packages
    )
    if(NOT _packages_type_error STREQUAL "NOTFOUND"
            OR NOT _packages_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.packages_invalid"
            "adapter"
            "${recipe_id}"
            "Zig package cache lock packages must be an array."
        )
    endif()
    string(JSON _package_count LENGTH "${_lock_json}" packages)
    if(_package_count LESS 1 OR _package_count GREATER 128)
        _arken_extpkg_error(
            "arken.external_packages.zig_cache.package_count_invalid"
            "adapter"
            "${recipe_id}"
            "Zig package cache lock requires between 1 and 128 packages."
        )
    endif()
    set(_package_names "")
    set(_package_hashes "")
    set(_package_urls "")
    set(_package_records_json "")
    math(EXPR _package_last "${_package_count} - 1")
    foreach(_index RANGE 0 ${_package_last})
        string(JSON _package GET "${_lock_json}" packages ${_index})
        _arken_extpkg_validate_object_keys(
            "${_package}"
            "${recipe_id}:zig-package-lock.packages[${_index}]"
            "name;url;zigHash"
        )
        _arken_extpkg_json_required_string(
            _name
            "${_package}"
            "${recipe_id}:zig-package-lock.packages[${_index}]"
            name
        )
        _arken_extpkg_json_required_string(
            _url
            "${_package}"
            "${recipe_id}:zig-package-lock.packages[${_index}]"
            url
        )
        _arken_extpkg_json_required_string(
            _zig_hash
            "${_package}"
            "${recipe_id}:zig-package-lock.packages[${_index}]"
            zigHash
        )
        string(LENGTH "${_zig_hash}" _zig_hash_length)
        if(NOT _name MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$"
                OR _name MATCHES "[;\n\r|]"
                OR _url MATCHES "[;\n\r|]"
                OR NOT _zig_hash MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]+$"
                OR _zig_hash_length GREATER 192)
            _arken_extpkg_error(
                "arken.external_packages.zig_cache.package_record_invalid"
                "adapter"
                "${recipe_id}"
                "Zig package cache records require safe names, URLs, and Zig content hashes."
            )
        endif()
        if(NOT _url MATCHES "^https://"
                AND NOT _url MATCHES "^git\\+https://")
            if(NOT ARKEN_EXTERNAL_PACKAGES_ALLOW_TEST_SOURCES
                    OR NOT _url MATCHES "^file://")
                _arken_extpkg_error(
                    "arken.external_packages.zig_cache.url_scheme_rejected"
                    "acquisition"
                    "${recipe_id}"
                    "Zig package URLs must use HTTPS or git+HTTPS. Local file URLs are admitted only for controlled tests."
                )
            endif()
        endif()
        list(FIND _package_hashes "${_zig_hash}" _hash_duplicate)
        list(FIND _package_urls "${_url}" _url_duplicate)
        if(NOT _hash_duplicate EQUAL -1 OR NOT _url_duplicate EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.zig_cache.package_duplicate"
                "adapter"
                "${recipe_id}"
                "Zig package cache lock contains a duplicate URL or content hash."
            )
        endif()
        list(APPEND _package_names "${_name}")
        list(APPEND _package_hashes "${_zig_hash}")
        list(APPEND _package_urls "${_url}")
        _arken_extpkg_json_escape(_name_json "${_name}")
        _arken_extpkg_json_escape(_url_json "${_url}")
        _arken_extpkg_json_escape(_zig_hash_json "${_zig_hash}")
        if(_index GREATER 0)
            string(APPEND _package_records_json ",\n")
        endif()
        string(APPEND _package_records_json
            "    {\"name\":\"${_name_json}\",\"url\":\"${_url_json}\",\"zigHash\":\"${_zig_hash_json}\"}"
        )
    endforeach()

    string(
        SHA256
        _cache_key
        "${_lock_sha256}|${_zig_sha256}|${_zig_version_identity}"
    )
    set(_cache_parent "${cache_root}/zig-packages/sha256")
    set(_entry "${_cache_parent}/${_cache_key}")
    set(_packages_root "${_entry}/p")
    set(_receipt "${_entry}/zig-package-cache.receipt.json")
    set(_complete "${_entry}/.complete")
    file(MAKE_DIRECTORY "${cache_root}/locks" "${_cache_parent}")
    file(
        LOCK
        "${cache_root}/locks/zig-packages-${_cache_key}.lock"
        GUARD FUNCTION
        TIMEOUT 600
    )

    set(_entry_valid FALSE)
    if(EXISTS "${_entry}")
        if(EXISTS "${_receipt}"
                AND EXISTS "${_complete}"
                AND IS_DIRECTORY "${_packages_root}"
                AND NOT IS_SYMLINK "${_packages_root}")
            file(READ "${_receipt}" _receipt_json)
            string(JSON _receipt_lock ERROR_VARIABLE _receipt_lock_error
                GET "${_receipt_json}" lockSha256)
            string(JSON _receipt_zig ERROR_VARIABLE _receipt_zig_error
                GET "${_receipt_json}" zigSha256)
            string(JSON _receipt_tree ERROR_VARIABLE _receipt_tree_error
                GET "${_receipt_json}" packagesTreeSha256)
            string(JSON _receipt_count ERROR_VARIABLE _receipt_count_error
                GET "${_receipt_json}" packageCount)
            set(_expected_directories_present TRUE)
            foreach(_zig_hash IN LISTS _package_hashes)
                if(NOT IS_DIRECTORY "${_packages_root}/${_zig_hash}"
                        OR IS_SYMLINK "${_packages_root}/${_zig_hash}")
                    set(_expected_directories_present FALSE)
                endif()
            endforeach()
            if(_receipt_lock_error STREQUAL "NOTFOUND"
                    AND _receipt_zig_error STREQUAL "NOTFOUND"
                    AND _receipt_tree_error STREQUAL "NOTFOUND"
                    AND _receipt_count_error STREQUAL "NOTFOUND"
                    AND _receipt_lock STREQUAL _lock_sha256
                    AND _receipt_zig STREQUAL _zig_sha256
                    AND _receipt_count EQUAL _package_count
                    AND _expected_directories_present)
                _arken_extpkg_tree_identity(
                    _observed_tree
                    "${_packages_root}"
                    "${recipe_id}"
                )
                if(_observed_tree STREQUAL _receipt_tree)
                    set(_entry_valid TRUE)
                endif()
            endif()
        endif()
        if(NOT _entry_valid)
            _arken_extpkg_quarantine_cache_entry(
                "${_entry}"
                "Zig package cache receipt, package set, or tree mismatch"
            )
        endif()
    endif()

    if(NOT _entry_valid)
        if(DEFINED ARKEN_EXTERNAL_PACKAGES_OFFLINE
                AND ARKEN_EXTERNAL_PACKAGES_OFFLINE)
            _arken_extpkg_error(
                "arken.external_packages.offline.zig_cache_missing"
                "acquisition"
                "${recipe_id}"
                "Offline mode is enabled and exact Zig package cache ${_cache_key} is absent. Populate it in an authorized online build, then retry."
            )
        endif()
        string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
        set(_temporary "${_entry}.partial-${_nonce}")
        file(REMOVE_RECURSE "${_temporary}")
        file(MAKE_DIRECTORY "${_temporary}")
        foreach(_url _zig_hash IN ZIP_LISTS _package_urls _package_hashes)
            set(_fetch_succeeded FALSE)
            set(_fetch_diagnostic "")
            foreach(_attempt RANGE 1 2)
                execute_process(
                    COMMAND
                        "${_zig_program}"
                        fetch
                        --global-cache-dir
                        "${_temporary}"
                        "${_url}"
                    RESULT_VARIABLE _fetch_result
                    OUTPUT_VARIABLE _fetch_output
                    ERROR_VARIABLE _fetch_error
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                    TIMEOUT 300
                )
                if(_fetch_result EQUAL 0
                        AND _fetch_output STREQUAL _zig_hash
                        AND IS_DIRECTORY "${_temporary}/p/${_zig_hash}"
                        AND NOT IS_SYMLINK "${_temporary}/p/${_zig_hash}")
                    set(_fetch_succeeded TRUE)
                    break()
                endif()
                set(_fetch_diagnostic
                    "attempt ${_attempt}: result=${_fetch_result}, "
                    "observed='${_fetch_output}', diagnostic='${_fetch_error}'")
                file(REMOVE_RECURSE "${_temporary}/p/${_zig_hash}")
            endforeach()
            if(NOT _fetch_succeeded)
                file(REMOVE_RECURSE "${_temporary}")
                _arken_extpkg_error(
                    "arken.external_packages.zig_cache.fetch_failed"
                    "acquisition"
                    "${recipe_id}"
                    "Zig package fetch failed or returned the wrong content identity for ${_url}. Expected ${_zig_hash}; ${_fetch_diagnostic}"
                )
            endif()
        endforeach()
        _arken_extpkg_tree_identity(
            _packages_tree_sha256
            "${_temporary}/p"
            "${recipe_id}"
        )
        _arken_extpkg_json_escape(_zig_program_json "${_zig_program}")
        set(_receipt_json
            "{\n"
            "  \"schemaVersion\": 1,\n"
            "  \"recipeId\": \"${recipe_id}\",\n"
            "  \"lockSha256\": \"${_lock_sha256}\",\n"
            "  \"zigExecutable\": \"${_zig_program_json}\",\n"
            "  \"zigSha256\": \"${_zig_sha256}\",\n"
            "  \"zigVersionIdentity\": \"${_zig_version_identity}\",\n"
            "  \"packageCount\": ${_package_count},\n"
            "  \"packagesTreeSha256\": \"${_packages_tree_sha256}\",\n"
            "  \"packages\": [\n${_package_records_json}\n  ]\n"
            "}\n"
        )
        string(JOIN "" _receipt_json ${_receipt_json})
        file(WRITE
            "${_temporary}/zig-package-cache.receipt.json"
            "${_receipt_json}"
        )
        file(WRITE "${_temporary}/.complete" "${_cache_key}\n")
        file(RENAME "${_temporary}" "${_entry}" RESULT _rename_result)
        if(NOT _rename_result STREQUAL "0")
            file(REMOVE_RECURSE "${_temporary}")
            if(NOT EXISTS "${_receipt}" OR NOT EXISTS "${_complete}")
                _arken_extpkg_error(
                    "arken.external_packages.zig_cache.publish_failed"
                    "cache"
                    "${recipe_id}"
                    "Atomic Zig package cache publication failed: ${_rename_result}"
                )
            endif()
        endif()
    else()
        file(READ "${_receipt}" _receipt_json)
        string(JSON _packages_tree_sha256
            GET "${_receipt_json}" packagesTreeSha256)
        message(STATUS
            "Arken external package ${recipe_id}: resolved exact Zig packages from verified cache")
    endif()

    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _component_last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_component_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_component_common(
            _name
            _target
            _kind
            "${_component}"
            "${recipe_id}"
            "${_index}"
        )
        _arken_extpkg_component_selected(
            _selected
            "${_name}"
            "${selected_components}"
        )
        if(NOT _selected)
            continue()
        endif()
        if(NOT _kind STREQUAL "interface")
            _arken_extpkg_error(
                "arken.external_packages.zig_cache.component_kind_invalid"
                "adapter"
                "${recipe_id}"
                "Zig package cache components must use kind 'interface'."
            )
        endif()
        add_library("${_target}" INTERFACE IMPORTED GLOBAL)
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_DIRECTORY "${_packages_root}"
        )
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_DIRECTORY_SHA256
                "${_packages_tree_sha256}"
        )
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_RECEIPT_PATH
                "${_receipt}"
        )
    endforeach()
endfunction()

include(
    "${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageCmakeExternal.cmake"
)

function(_arken_extpkg_materialize_recipe
    recipe_json
    recipe_id
    recipe_sha
    instance_id
    source_identity
    source_root
    selected_components
    package_build_root
    receipt_root
    cache_root
)
    _arken_extpkg_json_required_string(_adapter_kind "${recipe_json}" "${recipe_id}" adapter kind)
    _arken_extpkg_json_optional_integer(_adapter_version "${recipe_json}" 1 "${recipe_id}" adapter version)
    set(_adapter_id "${_adapter_kind}-v${_adapter_version}")
    if(_adapter_kind STREQUAL "header-only")
        _arken_extpkg_materialize_interface_components(
            "${recipe_json}" "${recipe_id}" "${source_root}" "${selected_components}"
        )
    elseif(_adapter_kind STREQUAL "selected-sources")
        _arken_extpkg_materialize_selected_source_components(
            "${recipe_json}"
            "${recipe_id}"
            "${recipe_sha}"
            "${instance_id}"
            "${source_identity}"
            "${source_root}"
            "${selected_components}"
            "${receipt_root}"
            "${_adapter_id}"
        )
    elseif(_adapter_kind STREQUAL "tracked-source")
        string(JSON _component_count LENGTH "${recipe_json}" components)
        set(_has_compiled FALSE)
        math(EXPR _last "${_component_count} - 1")
        foreach(_index RANGE 0 ${_last})
            string(JSON _component GET "${recipe_json}" components ${_index})
            _arken_extpkg_json_required_string(_name "${_component}" "${recipe_id}" name)
            _arken_extpkg_component_selected(_selected "${_name}" "${selected_components}")
            if(NOT _selected)
                continue()
            endif()
            _arken_extpkg_json_optional_string(_kind "${_component}" "interface" "${recipe_id}:${_name}" kind)
            if(NOT _kind STREQUAL "interface")
                set(_has_compiled TRUE)
            endif()
        endforeach()
        if(_has_compiled)
            _arken_extpkg_materialize_selected_source_components(
                "${recipe_json}"
                "${recipe_id}"
                "${recipe_sha}"
                "${instance_id}"
                "${source_identity}"
                "${source_root}"
                "${selected_components}"
                "${receipt_root}"
                "${_adapter_id}"
            )
        else()
            _arken_extpkg_materialize_interface_components(
                "${recipe_json}" "${recipe_id}" "${source_root}" "${selected_components}"
            )
        endif()
    elseif(_adapter_kind STREQUAL "cmake-source")
        _arken_extpkg_materialize_cmake_source(
            "${recipe_json}"
            "${recipe_id}"
            "${recipe_sha}"
            "${instance_id}"
            "${source_identity}"
            "${source_root}"
            "${selected_components}"
            "${package_build_root}"
            "${receipt_root}"
        )
    elseif(_adapter_kind STREQUAL "cmake-external")
        _arken_extpkg_materialize_cmake_external(
            "${recipe_json}"
            "${recipe_id}"
            "${recipe_sha}"
            "${instance_id}"
            "${source_identity}"
            "${source_root}"
            "${selected_components}"
            "${package_build_root}"
            "${cache_root}"
        )
    elseif(_adapter_kind STREQUAL "configure-make")
        _arken_extpkg_materialize_configure_make(
            "${recipe_json}"
            "${recipe_id}"
            "${recipe_sha}"
            "${instance_id}"
            "${source_identity}"
            "${source_root}"
            "${selected_components}"
            "${package_build_root}"
            "${cache_root}"
        )
    elseif(_adapter_kind STREQUAL "zig-package-cache")
        _arken_extpkg_materialize_zig_package_cache(
            "${recipe_json}"
            "${recipe_id}"
            "${source_root}"
            "${selected_components}"
            "${cache_root}"
        )
    elseif(_adapter_kind STREQUAL "verified-prebuilt")
        _arken_extpkg_materialize_imported_components(
            "${recipe_json}" "${recipe_id}" "${source_root}" "${selected_components}" ""
        )
    elseif(_adapter_kind STREQUAL "system-sdk")
        _arken_extpkg_materialize_system_sdk("${recipe_json}" "${recipe_id}" "${selected_components}")
    elseif(_adapter_kind STREQUAL "tool-profile")
        _arken_extpkg_materialize_tool_profile("${recipe_json}" "${recipe_id}" "${selected_components}")
    else()
        _arken_extpkg_error(
            "arken.external_packages.adapter.unsupported"
            "adapter"
            "${recipe_id}"
            "Unsupported typed adapter '${_adapter_kind}'."
        )
    endif()

    if(_adapter_kind STREQUAL "configure-make")
        set(_runtime_root "${cache_root}/packages/${instance_id}/install")
        set(_package_build_target "arken-external-package-${recipe_id}")
        _arken_extpkg_identifier(_package_build_target "${_package_build_target}")
    elseif(_adapter_kind STREQUAL "cmake-external")
        set(_runtime_root "${cache_root}/packages/${instance_id}/build")
        set(_package_build_target "arken-external-package-${recipe_id}")
        _arken_extpkg_identifier(_package_build_target "${_package_build_target}")
    else()
        set(_runtime_root "${source_root}")
        set(_package_build_target "")
    endif()
    string(JSON _component_count LENGTH "${recipe_json}" components)
    math(EXPR _component_last "${_component_count} - 1")
    foreach(_component_index RANGE 0 ${_component_last})
        string(JSON _component GET "${recipe_json}" components ${_component_index})
        _arken_extpkg_json_required_string(
            _component_name
            "${_component}"
            "${recipe_id}"
            name
        )
        _arken_extpkg_component_selected(
            _selected
            "${_component_name}"
            "${selected_components}"
        )
        if(NOT _selected)
            continue()
        endif()
        _arken_extpkg_json_required_string(
            _component_target
            "${_component}"
            "${recipe_id}:${_component_name}"
            target
        )
        if(NOT TARGET "${_component_target}")
            _arken_extpkg_error(
                "arken.external_packages.component.target_not_materialized"
                "adapter"
                "${recipe_id}"
                "Adapter did not materialize component target '${_component_target}'."
            )
        endif()
        get_target_property(_aliased_target "${_component_target}" ALIASED_TARGET)
        if(_aliased_target)
            set(_metadata_target "${_aliased_target}")
        else()
            set(_metadata_target "${_component_target}")
        endif()
        _arken_extpkg_json_array_to_list(
            _runtime_relative
            "${_component}"
            "${recipe_id}:${_component_name}"
            runtimeFiles
        )
        set(_runtime_files "")
        foreach(_runtime_file IN LISTS _runtime_relative)
            if(IS_ABSOLUTE "${_runtime_file}"
                    OR _runtime_file MATCHES "(^|/)\\.\\.(/|$)")
                _arken_extpkg_error(
                    "arken.external_packages.component.runtime_path_unsafe"
                    "adapter"
                    "${recipe_id}"
                    "Runtime file must be package-root relative: ${_runtime_file}"
                )
            endif()
            list(APPEND _runtime_files "${_runtime_root}/${_runtime_file}")
        endforeach()
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RECIPE_ID "${recipe_id}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RECIPE_SHA256 "${recipe_sha}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID "${instance_id}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_SOURCE_IDENTITY "${source_identity}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_SOURCE_ROOT "${source_root}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_ADAPTER_ID "${_adapter_id}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RECEIPT_ROOT "${receipt_root}"
        )
        get_target_property(
            _component_receipt_path
            "${_metadata_target}"
            ARKEN_EXTERNAL_PACKAGE_COMPONENT_RECEIPT_PATH
        )
        if(NOT _component_receipt_path)
            set(
                _component_receipt_path
                "${receipt_root}/package.build.receipt.json"
            )
        endif()
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RECEIPT_PATH
                "${_component_receipt_path}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_RUNTIME_FILES "${_runtime_files}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_RUNTIME_RELATIVE_FILES "${_runtime_relative}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RUNTIME_ROOT "${_runtime_root}"
        )
        get_target_property(
            _component_output_root
            "${_metadata_target}"
            ARKEN_EXTERNAL_PACKAGE_COMPONENT_OUTPUT_ROOT
        )
        if(NOT _component_output_root)
            set(_component_output_root "${_runtime_root}")
        endif()
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_OUTPUT_ROOT
                "${_component_output_root}"
        )
        get_target_property(
            _component_build_target
            "${_metadata_target}"
            ARKEN_EXTERNAL_PACKAGE_COMPONENT_BUILD_TARGET
        )
        if(_component_build_target)
            set(_effective_build_target "${_component_build_target}")
        else()
            set(_effective_build_target "${_package_build_target}")
        endif()
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_BUILD_TARGET
                "${_effective_build_target}"
        )
    endforeach()
endfunction()
