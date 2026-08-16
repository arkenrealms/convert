if(NOT DEFINED RECEIPT_PATH OR RECEIPT_PATH STREQUAL "")
    message(FATAL_ERROR "RECEIPT_PATH is required")
endif()
foreach(_required IN ITEMS RECIPE_ID RECIPE_SHA256 INSTANCE_ID ADAPTER_ID SOURCE_IDENTITY)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

set(_output_files "")
if(DEFINED OUTPUT_FILES_JOINED AND NOT OUTPUT_FILES_JOINED STREQUAL "")
    string(REPLACE "|" ";" _output_files "${OUTPUT_FILES_JOINED}")
endif()
if(DEFINED OUTPUT_ROOT
        AND NOT OUTPUT_ROOT STREQUAL ""
        AND DEFINED OUTPUT_RELATIVE_PATHS_JOINED
        AND NOT OUTPUT_RELATIVE_PATHS_JOINED STREQUAL "")
    string(REPLACE "|" ";" _relative_paths "${OUTPUT_RELATIVE_PATHS_JOINED}")
    foreach(_relative IN LISTS _relative_paths)
        list(APPEND _output_files "${OUTPUT_ROOT}/${_relative}")
    endforeach()
endif()
list(REMOVE_DUPLICATES _output_files)
list(SORT _output_files)
if(_output_files STREQUAL "")
    message(FATAL_ERROR "At least one output file is required")
endif()

set(_outputs_json "")
set(_output_identity_input "")
set(_output_index 0)
foreach(_output IN LISTS _output_files)
    if(NOT EXISTS "${_output}" OR IS_DIRECTORY "${_output}" OR IS_SYMLINK "${_output}")
        message(FATAL_ERROR
            "Arken external package '${RECIPE_ID}' expected output is missing "
            "or is not a regular file: ${_output}"
        )
    endif()
    file(SHA256 "${_output}" _sha256)
    file(SIZE "${_output}" _bytes)
    set(_recorded_output "${_output}")
    if(DEFINED RECORDED_OUTPUT_ROOT
            AND NOT RECORDED_OUTPUT_ROOT STREQUAL ""
            AND DEFINED OUTPUT_ROOT
            AND NOT OUTPUT_ROOT STREQUAL "")
        cmake_path(RELATIVE_PATH _output BASE_DIRECTORY "${OUTPUT_ROOT}" OUTPUT_VARIABLE _relative_output)
        set(_recorded_output "${RECORDED_OUTPUT_ROOT}/${_relative_output}")
    endif()
    _arken_extpkg_json_escape(_path_json "${_recorded_output}")
    if(_output_index GREATER 0)
        string(APPEND _outputs_json ",\n")
    endif()
    string(APPEND _outputs_json
        "    {\"path\":\"${_path_json}\",\"bytes\":${_bytes},\"sha256\":\"${_sha256}\"}"
    )
    string(APPEND _output_identity_input "${_recorded_output}:${_bytes}:${_sha256}\n")
    math(EXPR _output_index "${_output_index} + 1")
endforeach()
string(SHA256 _outputs_sha256 "${_output_identity_input}")

_arken_extpkg_json_escape(_source_identity_json "${SOURCE_IDENTITY}")
set(_configuration_json "")
if(ADAPTER_ID STREQUAL "configure-make-v1")
    if(NOT DEFINED INSTALL_MODE
            OR (NOT INSTALL_MODE STREQUAL "direct"
                AND NOT INSTALL_MODE STREQUAL "destdir"))
        message(FATAL_ERROR
            "Configure/Make package receipts require install mode direct or destdir"
        )
    endif()
    set(_configure_arguments "")
    if(DEFINED CONFIGURE_ARGUMENTS_JOINED
            AND NOT CONFIGURE_ARGUMENTS_JOINED STREQUAL "")
        string(REPLACE "|" ";" _configure_arguments "${CONFIGURE_ARGUMENTS_JOINED}")
    endif()
    _arken_extpkg_json_string_array(
        _configure_arguments_json
        "${_configure_arguments}"
    )
    set(_configuration_identity_input "installMode=${INSTALL_MODE}\n")
    foreach(_argument IN LISTS _configure_arguments)
        string(APPEND _configuration_identity_input "argument=${_argument}\n")
    endforeach()
    set(_logs_json "")
    set(_log_index 0)
    foreach(_log_kind IN ITEMS stdout stderr)
        string(TOUPPER "${_log_kind}" _log_kind_upper)
        set(_log_path_variable "CONFIGURE_${_log_kind_upper}_PATH")
        if(NOT DEFINED ${_log_path_variable}
                OR NOT EXISTS "${${_log_path_variable}}"
                OR IS_DIRECTORY "${${_log_path_variable}}"
                OR IS_SYMLINK "${${_log_path_variable}}")
            message(FATAL_ERROR
                "Configure/Make package receipt is missing configure ${_log_kind} log"
            )
        endif()
        file(SHA256 "${${_log_path_variable}}" _log_sha256)
        file(SIZE "${${_log_path_variable}}" _log_bytes)
        if(DEFINED RECORDED_LOG_ROOT AND NOT RECORDED_LOG_ROOT STREQUAL "")
            set(_recorded_log
                "${RECORDED_LOG_ROOT}/configure.${_log_kind}.log")
        else()
            set(_recorded_log "${${_log_path_variable}}")
        endif()
        _arken_extpkg_json_escape(_recorded_log_json "${_recorded_log}")
        if(_log_index GREATER 0)
            string(APPEND _logs_json ",\n")
        endif()
        string(APPEND _logs_json
            "      {\"kind\":\"${_log_kind}\",\"path\":\"${_recorded_log_json}\",\"bytes\":${_log_bytes},\"sha256\":\"${_log_sha256}\"}"
        )
        string(APPEND _configuration_identity_input
            "log=${_recorded_log}:${_log_bytes}:${_log_sha256}\n"
        )
        math(EXPR _log_index "${_log_index} + 1")
    endforeach()
    string(SHA256 _configuration_sha256 "${_configuration_identity_input}")
    set(_configuration_json
        ",\n"
        "  \"configuration\": {\n"
        "    \"installMode\": \"${INSTALL_MODE}\",\n"
        "    \"arguments\": ${_configure_arguments_json},\n"
        "    \"logs\": [\n${_logs_json}\n    ],\n"
        "    \"sha256\": \"${_configuration_sha256}\"\n"
        "  }"
    )
    string(JOIN "" _configuration_json ${_configuration_json})
endif()
set(_receipt
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"recipeId\": \"${RECIPE_ID}\",\n"
    "  \"recipeSha256\": \"${RECIPE_SHA256}\",\n"
    "  \"packageInstanceId\": \"${INSTANCE_ID}\",\n"
    "  \"adapterId\": \"${ADAPTER_ID}\",\n"
    "  \"sourceIdentity\": \"${_source_identity_json}\",\n"
    "  \"outputsSha256\": \"${_outputs_sha256}\",\n"
    "  \"outputs\": [\n${_outputs_json}\n  ]${_configuration_json}\n"
    "}\n"
)
string(JOIN "" _receipt ${_receipt})
_arken_extpkg_write_if_different("${RECEIPT_PATH}" "${_receipt}")
