function(arken_embed_text_file input_path output_path variable_name)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input_path})
    file(READ ${input_path} _arken_embed_contents)
    string(REPLACE "\\" "\\\\" _arken_embed_contents "${_arken_embed_contents}")
    string(REPLACE "\"" "\\\"" _arken_embed_contents "${_arken_embed_contents}")
    string(REPLACE "\n" "\\n\"\n\"" _arken_embed_contents "${_arken_embed_contents}")
    file(WRITE ${output_path} "#pragma once\nstatic const char ${variable_name}[] =\n\"${_arken_embed_contents}\";\n")
endfunction()

function(arken_join_emscripten_exports exports_list output_var)
    set(_joined "[")
    foreach(_export ${exports_list})
        if(NOT _joined STREQUAL "[")
            string(APPEND _joined ",")
        endif()
        string(APPEND _joined "\"${_export}\"")
    endforeach()
    string(APPEND _joined "]")
    set(${output_var} ${_joined} PARENT_SCOPE)
endfunction()

function(arken_add_wasm_capsule)
    set(options)
    set(oneValueArgs
        TARGET
        CAPSULE_ID
        OUTPUT_DIR
        MANIFEST
    )
    set(multiValueArgs
        SOURCES
        INCLUDE_DIRS
        LINK_LIBRARIES
        EXPORTED_FUNCTIONS
        EXPORTED_RUNTIME_METHODS
        COMPILE_OPTIONS
        LINK_OPTIONS
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT EMSCRIPTEN)
        return()
    endif()
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "arken_add_wasm_capsule requires TARGET")
    endif()
    if(NOT ARG_CAPSULE_ID)
        message(FATAL_ERROR "arken_add_wasm_capsule requires CAPSULE_ID")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "arken_add_wasm_capsule requires OUTPUT_DIR")
    endif()
    if(NOT ARG_MANIFEST)
        message(FATAL_ERROR "arken_add_wasm_capsule requires MANIFEST")
    endif()
    if(NOT ARG_SOURCES AND NOT ARG_LINK_LIBRARIES)
        message(FATAL_ERROR "arken_add_wasm_capsule requires SOURCES or LINK_LIBRARIES")
    endif()
    if(NOT CMAKE_CXX_COMPILER)
        message(FATAL_ERROR "A C++ compiler is required to build Arken Wasm capsule artifacts.")
    endif()

    set(_arken_wasm_js "${ARG_OUTPUT_DIR}/${ARG_CAPSULE_ID}.mjs")
    set(_arken_wasm_bin "${ARG_OUTPUT_DIR}/${ARG_CAPSULE_ID}.wasm")
    set(_arken_wasm_manifest "${ARG_OUTPUT_DIR}/capsule.manifest.json")
    set(_arken_exports ${ARG_EXPORTED_FUNCTIONS})
    if(NOT _arken_exports)
        set(_arken_exports
            _arken_abi_version
            _arken_manifest_json
            _arken_alloc
            _arken_free
            _arken_last_error_message
        )
    endif()
    arken_join_emscripten_exports("${_arken_exports}" _arken_exports_json)

    set(_arken_runtime_methods ${ARG_EXPORTED_RUNTIME_METHODS})
    if(NOT _arken_runtime_methods)
        set(_arken_runtime_methods ccall)
    endif()
    set(_arken_runtime_methods_json "[")
    foreach(_arken_runtime_method IN LISTS _arken_runtime_methods)
        if(NOT _arken_runtime_methods_json STREQUAL "[")
            string(APPEND _arken_runtime_methods_json ",")
        endif()
        string(APPEND _arken_runtime_methods_json "\"${_arken_runtime_method}\"")
    endforeach()
    string(APPEND _arken_runtime_methods_json "]")

    set(_arken_include_flags)
    foreach(_arken_include_dir IN LISTS ARG_INCLUDE_DIRS)
        list(APPEND _arken_include_flags "-I${_arken_include_dir}")
    endforeach()

    set(_arken_link_flags)
    foreach(_arken_link_lib IN LISTS ARG_LINK_LIBRARIES)
        if(TARGET ${_arken_link_lib})
            list(APPEND _arken_link_flags "$<TARGET_FILE:${_arken_link_lib}>")
        else()
            list(APPEND _arken_link_flags "${_arken_link_lib}")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT
            "${_arken_wasm_js}"
            "${_arken_wasm_bin}"
            "${_arken_wasm_manifest}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
        COMMAND ${CMAKE_CXX_COMPILER}
            -std=c++20
            -O3
            ${ARG_COMPILE_OPTIONS}
            ${_arken_include_flags}
            ${ARG_SOURCES}
            ${_arken_link_flags}
            -o "${_arken_wasm_js}"
            -s MODULARIZE=1
            -s EXPORT_ES6=1
            -s EXPORT_ALL=1
            -s ENVIRONMENT=web,worker,node
            -s ALLOW_MEMORY_GROWTH=1
            -s FILESYSTEM=0
            -s DISABLE_EXCEPTION_CATCHING=0
            -s EXPORTED_FUNCTIONS=${_arken_exports_json}
            -s EXPORTED_RUNTIME_METHODS=${_arken_runtime_methods_json}
            ${ARG_LINK_OPTIONS}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARG_MANIFEST}"
            "${_arken_wasm_manifest}"
        DEPENDS
            ${ARG_SOURCES}
            ${ARG_MANIFEST}
            ${ARG_LINK_LIBRARIES}
        COMMENT "Building Wasm capsule ${ARG_CAPSULE_ID}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    add_custom_target(${ARG_TARGET}
        DEPENDS
            "${_arken_wasm_js}"
            "${_arken_wasm_bin}"
            "${_arken_wasm_manifest}"
    )
    set(${ARG_TARGET}_JS "${_arken_wasm_js}" PARENT_SCOPE)
    set(${ARG_TARGET}_WASM "${_arken_wasm_bin}" PARENT_SCOPE)
    set(${ARG_TARGET}_MANIFEST "${_arken_wasm_manifest}" PARENT_SCOPE)
endfunction()
