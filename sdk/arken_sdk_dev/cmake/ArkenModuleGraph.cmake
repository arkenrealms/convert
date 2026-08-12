include_guard(GLOBAL)

function(_arken_manifest_array_to_list out_var json_text field_name)
    set(_values "")
    string(JSON _length ERROR_VARIABLE _field_error LENGTH "${json_text}" "${field_name}")
    if(_field_error STREQUAL "NOTFOUND")
        if(_length GREATER 0)
            math(EXPR _last_index "${_length} - 1")
            foreach(_index RANGE 0 ${_last_index})
                string(JSON _value GET "${json_text}" "${field_name}" ${_index})
                list(APPEND _values "${_value}")
            endforeach()
        endif()
    elseif(NOT _field_error MATCHES "member '.*' not found")
        message(FATAL_ERROR "Invalid module manifest field '${field_name}': ${_field_error}")
    endif()
    set(${out_var} "${_values}" PARENT_SCOPE)
endfunction()

function(_arken_register_module_manifest manifest_path kind)
    file(READ "${manifest_path}" _manifest_json)
    string(JSON _key GET "${_manifest_json}" key)
    if(_key STREQUAL "")
        message(FATAL_ERROR "Module manifest is missing 'key': ${manifest_path}")
    endif()

    string(MAKE_C_IDENTIFIER "${_key}" _key_ident)
    get_property(_existing_path GLOBAL PROPERTY "ARKEN_MODULE_PATH_${_key_ident}")
    if(NOT "${_existing_path}" STREQUAL "")
        message(FATAL_ERROR
            "Duplicate module key '${_key}' in '${manifest_path}' and '${_existing_path}'."
        )
    endif()

    get_filename_component(_module_dir "${manifest_path}" DIRECTORY)
    get_filename_component(_module_name "${_module_dir}" NAME)
    set_property(GLOBAL PROPERTY "ARKEN_MODULE_KIND_${_key_ident}" "${kind}")
    set_property(GLOBAL PROPERTY "ARKEN_MODULE_PATH_${_key_ident}" "${manifest_path}")
    set_property(GLOBAL PROPERTY "ARKEN_MODULE_DIR_${_key_ident}" "${_module_name}")
endfunction()

