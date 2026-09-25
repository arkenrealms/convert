include_guard(GLOBAL)
# Stable browser substrates use the native compiler and an explicitly imported
# browser capability surface. This does not compile authored LiveMachine programs.
function(arken_browser_cpp_reactor target)
    cmake_parse_arguments(ARG "NO_HOST_IO" "MAX_MEMORY_BYTES" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "${target}: unknown or incomplete reactor profile arguments")
    endif()
    if(NOT ARKEN_BROWSER_CPP OR NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-wasip1")
        message(FATAL_ERROR "${target} requires the graph-owned native browser C++ toolchain")
    endif()
    if(NOT DEFINED ARG_MAX_MEMORY_BYTES)
        set(ARG_MAX_MEMORY_BYTES 268435456)
    endif()
    if(NOT ARG_MAX_MEMORY_BYTES MATCHES "^[1-9][0-9]*$"
       OR ARG_MAX_MEMORY_BYTES LESS 2097152 OR ARG_MAX_MEMORY_BYTES GREATER 2147483648)
        message(FATAL_ERROR "${target}: MAX_MEMORY_BYTES must be 2 MiB through 2 GiB")
    endif()
    math(EXPR memory_remainder "${ARG_MAX_MEMORY_BYTES} % 65536")
    if(NOT memory_remainder EQUAL 0)
        message(FATAL_ERROR "${target}: MAX_MEMORY_BYTES must be a whole Wasm page")
    endif()
    set_property(TARGET ${target} PROPERTY ARKEN_BROWSER_CPP_MAX_MEMORY_BYTES "${ARG_MAX_MEMORY_BYTES}")
    if(ARG_NO_HOST_IO)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/wasm/closed_reactor.cpp")
        foreach(call environ_get environ_sizes_get clock_time_get poll_oneoff fd_close fd_seek fd_write
                fd_fdstat_get fd_fdstat_set_flags fd_prestat_get fd_prestat_dir_name fd_read path_open proc_exit)
            target_link_options(${target} PRIVATE "-Wl,--wrap=__wasi_${call}")
        endforeach()
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX .wasm)
    target_link_options(${target} PRIVATE -Wl,--export-dynamic -Wl,--export=malloc
        -Wl,--export=free "-Wl,--max-memory=${ARG_MAX_MEMORY_BYTES}" -Wl,-z,stack-size=1048576)
endfunction()

# Consumers exercise the SDK-owned denial contract without compiling private
# platform sources through a product-specific path.
function(arken_browser_cpp_closed_profile_probe target)
    add_executable(${target}
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/wasm/tests/closed_reactor_probe.cpp")
    arken_browser_cpp_reactor(${target} NO_HOST_IO MAX_MEMORY_BYTES 2097152)
endfunction()
