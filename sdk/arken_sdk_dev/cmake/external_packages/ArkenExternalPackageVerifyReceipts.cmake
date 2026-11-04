set(_receipt_roots "")
if(DEFINED RECEIPT_ROOT AND NOT RECEIPT_ROOT STREQUAL "")
    list(APPEND _receipt_roots "${RECEIPT_ROOT}")
endif()
if(DEFINED RECEIPT_ROOTS_JOINED AND NOT RECEIPT_ROOTS_JOINED STREQUAL "")
    string(REPLACE "|" ";" _joined_roots "${RECEIPT_ROOTS_JOINED}")
    list(APPEND _receipt_roots ${_joined_roots})
endif()
list(REMOVE_DUPLICATES _receipt_roots)
if(_receipt_roots STREQUAL "")
    message(FATAL_ERROR "RECEIPT_ROOT or RECEIPT_ROOTS_JOINED is required")
endif()
set(_receipts "")
foreach(_receipt_root IN LISTS _receipt_roots)
    if(NOT EXISTS "${_receipt_root}")
        continue()
    endif()
    file(GLOB_RECURSE _root_receipts LIST_DIRECTORIES FALSE "${_receipt_root}/*.build.receipt.json")
    list(APPEND _receipts ${_root_receipts})
endforeach()
if(_receipts STREQUAL "")
    message(STATUS "Arken external package receipt verification: no receipts materialized")
    return()
