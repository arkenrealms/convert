include_guard(GLOBAL)

function(_arken_extpkg_error code phase subject detail)
    message(FATAL_ERROR
        "Arken external package error\n"
        "  code: ${code}\n"
        "  phase: ${phase}\n"
        "  subject: ${subject}\n"
        "  detail: ${detail}"
    )
endfunction()

function(_arken_extpkg_json_string_array out_var)
    set(_json "[")
    set(_index 0)
    foreach(_value IN LISTS ARGN)
        _arken_extpkg_json_escape(_escaped_value "${_value}")
        if(_index GREATER 0)
            string(APPEND _json ",")
        endif()
        string(APPEND _json "\"${_escaped_value}\"")
        math(EXPR _index "${_index} + 1")
    endforeach()
    string(APPEND _json "]")
    set(${out_var} "${_json}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_type out_var json_text)
    string(JSON _type ERROR_VARIABLE _error TYPE "${json_text}" ${ARGN})
    if(_error STREQUAL "NOTFOUND")
        set(${out_var} "${_type}" PARENT_SCOPE)
    elseif(_error MATCHES "member '.*' not found")
        set(${out_var} "" PARENT_SCOPE)
    else()
        _arken_extpkg_error(
            "arken.external_packages.recipe.invalid_json"
            "schema"
            "json"
            "${_error}"
        )
    endif()
endfunction()

function(_arken_extpkg_json_required_string out_var json_text subject)
    set(_path ${ARGN})
    string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${_path})
    if(NOT _error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.required_string_missing"
            "schema"
            "${subject}"
            "Required string '${_path}' is missing or invalid: ${_error}"
        )
    endif()
    string(JSON _type TYPE "${json_text}" ${_path})
    if(NOT _type STREQUAL "STRING" OR _value STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.recipe.required_string_invalid"
            "schema"
            "${subject}"
            "Required field '${_path}' must be a non-empty string."
        )
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_optional_string out_var json_text default_value subject)
    set(_path ${ARGN})
    string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${_path})
    if(_error MATCHES "member '.*' not found")
        set(${out_var} "${default_value}" PARENT_SCOPE)
        return()
    endif()
    if(NOT _error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_string_invalid"
            "schema"
            "${subject}"
            "Optional string '${_path}' is invalid: ${_error}"
        )
    endif()
    string(JSON _type TYPE "${json_text}" ${_path})
    if(NOT _type STREQUAL "STRING")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_string_invalid"
            "schema"
            "${subject}"
            "Optional field '${_path}' must be a string."
        )
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_optional_bool out_var json_text default_value subject)
    set(_path ${ARGN})
    string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${_path})
    if(_error MATCHES "member '.*' not found")
        set(${out_var} "${default_value}" PARENT_SCOPE)
        return()
    endif()
    if(NOT _error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_bool_invalid"
            "schema"
            "${subject}"
            "Optional boolean '${_path}' is invalid: ${_error}"
        )
    endif()
    string(JSON _type TYPE "${json_text}" ${_path})
    if(NOT _type STREQUAL "BOOLEAN")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_bool_invalid"
            "schema"
            "${subject}"
            "Optional field '${_path}' must be a boolean."
        )
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_optional_integer out_var json_text default_value subject)
    set(_path ${ARGN})
    string(JSON _value ERROR_VARIABLE _error GET "${json_text}" ${_path})
    if(_error MATCHES "member '.*' not found")
        set(${out_var} "${default_value}" PARENT_SCOPE)
        return()
    endif()
    if(NOT _error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_integer_invalid"
            "schema"
            "${subject}"
            "Optional integer '${_path}' is invalid: ${_error}"
        )
    endif()
    string(JSON _type TYPE "${json_text}" ${_path})
    if(NOT _type STREQUAL "NUMBER" OR NOT _value MATCHES "^[0-9]+$")
        _arken_extpkg_error(
            "arken.external_packages.recipe.optional_integer_invalid"
            "schema"
            "${subject}"
            "Optional field '${_path}' must be a non-negative integer."
        )
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_array_to_list out_var json_text subject)
    set(_path ${ARGN})
    string(JSON _length ERROR_VARIABLE _error LENGTH "${json_text}" ${_path})
    if(_error MATCHES "member '.*' not found")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    if(NOT _error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.array_invalid"
            "schema"
            "${subject}"
            "Array '${_path}' is invalid: ${_error}"
        )
    endif()
    set(_values "")
    if(_length GREATER 0)
        math(EXPR _last "${_length} - 1")
        foreach(_index RANGE 0 ${_last})
            string(JSON _type TYPE "${json_text}" ${_path} ${_index})
            if(NOT _type STREQUAL "STRING")
                _arken_extpkg_error(
                    "arken.external_packages.recipe.array_value_invalid"
                    "schema"
                    "${subject}"
                    "Array '${_path}' accepts string values only."
                )
            endif()
            string(JSON _value GET "${json_text}" ${_path} ${_index})
            if(_value STREQUAL "")
                _arken_extpkg_error(
                    "arken.external_packages.recipe.array_value_invalid"
                    "schema"
                    "${subject}"
                    "Array '${_path}' contains an empty value."
                )
            endif()
            list(APPEND _values "${_value}")
        endforeach()
    endif()
    set(${out_var} "${_values}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_require_unique_list values subject field_name)
    set(_values ${values})
    list(LENGTH _values _before)
    list(REMOVE_DUPLICATES _values)
    list(LENGTH _values _after)
    if(NOT _before EQUAL _after)
        _arken_extpkg_error(
            "arken.external_packages.recipe.duplicate_value"
            "schema"
            "${subject}"
            "Field '${field_name}' contains duplicate values."
        )
    endif()
endfunction()

function(_arken_extpkg_validate_object_keys json_text subject allowed_keys)
    set(_path ${ARGN})
    string(JSON _type ERROR_VARIABLE _type_error TYPE "${json_text}" ${_path})
    if(NOT _type_error STREQUAL "NOTFOUND")
        _arken_extpkg_error(
            "arken.external_packages.recipe.object_missing"
            "schema"
            "${subject}"
            "Expected object '${_path}': ${_type_error}"
        )
    endif()
    if(NOT _type STREQUAL "OBJECT")
        _arken_extpkg_error(
            "arken.external_packages.recipe.object_invalid"
            "schema"
            "${subject}"
            "Expected '${_path}' to be an object."
        )
    endif()
    string(JSON _length LENGTH "${json_text}" ${_path})
    if(_length GREATER 0)
        math(EXPR _last "${_length} - 1")
        foreach(_index RANGE 0 ${_last})
            string(JSON _key MEMBER "${json_text}" ${_path} ${_index})
            list(FIND allowed_keys "${_key}" _allowed_index)
            if(_allowed_index EQUAL -1)
                _arken_extpkg_error(
                    "arken.external_packages.recipe.unknown_field"
                    "schema"
                    "${subject}"
                    "Unknown field '${_key}' in '${_path}'. Recipes are declarative and do not accept arbitrary commands or options."
                )
            endif()
        endforeach()
    endif()
endfunction()

function(_arken_extpkg_json_escape out_var value)
    set(_escaped "${value}")
    string(REPLACE "\\" "\\\\" _escaped "${_escaped}")
    string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
    string(REPLACE "\n" "\\n" _escaped "${_escaped}")
    string(REPLACE "\r" "\\r" _escaped "${_escaped}")
    string(REPLACE "\t" "\\t" _escaped "${_escaped}")
    set(${out_var} "${_escaped}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_json_string_array out_var values)
    set(_json "")
    set(_index 0)
    foreach(_value IN LISTS values)
        _arken_extpkg_json_escape(_escaped "${_value}")
        if(_index GREATER 0)
            string(APPEND _json ",")
        endif()
        string(APPEND _json "\"${_escaped}\"")
        math(EXPR _index "${_index} + 1")
    endforeach()
    set(${out_var} "[${_json}]" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_write_if_different path content)
    get_filename_component(_directory "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    if(EXISTS "${path}")
        file(READ "${path}" _existing)
        if("${_existing}" STREQUAL "${content}")
            return()
        endif()
    endif()
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
    set(_temporary "${path}.tmp-${_nonce}")
    file(WRITE "${_temporary}" "${content}")
    file(RENAME "${_temporary}" "${path}")
endfunction()

function(_arken_extpkg_add_configure_dependency path)
    if(CMAKE_SCRIPT_MODE_FILE)
        return()
    endif()
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${path}"
    )
endfunction()

function(_arken_extpkg_identifier out_var value)
    string(MAKE_C_IDENTIFIER "${value}" _identifier)
    string(TOLOWER "${_identifier}" _identifier)
    set(${out_var} "${_identifier}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_current_platform out_var)
    if(EMSCRIPTEN OR ARKEN_BROWSER_CPP)
        set(_platform "web")
    elseif(ANDROID)
        set(_platform "android")
    elseif(IOS)
        set(_platform "ios")
    elseif(APPLE)
        set(_platform "macos")
    elseif(WIN32)
        set(_platform "windows")
    elseif(UNIX)
        set(_platform "linux")
    else()
        set(_platform "unknown")
    endif()
    set(${out_var} "${_platform}" PARENT_SCOPE)
endfunction()
