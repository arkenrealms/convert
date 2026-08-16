include_guard(GLOBAL)

function(_arken_extpkg_cmake_external_json_string out_var value)
    _arken_extpkg_json_escape(_escaped "${value}")
    set(${out_var} "\"${_escaped}\"" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_materialize_cmake_external
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
    if(CMAKE_CONFIGURATION_TYPES)
        _arken_extpkg_error(
            "arken.external_packages.cmake_external.multiconfig_unsupported"
            "adapter"
            "${recipe_id}"
            "cmake-external-v1 requires a single-configuration parent generator."
        )
    endif()

    _arken_extpkg_json_optional_string(
        _subdirectory
        "${recipe_json}"
        ""
        "${recipe_id}"
        adapter
        cmakeSubdirectory
    )
    set(_cmake_root "${source_root}")
    if(NOT _subdirectory STREQUAL "")
        _arken_extpkg_validate_relative_component_path(
            "${_subdirectory}"
            "${recipe_id}"
            cmakeSubdirectory
        )
        set(_cmake_root "${source_root}/${_subdirectory}")
    endif()
    if(NOT EXISTS "${_cmake_root}/CMakeLists.txt")
        _arken_extpkg_error(
            "arken.external_packages.cmake_external.source_missing"
            "adapter"
            "${recipe_id}"
            "CMake external source root does not contain CMakeLists.txt: ${_cmake_root}"
        )
    endif()

    _arken_extpkg_json_optional_string(
        _build_type
        "${recipe_json}"
        "Release"
        "${recipe_id}"
        adapter
        buildType
    )
    if(NOT _build_type STREQUAL "Debug"
            AND NOT _build_type STREQUAL "Release"
            AND NOT _build_type STREQUAL "RelWithDebInfo"
            AND NOT _build_type STREQUAL "MinSizeRel")
        _arken_extpkg_error(
            "arken.external_packages.cmake_external.build_type_invalid"
            "adapter"
            "${recipe_id}"
            "cmake-external buildType must be Debug, Release, RelWithDebInfo, or MinSizeRel."
        )
    endif()

    _arken_extpkg_json_type(_options_type "${recipe_json}" adapter options)
    set(_options_json "")
    set(_option_index 0)
    if(_options_type STREQUAL "OBJECT")
        string(JSON _option_count LENGTH "${recipe_json}" adapter options)
        if(_option_count GREATER 0)
            math(EXPR _option_last "${_option_count} - 1")
            foreach(_index RANGE 0 ${_option_last})
                string(JSON _name MEMBER "${recipe_json}" adapter options ${_index})
                string(JSON _value GET "${recipe_json}" adapter options "${_name}")
                string(JSON _value_type TYPE "${recipe_json}" adapter options "${_name}")
                if(NOT _name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$"
                        OR _name MATCHES "^(CMAKE|ARKEN)_")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_external.option_name_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-external option '${_name}' is not an upstream-scoped typed option."
                    )
                endif()
                if(NOT _value_type STREQUAL "BOOLEAN"
                        AND NOT _value_type STREQUAL "STRING"
                        AND NOT _value_type STREQUAL "NUMBER")
                    _arken_extpkg_error(
                        "arken.external_packages.cmake_external.option_invalid"
                        "adapter"
                        "${recipe_id}"
                        "CMake-external options accept boolean, string, or number values only: ${_name}"
                    )
                endif()
                _arken_extpkg_json_escape(_name_json "${_name}")
                _arken_extpkg_json_escape(_value_json "${_value}")
                if(_option_index GREATER 0)
                    string(APPEND _options_json ",\n")
                endif()
                string(APPEND _options_json
                    "    {\"name\":\"${_name_json}\",\"type\":\"${_value_type}\",\"value\":\"${_value_json}\"}"
                )
                math(EXPR _option_index "${_option_index} + 1")
            endforeach()
        endif()
    elseif(NOT _options_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.cmake_external.options_invalid"
            "adapter"
            "${recipe_id}"
            "adapter.options must be an object."
        )
    endif()

    set(_build_targets "")
    set(_output_relative_paths "")
    set(_build_include_directories "")
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

        _arken_extpkg_json_optional_string(
            _upstream_target
            "${_component}"
            ""
            "${recipe_id}:${_name}"
            upstreamTarget
        )
        _arken_extpkg_json_optional_string(
            _library_path
            "${_component}"
            ""
            "${recipe_id}:${_name}"
            libraryPath
        )
        _arken_extpkg_json_optional_string(
            _executable_path
            "${_component}"
            ""
            "${recipe_id}:${_name}"
            executablePath
        )
        if(_kind STREQUAL "static" OR _kind STREQUAL "shared")
            if(_upstream_target STREQUAL "" OR _library_path STREQUAL "")
                _arken_extpkg_error(
                    "arken.external_packages.cmake_external.component_incomplete"
                    "adapter"
                    "${recipe_id}"
                    "Compiled library component '${_name}' requires upstreamTarget and libraryPath."
                )
            endif()
        elseif(_kind STREQUAL "executable")
            if(_upstream_target STREQUAL "" OR _executable_path STREQUAL "")
                _arken_extpkg_error(
                    "arken.external_packages.cmake_external.component_incomplete"
                    "adapter"
                    "${recipe_id}"
                    "Executable component '${_name}' requires upstreamTarget and executablePath."
                )
            endif()
        elseif(NOT _kind STREQUAL "interface")
            _arken_extpkg_error(
                "arken.external_packages.cmake_external.component_kind_invalid"
                "adapter"
                "${recipe_id}"
                "CMake-external component '${_name}' has unsupported kind '${_kind}'."
            )
        endif()
        if(NOT _upstream_target STREQUAL "")
            if(NOT _upstream_target MATCHES "^[A-Za-z0-9_.+-]+$")
                _arken_extpkg_error(
                    "arken.external_packages.cmake_external.target_invalid"
                    "adapter"
                    "${recipe_id}"
                    "CMake-external upstream target is unsafe: ${_upstream_target}"
                )
            endif()
            list(APPEND _build_targets "${_upstream_target}")
        endif()
        foreach(_relative IN ITEMS "${_library_path}" "${_executable_path}")
            if(NOT _relative STREQUAL "")
                _arken_extpkg_validate_relative_component_path(
                    "${_relative}"
                    "${recipe_id}:${_name}"
                    outputPath
                )
                list(APPEND _output_relative_paths "${_relative}")
            endif()
        endforeach()
        _arken_extpkg_json_array_to_list(
            _build_files
            "${_component}"
            "${recipe_id}:${_name}"
            buildFiles
        )
        foreach(_relative IN LISTS _build_files)
            _arken_extpkg_validate_relative_component_path(
                "${_relative}"
                "${recipe_id}:${_name}"
                buildFiles
            )
            list(APPEND _output_relative_paths "${_relative}")
        endforeach()
        _arken_extpkg_json_array_to_list(
            _component_build_includes
            "${_component}"
            "${recipe_id}:${_name}"
            buildIncludeDirectories
        )
        _arken_extpkg_json_array_to_list(
            _component_build_system_includes
            "${_component}"
            "${recipe_id}:${_name}"
            buildSystemIncludeDirectories
        )
        list(APPEND
            _build_include_directories
            ${_component_build_includes}
            ${_component_build_system_includes}
        )
    endforeach()
    list(REMOVE_DUPLICATES _build_targets)
    list(SORT _build_targets)
    list(REMOVE_DUPLICATES _output_relative_paths)
    list(SORT _output_relative_paths)
    list(REMOVE_DUPLICATES _build_include_directories)
    list(SORT _build_include_directories)
    if(_build_targets STREQUAL "" OR _output_relative_paths STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.cmake_external.outputs_missing"
            "adapter"
            "${recipe_id}"
            "CMake-external requires at least one selected build target and output."
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
            "arken.external_packages.cmake_external.build_jobs_invalid"
            "adapter"
            "${recipe_id}"
            "ARKEN_EXTERNAL_PACKAGE_BUILD_JOBS must be a positive integer."
        )
    endif()

    set(_entry "${cache_root}/packages/${instance_id}")
    set(_external_build_root "${_entry}/build")
    set(_receipt "${_entry}/package.build.receipt.json")
    set(_external_target "arken-external-package-${recipe_id}")
    _arken_extpkg_identifier(_external_target "${_external_target}")
    file(MAKE_DIRECTORY "${cache_root}/locks" "${cache_root}/packages")
    foreach(_relative IN LISTS _output_relative_paths)
        get_filename_component(_output_parent "${_external_build_root}/${_relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${_output_parent}")
    endforeach()
    foreach(_relative IN LISTS _build_include_directories)
        if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "(^|/)\\.\\.(/|$)")
            _arken_extpkg_error(
                "arken.external_packages.cmake_external.include_path_unsafe"
                "adapter"
                "${recipe_id}"
                "CMake-external build include path must be build-root relative: ${_relative}"
            )
        endif()
        file(MAKE_DIRECTORY "${_external_build_root}/${_relative}")
    endforeach()

    _arken_extpkg_json_escape(_recipe_id_json "${recipe_id}")
    _arken_extpkg_json_escape(_recipe_sha_json "${recipe_sha}")
    _arken_extpkg_json_escape(_instance_id_json "${instance_id}")
    _arken_extpkg_json_escape(_source_identity_json "${source_identity}")
    _arken_extpkg_json_escape(_source_root_json "${source_root}")
    _arken_extpkg_json_escape(_cmake_root_json "${_cmake_root}")
    _arken_extpkg_json_escape(_entry_json "${_entry}")
    _arken_extpkg_json_escape(_build_type_json "${_build_type}")
    _arken_extpkg_json_escape(_generator_json "${CMAKE_GENERATOR}")
    _arken_extpkg_json_escape(_make_program_json "${CMAKE_MAKE_PROGRAM}")
    _arken_extpkg_json_escape(_c_compiler_json "${CMAKE_C_COMPILER}")
    _arken_extpkg_json_escape(_cxx_compiler_json "${CMAKE_CXX_COMPILER}")
    _arken_extpkg_json_escape(_c_flags_json "${CMAKE_C_FLAGS}")
    _arken_extpkg_json_escape(_cxx_flags_json "${CMAKE_CXX_FLAGS}")
    _arken_extpkg_json_escape(_exe_linker_flags_json "${CMAKE_EXE_LINKER_FLAGS}")
    _arken_extpkg_json_escape(_shared_linker_flags_json "${CMAKE_SHARED_LINKER_FLAGS}")
    _arken_extpkg_json_escape(_module_linker_flags_json "${CMAKE_MODULE_LINKER_FLAGS}")
    _arken_extpkg_json_escape(_toolchain_file_json "${CMAKE_TOOLCHAIN_FILE}")
    _arken_extpkg_json_escape(_sysroot_json "${CMAKE_SYSROOT}")
    _arken_extpkg_json_escape(_osx_sysroot_json "${CMAKE_OSX_SYSROOT}")
    _arken_extpkg_json_escape(_deployment_target_json "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    _arken_extpkg_json_escape(_architectures_json "${CMAKE_OSX_ARCHITECTURES}")
    _arken_extpkg_json_string_array(_build_targets_json "${_build_targets}")
    _arken_extpkg_json_string_array(_output_paths_json "${_output_relative_paths}")
    set(_input_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"recipeId\": \"${_recipe_id_json}\",\n"
        "  \"recipeSha256\": \"${_recipe_sha_json}\",\n"
        "  \"instanceId\": \"${_instance_id_json}\",\n"
        "  \"sourceIdentity\": \"${_source_identity_json}\",\n"
        "  \"sourceRoot\": \"${_source_root_json}\",\n"
        "  \"cmakeRoot\": \"${_cmake_root_json}\",\n"
        "  \"entryRoot\": \"${_entry_json}\",\n"
        "  \"buildType\": \"${_build_type_json}\",\n"
        "  \"buildJobs\": ${_build_jobs},\n"
        "  \"generator\": \"${_generator_json}\",\n"
        "  \"makeProgram\": \"${_make_program_json}\",\n"
        "  \"cCompiler\": \"${_c_compiler_json}\",\n"
        "  \"cxxCompiler\": \"${_cxx_compiler_json}\",\n"
        "  \"cFlags\": \"${_c_flags_json}\",\n"
        "  \"cxxFlags\": \"${_cxx_flags_json}\",\n"
        "  \"exeLinkerFlags\": \"${_exe_linker_flags_json}\",\n"
        "  \"sharedLinkerFlags\": \"${_shared_linker_flags_json}\",\n"
        "  \"moduleLinkerFlags\": \"${_module_linker_flags_json}\",\n"
        "  \"toolchainFile\": \"${_toolchain_file_json}\",\n"
        "  \"sysroot\": \"${_sysroot_json}\",\n"
        "  \"osxSysroot\": \"${_osx_sysroot_json}\",\n"
        "  \"deploymentTarget\": \"${_deployment_target_json}\",\n"
        "  \"architectures\": \"${_architectures_json}\",\n"
        "  \"options\": [\n${_options_json}\n  ],\n"
        "  \"buildTargets\": ${_build_targets_json},\n"
        "  \"outputRelativePaths\": ${_output_paths_json}\n"
        "}\n"
    )
    string(JOIN "" _input_json ${_input_json})
    set(_input_path "${package_build_root}/cmake-external-input.json")
    _arken_extpkg_write_if_different("${_input_path}" "${_input_json}")

    set(_byproducts "")
    foreach(_relative IN LISTS _output_relative_paths)
        list(APPEND _byproducts "${_external_build_root}/${_relative}")
    endforeach()
    ExternalProject_Add(
        "${_external_target}"
        SOURCE_DIR "${source_root}"
        BINARY_DIR "${package_build_root}/cmake-external-driver"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
            "${CMAKE_COMMAND}"
            "-DINPUT_PATH=${_input_path}"
            "-DBUILD_RECEIPT_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageBuildReceipt.cmake"
            "-DVERIFY_RECEIPTS_SCRIPT=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageVerifyReceipts.cmake"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageCmakeExternalBuild.cmake"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${_byproducts}
        BUILD_ALWAYS ON
        LOG_BUILD ON
        USES_TERMINAL_BUILD TRUE
    )
    _arken_extpkg_materialize_imported_components(
        "${recipe_json}"
        "${recipe_id}"
        "${_external_build_root}"
        "${selected_components}"
        "${_external_target}"
        "${source_root}"
    )
    foreach(_index RANGE 0 ${_component_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_json_required_string(
            _name
            "${_component}"
            "${recipe_id}:components[${_index}]"
            name
        )
        _arken_extpkg_json_required_string(
            _target
            "${_component}"
            "${recipe_id}:components[${_index}]"
            target
        )
        _arken_extpkg_component_selected(
            _selected
            "${_name}"
            "${selected_components}"
        )
        if(NOT _selected)
            continue()
        endif()
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_RECEIPT_PATH "${_receipt}"
        )
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_OUTPUT_ROOT
                "${_external_build_root}"
        )
        set_property(
            TARGET "${_target}"
            PROPERTY ARKEN_EXTERNAL_PACKAGE_COMPONENT_BUILD_TARGET
                "${_external_target}"
        )
    endforeach()
endfunction()
