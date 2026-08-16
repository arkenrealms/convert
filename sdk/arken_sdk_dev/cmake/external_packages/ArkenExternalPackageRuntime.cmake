include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

function(_arken_extpkg_runtime_metadata_target out_var target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "External package component target is missing: ${target_name}")
    endif()
    get_target_property(_aliased "${target_name}" ALIASED_TARGET)
    if(_aliased)
        set(${out_var} "${_aliased}" PARENT_SCOPE)
    else()
        set(${out_var} "${target_name}" PARENT_SCOPE)
    endif()
endfunction()

function(arken_generate_external_package_cpp_identity)
    set(options)
    set(oneValueArgs
        TARGET
        PACKAGE_TARGET
        OUTPUT_HEADER
        IDENTITY_TYPE
        OUT_HEADER
    )
    set(multiValueArgs OUTPUT_RELATIVE_PATHS)
    cmake_parse_arguments(
        ARG
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    foreach(_required IN ITEMS TARGET PACKAGE_TARGET OUTPUT_HEADER IDENTITY_TYPE)
        if(NOT ARG_${_required})
            message(FATAL_ERROR
                "arken_generate_external_package_cpp_identity requires "
                "${_required}")
        endif()
    endforeach()
    if(NOT ARG_TARGET MATCHES "^[A-Za-z_][A-Za-z0-9_.+-]*$"
            OR NOT ARG_IDENTITY_TYPE MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR
            "External package C++ identity target or type is invalid")
    endif()
    if(TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "External package C++ identity target already exists: "
            "${ARG_TARGET}")
    endif()
    if(IS_ABSOLUTE "${ARG_OUTPUT_HEADER}")
        cmake_path(
            NORMAL_PATH
            ARG_OUTPUT_HEADER
            OUTPUT_VARIABLE _output_header
        )
    else()
        cmake_path(
            ABSOLUTE_PATH
            ARG_OUTPUT_HEADER
            BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            NORMALIZE
            OUTPUT_VARIABLE _output_header
        )
    endif()
    cmake_path(
        RELATIVE_PATH
        _output_header
        BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_VARIABLE _output_header_relative
    )
    if(_output_header_relative MATCHES "^\\.\\.(/|$)")
        message(FATAL_ERROR
            "External package C++ identity header must remain inside the "
            "active build tree: ${_output_header}")
    endif()

    _arken_extpkg_runtime_metadata_target(
        _metadata_target
        "${ARG_PACKAGE_TARGET}"
    )
    foreach(_property IN ITEMS
            ARKEN_EXTERNAL_PACKAGE_RECIPE_ID
            ARKEN_EXTERNAL_PACKAGE_RECIPE_SHA256
            ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID
            ARKEN_EXTERNAL_PACKAGE_SOURCE_IDENTITY
            ARKEN_EXTERNAL_PACKAGE_ADAPTER_ID
            ARKEN_EXTERNAL_PACKAGE_RECEIPT_ROOT
            ARKEN_EXTERNAL_PACKAGE_RUNTIME_ROOT
            ARKEN_EXTERNAL_PACKAGE_BUILD_TARGET)
        get_target_property(_value "${_metadata_target}" "${_property}")
        if(NOT _value)
            message(FATAL_ERROR
                "External package target ${ARG_PACKAGE_TARGET} lacks "
                "${_property}")
        endif()
        set("_${_property}" "${_value}")
    endforeach()

    set(_requested_outputs "${ARG_OUTPUT_RELATIVE_PATHS}")
    list(REMOVE_DUPLICATES _requested_outputs)
    list(SORT _requested_outputs)
    foreach(_relative IN LISTS _requested_outputs)
        if(_relative STREQUAL ""
                OR IS_ABSOLUTE "${_relative}"
                OR _relative MATCHES "(^|/)\\.\\.(/|$)"
                OR _relative MATCHES "[|;\n\r]")
            message(FATAL_ERROR
                "External package C++ identity output path is unsafe: "
                "${_relative}")
        endif()
    endforeach()
    _arken_extpkg_json_string_array(
        _requested_outputs_json
        "${_requested_outputs}"
    )

    set(_manifest_dir
        "${CMAKE_CURRENT_BINARY_DIR}/_arken/external-package-identity-manifests")
    file(MAKE_DIRECTORY "${_manifest_dir}")
    set(_manifest_path "${_manifest_dir}/${ARG_TARGET}.json")
    foreach(_field IN ITEMS
            _ARKEN_EXTERNAL_PACKAGE_RECIPE_ID
            _ARKEN_EXTERNAL_PACKAGE_RECIPE_SHA256
            _ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID
            _ARKEN_EXTERNAL_PACKAGE_SOURCE_IDENTITY
            _ARKEN_EXTERNAL_PACKAGE_ADAPTER_ID
            _ARKEN_EXTERNAL_PACKAGE_RECEIPT_ROOT
            _ARKEN_EXTERNAL_PACKAGE_RUNTIME_ROOT
            _output_header
            ARG_IDENTITY_TYPE)
        _arken_extpkg_json_escape("${_field}_json" "${${_field}}")
    endforeach()
    string(CONCAT _manifest
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"identityType\": \"${ARG_IDENTITY_TYPE_json}\",\n"
        "  \"recipeId\": \"${_ARKEN_EXTERNAL_PACKAGE_RECIPE_ID_json}\",\n"
        "  \"recipeSha256\": \"${_ARKEN_EXTERNAL_PACKAGE_RECIPE_SHA256_json}\",\n"
        "  \"packageInstanceId\": \"${_ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID_json}\",\n"
        "  \"adapterId\": \"${_ARKEN_EXTERNAL_PACKAGE_ADAPTER_ID_json}\",\n"
        "  \"sourceIdentity\": \"${_ARKEN_EXTERNAL_PACKAGE_SOURCE_IDENTITY_json}\",\n"
        "  \"packageRoot\": \"${_ARKEN_EXTERNAL_PACKAGE_RUNTIME_ROOT_json}\",\n"
        "  \"receiptPath\": \"${_ARKEN_EXTERNAL_PACKAGE_RECEIPT_ROOT_json}/package.build.receipt.json\",\n"
        "  \"outputHeader\": \"${_output_header_json}\",\n"
        "  \"requestedOutputs\": ${_requested_outputs_json}\n"
        "}\n"
    )
    _arken_extpkg_write_if_different("${_manifest_path}" "${_manifest}")

    add_custom_target(
        "${ARG_TARGET}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMANIFEST_PATH=${_manifest_path}"
            -P
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageGenerateIdentityHeader.cmake"
        BYPRODUCTS "${_output_header}"
        DEPENDS
            "${_manifest_path}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageGenerateIdentityHeader.cmake"
        VERBATIM
    )
    add_dependencies(
        "${ARG_TARGET}"
        "${_ARKEN_EXTERNAL_PACKAGE_BUILD_TARGET}"
    )
    set_property(
        TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_PACKAGE_IDENTITY_HEADER "${_output_header}"
    )
    if(ARG_OUT_HEADER)
        set(${ARG_OUT_HEADER} "${_output_header}" PARENT_SCOPE)
    endif()
endfunction()

function(arken_stage_external_package_runtime)
    set(options)
    set(oneValueArgs
        TARGET
        DESTINATION_ROOT
        OUT_RUNTIME_ROOT
        OUT_RECEIPT
    )
    set(multiValueArgs COMPONENT_TARGETS)
    cmake_parse_arguments(
        ARG
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    if(NOT ARG_TARGET
            OR NOT ARG_TARGET MATCHES "^[A-Za-z_][A-Za-z0-9_.+-]*$")
        message(FATAL_ERROR
            "arken_stage_external_package_runtime requires a stable TARGET name")
    endif()
    if(TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "External package runtime staging target already exists: ${ARG_TARGET}")
    endif()
    if(ARG_COMPONENT_TARGETS STREQUAL "")
        message(FATAL_ERROR
            "External package runtime staging requires COMPONENT_TARGETS")
    endif()
    if(ARG_DESTINATION_ROOT)
        if(NOT IS_ABSOLUTE "${ARG_DESTINATION_ROOT}")
            message(FATAL_ERROR
                "External package runtime DESTINATION_ROOT must be absolute")
        endif()
        cmake_path(
            NORMAL_PATH
            ARG_DESTINATION_ROOT
            OUTPUT_VARIABLE _destination_base
        )
    else()
        set(_destination_base
            "${CMAKE_CURRENT_BINARY_DIR}/_arken/external-package-runtime")
    endif()

    set(_recipe_id "")
    set(_instance_id "")
    set(_package_root "")
    set(_build_targets "")
    set(_records "")
    foreach(_component_target IN LISTS ARG_COMPONENT_TARGETS)
        if(NOT TARGET "${_component_target}")
            message(FATAL_ERROR
                "External package runtime component target is missing: "
                "${_component_target}")
        endif()
        _arken_extpkg_runtime_metadata_target(
            _metadata_target
            "${_component_target}"
        )
        get_target_property(_component_recipe
            "${_metadata_target}" ARKEN_EXTERNAL_PACKAGE_RECIPE_ID)
        get_target_property(_component_instance
            "${_metadata_target}" ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID)
        get_target_property(_component_root
            "${_metadata_target}" ARKEN_EXTERNAL_PACKAGE_RUNTIME_ROOT)
        get_target_property(_component_build_target
            "${_metadata_target}" ARKEN_EXTERNAL_PACKAGE_BUILD_TARGET)
        get_target_property(_component_sources
            "${_metadata_target}" ARKEN_EXTERNAL_RUNTIME_FILES)
        get_target_property(_component_relative
            "${_metadata_target}" ARKEN_EXTERNAL_RUNTIME_RELATIVE_FILES)
        if(NOT _component_recipe
                OR NOT _component_instance
                OR NOT _component_root
                OR _component_sources STREQUAL ""
                OR _component_relative STREQUAL "")
            message(FATAL_ERROR
                "External package target lacks runtime staging metadata: "
                "${_component_target}")
        endif()
        if(_recipe_id STREQUAL "")
            set(_recipe_id "${_component_recipe}")
            set(_instance_id "${_component_instance}")
            set(_package_root "${_component_root}")
        elseif(NOT _recipe_id STREQUAL _component_recipe
                OR NOT _instance_id STREQUAL _component_instance
                OR NOT _package_root STREQUAL _component_root)
            message(FATAL_ERROR
                "One runtime staging target may contain only one package instance")
        endif()
        if(_component_build_target)
            list(APPEND _build_targets "${_component_build_target}")
        endif()
        list(LENGTH _component_sources _source_count)
        list(LENGTH _component_relative _relative_count)
        if(NOT _source_count EQUAL _relative_count)
            message(FATAL_ERROR
                "External package runtime metadata is misaligned: "
                "${_component_target}")
        endif()
        math(EXPR _source_last "${_source_count} - 1")
        foreach(_index RANGE 0 ${_source_last})
            list(GET _component_sources ${_index} _source)
            list(GET _component_relative ${_index} _relative)
            if(_source MATCHES "[|;\n\r]"
                    OR _relative MATCHES "[|;\n\r]")
                message(FATAL_ERROR
                    "External package runtime path contains an unsafe separator")
            endif()
            list(APPEND _records "${_relative}|${_source}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _build_targets)
    list(REMOVE_DUPLICATES _records)
    list(SORT _records)
    set(_runtime_root
        "${_destination_base}/${_recipe_id}/${_instance_id}")
    set(_manifest_dir
        "${CMAKE_CURRENT_BINARY_DIR}/_arken/external-package-runtime-manifests")
    file(MAKE_DIRECTORY "${_manifest_dir}")
    set(_manifest_path "${_manifest_dir}/${ARG_TARGET}.json")
    _arken_extpkg_json_escape(_package_root_json "${_package_root}")
    set(_files_json "")
    foreach(_record IN LISTS _records)
        string(FIND "${_record}" "|" _separator)
        string(SUBSTRING "${_record}" 0 ${_separator} _relative)
        math(EXPR _source_start "${_separator} + 1")
        string(SUBSTRING "${_record}" ${_source_start} -1 _source)
        _arken_extpkg_json_escape(_relative_json "${_relative}")
        _arken_extpkg_json_escape(_source_json "${_source}")
        if(NOT _files_json STREQUAL "")
            string(APPEND _files_json ",\n")
        endif()
        string(APPEND _files_json
            "    {\"relativePath\":\"${_relative_json}\",\"source\":\"${_source_json}\"}"
        )
    endforeach()
    set(_manifest
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"recipeId\": \"${_recipe_id}\",\n"
        "  \"packageInstanceId\": \"${_instance_id}\",\n"
        "  \"packageRoot\": \"${_package_root_json}\",\n"
        "  \"files\": [\n${_files_json}\n  ]\n"
        "}\n"
    )
    string(JOIN "" _manifest ${_manifest})
    _arken_extpkg_write_if_different("${_manifest_path}" "${_manifest}")
    set(_receipt "${_runtime_root}/runtime.staging.receipt.json")
    add_custom_target(
        "${ARG_TARGET}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMANIFEST_PATH=${_manifest_path}"
            "-DDESTINATION_ROOT=${_runtime_root}"
            -P
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageStageRuntime.cmake"
        DEPENDS
            "${_manifest_path}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArkenExternalPackageStageRuntime.cmake"
            ${_build_targets}
        VERBATIM
    )
    if(NOT _build_targets STREQUAL "")
        add_dependencies("${ARG_TARGET}" ${_build_targets})
    endif()
    set_property(TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_PACKAGE_RECIPE_ID "${_recipe_id}")
    set_property(TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_PACKAGE_INSTANCE_ID "${_instance_id}")
    set_property(TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_RUNTIME_ROOT "${_runtime_root}")
    set_property(TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_RUNTIME_RECEIPT "${_receipt}")
    if(ARG_OUT_RUNTIME_ROOT)
        set(${ARG_OUT_RUNTIME_ROOT} "${_runtime_root}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_RECEIPT)
        set(${ARG_OUT_RECEIPT} "${_receipt}" PARENT_SCOPE)
    endif()
endfunction()

function(arken_attach_external_package_runtime)
    set(options)
    set(oneValueArgs TARGET STAGE_TARGET RUNTIME_ROOT SCOPE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})
    if(NOT ARG_TARGET OR NOT TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "arken_attach_external_package_runtime TARGET is missing")
    endif()
    if(NOT ARG_STAGE_TARGET OR NOT TARGET "${ARG_STAGE_TARGET}")
        message(FATAL_ERROR
            "arken_attach_external_package_runtime STAGE_TARGET is missing")
    endif()
    if(NOT ARG_RUNTIME_ROOT OR NOT IS_ABSOLUTE "${ARG_RUNTIME_ROOT}")
        message(FATAL_ERROR
            "arken_attach_external_package_runtime RUNTIME_ROOT must be absolute")
    endif()
    if(NOT ARG_SCOPE)
        set(ARG_SCOPE PRIVATE)
    endif()
    if(NOT ARG_SCOPE STREQUAL "PRIVATE"
            AND NOT ARG_SCOPE STREQUAL "PUBLIC"
            AND NOT ARG_SCOPE STREQUAL "INTERFACE")
        message(FATAL_ERROR
            "External package runtime attachment scope is invalid: ${ARG_SCOPE}")
    endif()
    add_dependencies("${ARG_TARGET}" "${ARG_STAGE_TARGET}")
    if(APPLE OR UNIX)
        target_link_options(
            "${ARG_TARGET}"
            ${ARG_SCOPE}
            "LINKER:-rpath,${ARG_RUNTIME_ROOT}/lib"
        )
    endif()
    set_property(TARGET "${ARG_TARGET}"
        PROPERTY ARKEN_EXTERNAL_RUNTIME_ROOT "${ARG_RUNTIME_ROOT}")
endfunction()