function(_arken_validate_root_app_boundary root_manifest capsule_root)
    get_filename_component(_root_manifest_name "${root_manifest}" NAME)
    if(NOT _root_manifest_name STREQUAL "app.manifest.json")
        return()
    endif()
    get_filename_component(_root_app_dir "${root_manifest}" DIRECTORY)
    get_filename_component(_app_collection_root "${_root_app_dir}" DIRECTORY)
    get_filename_component(_app_collection_name "${_app_collection_root}" NAME)
    if(NOT _app_collection_name STREQUAL "app")
        return()
    endif()
    get_filename_component(_root_app_name "${_root_app_dir}" NAME)
    file(READ "${root_manifest}" _root_manifest_json)
    _arken_manifest_array_to_list(
        _root_public_dependencies
        "${_root_manifest_json}"
        "public_dependencies"
    )
    _arken_manifest_array_to_list(
        _root_private_dependencies
        "${_root_manifest_json}"
        "private_dependencies"
    )
    _arken_manifest_array_to_list(
        _root_test_dependencies
        "${_root_manifest_json}"
        "test_dependencies"
    )
    set(
        _root_declared_dependencies
        ${_root_public_dependencies}
        ${_root_private_dependencies}
        ${_root_test_dependencies}
    )
    set(_capsule_dependency_keys "")
    file(
        GLOB
        _capsule_dependency_manifests
        LIST_DIRECTORIES FALSE
        "${capsule_root}/*/capsule.manifest.json"
    )
    foreach(_capsule_dependency_manifest IN LISTS _capsule_dependency_manifests)
        file(READ "${_capsule_dependency_manifest}" _capsule_dependency_json)
        string(JSON _capsule_dependency_key GET "${_capsule_dependency_json}" key)
        list(APPEND _capsule_dependency_keys "${_capsule_dependency_key}")
    endforeach()
    file(
        GLOB
        _sibling_app_manifests
        LIST_DIRECTORIES FALSE
        "${_app_collection_root}/*/app.manifest.json"
    )
    if(CMAKE_SCRIPT_MODE_FILE)
        file(
            GLOB
            _root_app_cmake_inputs
            LIST_DIRECTORIES FALSE
            "${_root_app_dir}/CMakeLists.txt"
            "${_root_app_dir}/*.cmake"
        )
        file(
            GLOB
            _root_app_top_level_entries
            LIST_DIRECTORIES TRUE
            "${_root_app_dir}/*"
        )
    else()
        file(
            GLOB
            _root_app_cmake_inputs
            LIST_DIRECTORIES FALSE
            CONFIGURE_DEPENDS
            "${_root_app_dir}/CMakeLists.txt"
            "${_root_app_dir}/*.cmake"
        )
        file(
            GLOB
            _root_app_top_level_entries
            LIST_DIRECTORIES TRUE
            CONFIGURE_DEPENDS
            "${_root_app_dir}/*"
        )
    endif()
    foreach(_root_app_top_level_entry IN LISTS _root_app_top_level_entries)
        if(NOT IS_DIRECTORY "${_root_app_top_level_entry}")
            continue()
        endif()
        get_filename_component(
            _root_app_top_level_name
            "${_root_app_top_level_entry}"
            NAME
        )
        if(_root_app_top_level_name MATCHES
                "^(\\.tmp|CMakeFiles|_deps|build[^/]*|e2e-artifacts|native-build|node_modules|package-cache|proof)$")
            continue()
        endif()
        if(CMAKE_SCRIPT_MODE_FILE)
            file(
                GLOB_RECURSE
                _root_app_nested_cmake_inputs
                LIST_DIRECTORIES FALSE
                "${_root_app_top_level_entry}/*.cmake"
            )
        else()
            file(
                GLOB_RECURSE
                _root_app_nested_cmake_inputs
                LIST_DIRECTORIES FALSE
                CONFIGURE_DEPENDS
                "${_root_app_top_level_entry}/*.cmake"
            )
        endif()
        list(APPEND
            _root_app_cmake_inputs
            ${_root_app_nested_cmake_inputs}
        )
    endforeach()
    list(REMOVE_DUPLICATES _root_app_cmake_inputs)
    list(SORT _root_app_cmake_inputs)
    foreach(_sibling_manifest IN LISTS _sibling_app_manifests)
        get_filename_component(_sibling_app_dir "${_sibling_manifest}" DIRECTORY)
        get_filename_component(_sibling_app_name "${_sibling_app_dir}" NAME)
        if(_sibling_app_name STREQUAL _root_app_name)
            continue()
        endif()
        file(READ "${_sibling_manifest}" _sibling_manifest_json)
        string(
            JSON
            _sibling_app_key
            ERROR_VARIABLE
            _sibling_app_key_error
            GET
            "${_sibling_manifest_json}"
            key
        )
        if(NOT _sibling_app_key_error STREQUAL "NOTFOUND")
            string(
                JSON
                _sibling_app_key
                ERROR_VARIABLE
                _sibling_app_key_error
                GET
                "${_sibling_manifest_json}"
                appId
            )
        endif()
        if(_sibling_app_key_error STREQUAL "NOTFOUND")
            list(FIND
                _root_declared_dependencies
                "${_sibling_app_key}"
                _declared_dependency_index
            )
            list(FIND
                _capsule_dependency_keys
                "${_sibling_app_key}"
                _capsule_dependency_index
            )
            if(NOT _declared_dependency_index EQUAL -1
                    AND _capsule_dependency_index EQUAL -1)
                message(FATAL_ERROR
                    "Application-to-application dependency is forbidden: "
                    "'${_root_app_name}' declares app '${_sibling_app_name}' "
                    "(${_sibling_app_key}) as a module dependency. Move the "
                    "shared capability into a capsule or library."
                )
            endif()
        endif()
        foreach(_cmake_input IN LISTS _root_app_cmake_inputs)
            file(STRINGS "${_cmake_input}" _cmake_source_lines)
            foreach(_cmake_source_line IN LISTS _cmake_source_lines)
                string(STRIP "${_cmake_source_line}" _cmake_source_line)
                if(_cmake_source_line MATCHES "^#"
                        OR _cmake_source_line MATCHES
                            "(^|[ \t])MATCHES([ \t]|$)")
                    continue()
                endif()
                foreach(_forbidden_reference IN ITEMS
                        "app/${_sibling_app_name}/"
                        "app/${_sibling_app_name}\""
                        "app/${_sibling_app_name}'"
                        "../${_sibling_app_name}/"
                        "../${_sibling_app_name}\""
                        "../${_sibling_app_name}'")
                    string(FIND
                        "${_cmake_source_line}"
                        "${_forbidden_reference}"
                        _forbidden_reference_index
                    )
                    if(NOT _forbidden_reference_index EQUAL -1)
                        message(FATAL_ERROR
                            "Application-to-application CMake path is forbidden: "
                            "'${_root_app_name}' references sibling app "
                            "'${_sibling_app_name}' in '${_cmake_input}'. Move "
                            "the shared target, fixture, generated asset, or "
                            "build helper into its owning capsule or library."
                        )
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endforeach()
endfunction()

