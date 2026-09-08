include_guard(GLOBAL)
# Stable browser substrates use the native compiler and an explicitly imported
# browser capability surface. This does not compile authored LiveMachine programs.
function(arken_browser_cpp_reactor target)
    if(NOT ARKEN_BROWSER_CPP OR NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-wasip1")
        message(FATAL_ERROR "${target} requires the graph-owned native browser C++ toolchain")
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX .wasm)
    target_link_options(${target} PRIVATE -Wl,--export-dynamic -Wl,--export=malloc
        -Wl,--export=free -Wl,--max-memory=268435456 -Wl,-z,stack-size=1048576)
endfunction()
