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
        "name;target;kind;includeDirectories;sources;compileDefinitions;compileOptions;linkLibraries;runtimeFiles;libraryPath;upstreamTarget;systemTarget;executablePath;programNames;expectedSha256;expectedVersionRegex;versionArguments"
    )
    _arken_extpkg_json_required_string(_name "${component_json}" "${_subject}" name)
    _arken_extpkg_json_required_string(_target "${component_json}" "${_subject}" target)
    _arken_extpkg_json_optional_string(_kind "${component_json}" "interface" "${_subject}" kind)
    _arken_extpkg_validate_target_name("${_target}" "${recipe_id}")
    set(${out_name} "${_name}" PARENT_SCOPE)
    set(${out_target} "${_target}" PARENT_SCOPE)
    set(${out_kind} "${_kind}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_attach_usage target component_json root subject)
    _arken_extpkg_component_paths(_include_dirs "${component_json}" includeDirectories "${root}" "${subject}")
    if(NOT _include_dirs STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_include_dirs}")
    endif()
    _arken_extpkg_json_array_to_list(_definitions "${component_json}" "${subject}" compileDefinitions)
    if(NOT _definitions STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_DEFINITIONS "${_definitions}")
    endif()
    _arken_extpkg_json_array_to_list(_options "${component_json}" "${subject}" compileOptions)
    if(NOT _options STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_OPTIONS "${_options}")
    endif()
    _arken_extpkg_json_array_to_list(_links "${component_json}" "${subject}" linkLibraries)
    if(NOT _links STREQUAL "")
        set_property(TARGET "${target}" PROPERTY INTERFACE_LINK_LIBRARIES "${_links}")
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
        _arken_extpkg_json_array_to_list(_definitions "${_component}" "${recipe_id}:${_name}" compileDefinitions)
        if(NOT _definitions STREQUAL "")
            target_compile_definitions("${_internal}" PRIVATE ${_definitions})
        endif()
        _arken_extpkg_json_array_to_list(_options "${_component}" "${recipe_id}:${_name}" compileOptions)
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
                    set("${_option}" "${_value}")
                elseif(_value_type STREQUAL "STRING" OR _value_type STREQUAL "NUMBER")
                    set("${_option}" "${_value}")
                else()
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_source.option_invalid"
                        "adapter"
                        "${recipe_id}"
                        "Typed CMake options accept boolean, string, or number values only: ${_option}"
                    )
                endif()
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
    add_subdirectory("${_cmake_root}" "${package_build_root}/cmake-source" EXCLUDE_FROM_ALL)
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
                    "-DOUTPUT_FILES_JOINED=$<TARGET_FILE:${_upstream}>"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
                DEPENDS "${_upstream}"
                VERBATIM
            )
            add_custom_target("${_receipt_target}" DEPENDS "${_receipt}")
            add_dependencies("${_wrapper}" "${_receipt_target}")
        endif()
    endforeach()
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
    if(NOT EXISTS "${_program}" OR IS_DIRECTORY "${_program}")
        _arken_extpkg_error(
            "arken.external_packages.tool.missing"
            "adapter"
            "${recipe_id}"
            "Resolved tool is unavailable: ${_program}"
        )
    endif()
    cmake_path(NORMAL_PATH _program OUTPUT_VARIABLE _program)
    file(SHA256 "${_program}" _observed_sha)
    _arken_extpkg_json_optional_string(_expected_sha "${component}" "" "${recipe_id}:${component_name}" expectedSha256)
    if(NOT "${_expected_sha}" STREQUAL ""
            AND NOT "${_observed_sha}" STREQUAL "${_expected_sha}")
        _arken_extpkg_error(
            "arken.external_packages.tool.hash_mismatch"
            "adapter"
            "${recipe_id}"
            "Tool '${_program}' expected SHA-256 ${_expected_sha}, observed ${_observed_sha}."
        )
    endif()
    _arken_extpkg_json_array_to_list(_version_arguments "${component}" "${recipe_id}:${component_name}" versionArguments)
    _arken_extpkg_json_optional_string(_version_regex "${component}" "" "${recipe_id}:${component_name}" expectedVersionRegex)
    set(_version_output "")
    if(NOT _version_regex STREQUAL "")
        execute_process(
            COMMAND "${_program}" ${_version_arguments}
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
                "Tool '${_program}' did not match expected version expression '${_version_regex}'."
            )
        endif()
    endif()
    string(SHA256 _version_identity "${_version_output}")
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
            continue()
        elseif(_kind STREQUAL "executable")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" executablePath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" executablePath
            )
            set(_location "${root}/${_relative}")
            add_executable("${_target}" IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
        elseif(_kind STREQUAL "static")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" libraryPath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" libraryPath
            )
            set(_location "${root}/${_relative}")
            add_library("${_target}" STATIC IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
            _arken_extpkg_attach_usage("${_target}" "${_component}" "${_usage_root}" "${recipe_id}:${_name}")
        elseif(_kind STREQUAL "shared")
            _arken_extpkg_json_required_string(_relative "${_component}" "${recipe_id}:${_name}" libraryPath)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}" "${recipe_id}:${_name}" libraryPath
            )
            set(_location "${root}/${_relative}")
            add_library("${_target}" SHARED IMPORTED GLOBAL)
            set_property(TARGET "${_target}" PROPERTY IMPORTED_LOCATION "${_location}")
            _arken_extpkg_attach_usage("${_target}" "${_component}" "${_usage_root}" "${recipe_id}:${_name}")
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
        file(MAKE_DIRECTORY "${_materialization_root}/${_include_relative}")
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
            "-DCONFIGURE_ARGUMENTS_JOINED=${_configure_arguments_joined}"
            "-DOUTPUT_RELATIVE_PATHS_JOINED=${_output_relative_joined}"
            "-DINCLUDE_RELATIVE_PATHS_JOINED=${_include_relative_joined}"
            "-DMATERIALIZATION_ROOT=${_materialization_root}"
            "-DBUILD_RECEIPT_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
            "-DVERIFY_RECEIPTS_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageVerifyReceipts.cmake"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageConfigureMakeBuild.cmake"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${_byproducts}
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
    else()
        set(_runtime_root "${source_root}")
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
            PROPERTY ARKEN_EXTERNAL_PACKAGE_ADAPTER_ID "${_adapter_id}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_RECEIPT_ROOT "${receipt_root}"
        )
        set_property(
            TARGET "${_metadata_target}"
            PROPERTY ARKEN_EXTERNAL_RUNTIME_FILES "${_runtime_files}"
        )
    endforeach()
endfunction()
