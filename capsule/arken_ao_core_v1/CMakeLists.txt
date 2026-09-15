add_library(arken_ao_core_v1 STATIC
    src/core.cpp
    src/directory.cpp
)
add_library(arken::ao_core_v1 ALIAS arken_ao_core_v1)
target_compile_features(arken_ao_core_v1 PUBLIC cxx_std_20)
target_include_directories(arken_ao_core_v1
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_ao_core_v1
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if((BUILD_TESTING OR ARKEN_AO_CORE_V1_BUILD_TESTS) AND NOT EMSCRIPTEN)
    add_executable(arken-ao-core-v1-tests tests/core_tests.cpp)
    target_link_libraries(arken-ao-core-v1-tests PRIVATE arken::ao_core_v1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(arken-ao-core-v1-tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME ArkenAoCoreV1.Host COMMAND arken-ao-core-v1-tests)
    add_test(NAME ArkenAoCoreV1.TerminalVersion COMMAND arken-ao-core-v1-tests --version invalid)
    set_tests_properties(ArkenAoCoreV1.TerminalVersion PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "arken-ao-core-v1-tests-r4227")
endif()
