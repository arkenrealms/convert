add_library(arken_identity_v1 STATIC
    src/c_api.cpp
    src/identity.cpp
)
target_include_directories(
    arken_identity_v1
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(arken_identity_v1 PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_identity_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING OR ARKEN_IDENTITY_V1_BUILD_TESTS OR OASIS_NATIVE_BUILD_HOST_TESTS)
    add_executable(
        arken-identity-v1-tests
        tests/identity_host_tests.cpp
    )
    target_link_libraries(arken-identity-v1-tests PRIVATE arken_identity_v1)
    add_test(
        NAME arken-identity-v1-tests
        COMMAND arken-identity-v1-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/core-vectors.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/byte-order-vectors.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/uuid-v7-vectors.tsv
    )

    if(CMAKE_C_COMPILER_LOADED)
        add_executable(
            arken-identity-v1-c-header-tests
            tests/c_header_compile_test.c
        )
        target_include_directories(
            arken-identity-v1-c-header-tests
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(
            NAME arken-identity-v1-c-header-tests
            COMMAND arken-identity-v1-c-header-tests
        )
    endif()

    add_executable(
        arken-identity-v1-benchmark
        benchmarks/identity_benchmarks.cpp
    )
    target_link_libraries(arken-identity-v1-benchmark PRIVATE arken_identity_v1)
    add_test(
        NAME arken-identity-v1-benchmark
        COMMAND arken-identity-v1-benchmark 100000
    )
endif()