function(_arken_visit_module module_key)
    string(MAKE_C_IDENTIFIER "${module_key}" _key_ident)

    get_property(_visited GLOBAL PROPERTY ARKEN_GRAPH_VISITED)
    list(FIND _visited "${module_key}" _visited_index)
    if(NOT _visited_index EQUAL -1)
        return()
    endif()

    get_property(_visiting GLOBAL PROPERTY ARKEN_GRAPH_VISITING)
    list(FIND _visiting "${module_key}" _visiting_index)
    if(NOT _visiting_index EQUAL -1)
        message(FATAL_ERROR "Detected a module dependency cycle involving '${module_key}'.")
    endif()

    get_property(_manifest_path GLOBAL PROPERTY "ARKEN_MODULE_PATH_${_key_ident}")
    if(_manifest_path STREQUAL "")
        message(FATAL_ERROR "Unknown module dependency '${module_key}'.")
    endif()

    get_property(_kind GLOBAL PROPERTY "ARKEN_MODULE_KIND_${_key_ident}")
    get_property(_module_dir GLOBAL PROPERTY "ARKEN_MODULE_DIR_${_key_ident}")
    file(READ "${_manifest_path}" _manifest_json)

    set_property(GLOBAL APPEND PROPERTY ARKEN_GRAPH_VISITING "${module_key}")

    _arken_manifest_array_to_list(_public_dependencies "${_manifest_json}" "public_dependencies")
    _arken_manifest_array_to_list(_private_dependencies "${_manifest_json}" "private_dependencies")
    set(_module_dependencies ${_public_dependencies} ${_private_dependencies})
    if(BUILD_TESTING)
        _arken_manifest_array_to_list(_test_dependencies "${_manifest_json}" "test_dependencies")
        list(APPEND _module_dependencies ${_test_dependencies})
    endif()
    foreach(_dependency IN LISTS _module_dependencies)
        _arken_visit_module("${_dependency}")
    endforeach()

    if(_kind STREQUAL "capsule")
        get_property(_enabled_dirs GLOBAL PROPERTY ARKEN_GRAPH_ENABLED_CAPSULE_DIRS)
        list(FIND _enabled_dirs "${_module_dir}" _enabled_index)
        if(_enabled_index EQUAL -1)
            set_property(GLOBAL APPEND PROPERTY ARKEN_GRAPH_ENABLED_CAPSULE_DIRS "${_module_dir}")
        endif()
    endif()

    get_property(_visiting GLOBAL PROPERTY ARKEN_GRAPH_VISITING)
    list(REMOVE_ITEM _visiting "${module_key}")
    set_property(GLOBAL PROPERTY ARKEN_GRAPH_VISITING "${_visiting}")
    set_property(GLOBAL APPEND PROPERTY ARKEN_GRAPH_VISITED "${module_key}")
endfunction()

function(arken_resolve_enabled_capsules out_var)
    set(options)
    set(oneValueArgs ROOT_MANIFEST CAPSULE_ROOT LIB_ROOT)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_ROOT_MANIFEST OR NOT EXISTS "${ARG_ROOT_MANIFEST}")
        message(FATAL_ERROR "Root manifest not found: ${ARG_ROOT_MANIFEST}")
    endif()
    if(NOT ARG_CAPSULE_ROOT OR NOT EXISTS "${ARG_CAPSULE_ROOT}")
        message(FATAL_ERROR "Capsule root not found: ${ARG_CAPSULE_ROOT}")
    endif()
    if(NOT ARG_LIB_ROOT)
        set(ARG_LIB_ROOT "")
    endif()

    _arken_validate_root_app_boundary(
        "${ARG_ROOT_MANIFEST}"
        "${ARG_CAPSULE_ROOT}"
    )

    set_property(GLOBAL PROPERTY ARKEN_GRAPH_VISITING "")
    set_property(GLOBAL PROPERTY ARKEN_GRAPH_VISITED "")
    set_property(GLOBAL PROPERTY ARKEN_GRAPH_ENABLED_CAPSULE_DIRS "")

    file(GLOB _capsule_manifests LIST_DIRECTORIES FALSE "${ARG_CAPSULE_ROOT}/*/capsule.manifest.json")
    foreach(_manifest_path IN LISTS _capsule_manifests)
        _arken_register_module_manifest("${_manifest_path}" "capsule")
    endforeach()

    if(NOT ARG_LIB_ROOT STREQUAL "" AND EXISTS "${ARG_LIB_ROOT}")
        file(GLOB _lib_manifests LIST_DIRECTORIES FALSE "${ARG_LIB_ROOT}/*_dev/lib.manifest.json")
        foreach(_manifest_path IN LISTS _lib_manifests)
            _arken_register_module_manifest("${_manifest_path}" "lib")
        endforeach()
    endif()

    file(READ "${ARG_ROOT_MANIFEST}" _root_manifest_json)
    _arken_manifest_array_to_list(_root_public_dependencies "${_root_manifest_json}" "public_dependencies")
    _arken_manifest_array_to_list(_root_private_dependencies "${_root_manifest_json}" "private_dependencies")
    set(_root_dependencies
        ${_root_public_dependencies}
        ${_root_private_dependencies}
    )
    if(BUILD_TESTING)
        _arken_manifest_array_to_list(
            _root_test_dependencies
            "${_root_manifest_json}"
            "test_dependencies"
        )
        list(APPEND _root_dependencies ${_root_test_dependencies})
    endif()
    foreach(_dependency IN LISTS _root_dependencies)
        _arken_visit_module("${_dependency}")
    endforeach()

    get_property(_enabled_dirs GLOBAL PROPERTY ARKEN_GRAPH_ENABLED_CAPSULE_DIRS)
    list(REMOVE_DUPLICATES _enabled_dirs)
    set(${out_var} "${_enabled_dirs}" PARENT_SCOPE)
endfunction()