endif()
list(REMOVE_DUPLICATES _receipts)
list(SORT _receipts)
foreach(_receipt IN LISTS _receipts)
    if(_receipt MATCHES "\\.quarantine-" OR _receipt MATCHES "\\.partial-")
        continue()
    endif()
    file(READ "${_receipt}" _json)
    string(JSON _recipe_id ERROR_VARIABLE _recipe_error GET "${_json}" recipeId)
    string(JSON _adapter_id ERROR_VARIABLE _adapter_error GET "${_json}" adapterId)
    string(JSON _expected_outputs_sha ERROR_VARIABLE _identity_error GET "${_json}" outputsSha256)
    string(JSON _output_count ERROR_VARIABLE _outputs_error LENGTH "${_json}" outputs)
    if(NOT _recipe_error STREQUAL "NOTFOUND"
            OR NOT _adapter_error STREQUAL "NOTFOUND"
            OR NOT _identity_error STREQUAL "NOTFOUND"
            OR NOT _outputs_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR "Malformed external package build receipt: ${_receipt}")
    endif()
    set(_identity_input "")
    if(_output_count GREATER 0)
        math(EXPR _last "${_output_count} - 1")
        foreach(_index RANGE 0 ${_last})
            string(JSON _path GET "${_json}" outputs ${_index} path)
            string(JSON _expected_bytes GET "${_json}" outputs ${_index} bytes)
            string(JSON _expected_sha GET "${_json}" outputs ${_index} sha256)
            if(NOT EXISTS "${_path}" OR IS_DIRECTORY "${_path}" OR IS_SYMLINK "${_path}")
                message(FATAL_ERROR
                    "Arken external package '${_recipe_id}' output is missing: ${_path}"
                )
            endif()
            file(SIZE "${_path}" _observed_bytes)
            file(SHA256 "${_path}" _observed_sha)
            if(NOT _observed_bytes EQUAL _expected_bytes
                    OR NOT "${_observed_sha}" STREQUAL "${_expected_sha}")
                message(FATAL_ERROR
                    "Arken external package '${_recipe_id}' output identity mismatch: "
                    "${_path}; expected ${_expected_bytes}/${_expected_sha}, "
                    "observed ${_observed_bytes}/${_observed_sha}"
                )
            endif()
            string(APPEND _identity_input "${_path}:${_observed_bytes}:${_observed_sha}\n")
        endforeach()
    endif()
    string(SHA256 _observed_outputs_sha "${_identity_input}")
    if(NOT "${_observed_outputs_sha}" STREQUAL "${_expected_outputs_sha}")
        message(FATAL_ERROR
            "Arken external package '${_recipe_id}' aggregate output identity mismatch."
        )
    endif()
    if(_adapter_id STREQUAL "configure-make-v1")
        string(JSON _install_mode ERROR_VARIABLE _install_mode_error
            GET "${_json}" configuration installMode)
        string(JSON _argument_count ERROR_VARIABLE _arguments_error
            LENGTH "${_json}" configuration arguments)
        string(JSON _log_count ERROR_VARIABLE _logs_error
            LENGTH "${_json}" configuration logs)
        string(JSON _expected_configuration_sha ERROR_VARIABLE _configuration_error
            GET "${_json}" configuration sha256)
        if(NOT _install_mode_error STREQUAL "NOTFOUND"
                OR NOT _arguments_error STREQUAL "NOTFOUND"
                OR NOT _logs_error STREQUAL "NOTFOUND"
                OR NOT _configuration_error STREQUAL "NOTFOUND"
                OR (NOT _install_mode STREQUAL "direct"
                    AND NOT _install_mode STREQUAL "destdir")
                OR NOT _log_count EQUAL 2)
            message(FATAL_ERROR
                "Malformed configure evidence in external package receipt: ${_receipt}"
            )
        endif()
        set(_configuration_identity_input "installMode=${_install_mode}\n")
        set(_arguments "")
        if(_argument_count GREATER 0)
            math(EXPR _argument_last "${_argument_count} - 1")
            foreach(_index RANGE 0 ${_argument_last})
                string(JSON _argument GET
                    "${_json}" configuration arguments ${_index})
                list(APPEND _arguments "${_argument}")
                string(APPEND _configuration_identity_input
                    "argument=${_argument}\n")
            endforeach()
        endif()
        set(_unique_arguments "${_arguments}")
        list(REMOVE_DUPLICATES _unique_arguments)
        list(LENGTH _arguments _arguments_length)
        list(LENGTH _unique_arguments _unique_arguments_length)
        if(NOT _arguments_length EQUAL _unique_arguments_length)
            message(FATAL_ERROR
                "Configure evidence contains duplicate arguments: ${_receipt}"
            )
        endif()
        foreach(_index RANGE 0 1)
            string(JSON _log_kind GET
                "${_json}" configuration logs ${_index} kind)
            string(JSON _log_path GET
                "${_json}" configuration logs ${_index} path)
            string(JSON _expected_log_bytes GET
                "${_json}" configuration logs ${_index} bytes)
            string(JSON _expected_log_sha GET
                "${_json}" configuration logs ${_index} sha256)
            if((_index EQUAL 0 AND NOT _log_kind STREQUAL "stdout")
                    OR (_index EQUAL 1 AND NOT _log_kind STREQUAL "stderr")
                    OR NOT EXISTS "${_log_path}"
                    OR IS_DIRECTORY "${_log_path}"
                    OR IS_SYMLINK "${_log_path}")
                message(FATAL_ERROR
                    "Configure evidence log is missing or malformed: ${_receipt}"
                )
            endif()
            file(SIZE "${_log_path}" _observed_log_bytes)
            file(SHA256 "${_log_path}" _observed_log_sha)
            if(NOT _observed_log_bytes EQUAL _expected_log_bytes
                    OR NOT _observed_log_sha STREQUAL _expected_log_sha)
                message(FATAL_ERROR
                    "Configure evidence log identity mismatch: ${_log_path}"
                )
            endif()
            string(APPEND _configuration_identity_input
                "log=${_log_path}:${_observed_log_bytes}:${_observed_log_sha}\n"
            )
        endforeach()
        string(SHA256 _observed_configuration_sha
            "${_configuration_identity_input}")
        if(NOT _observed_configuration_sha STREQUAL
                _expected_configuration_sha)
            message(FATAL_ERROR
                "Configure evidence aggregate identity mismatch: ${_receipt}"
            )
        endif()
    endif()
endforeach()
list(LENGTH _receipts _receipt_count)
message(STATUS "Arken external package receipt verification passed (${_receipt_count} receipts)")
