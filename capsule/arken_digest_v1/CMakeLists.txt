if(NOT TARGET arken_ao_schema_v1)
    message(FATAL_ERROR "arken_digest_v1 requires arken_ao_schema_v1")
endif()

add_library(arken_digest_v1 STATIC
    src/digest.cpp
    src/hmac.cpp
)
add_library(arken::digest_v1 ALIAS arken_digest_v1)
target_compile_features(arken_digest_v1 PUBLIC cxx_std_20)
target_include_directories(arken_digest_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(arken_digest_v1 PUBLIC arken_ao_schema_v1)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(arken_digest_v1 PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    $<$<CONFIG:Debug>:-O2>)
endif()

option(
    ARKEN_DIGEST_V1_BUILD_TESTS
    "Build focused Arken Digest v1 tests"
    OFF
)

if(BUILD_TESTING OR ARKEN_DIGEST_V1_BUILD_TESTS OR OASIS_NATIVE_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(arken-digest-v1-tests tests/digest_tests.cpp)
    target_link_libraries(arken-digest-v1-tests PRIVATE arken_digest_v1)
    if(NOT MSVC)
        target_compile_options(arken-digest-v1-tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ArkenDigestV1.Core COMMAND arken-digest-v1-tests)
    add_test(NAME ArkenDigestV1.Version COMMAND arken-digest-v1-tests --version --invalid)
    set_tests_properties(ArkenDigestV1.Version PROPERTIES
        TIMEOUT 5 PASS_REGULAR_EXPRESSION "arken-digest-v1-tests-r5373")
    find_program(ARKEN_DIGEST_V1_NODE_EXECUTABLE NAMES node)
    if(ARKEN_DIGEST_V1_NODE_EXECUTABLE)
        add_test(
            NAME arken-digest-v1-browser-sha256-tests
            COMMAND ${ARKEN_DIGEST_V1_NODE_EXECUTABLE}
                --test
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sha256_tests.mjs
        )
        set_tests_properties(
            arken-digest-v1-browser-sha256-tests
            PROPERTIES
                LABELS "digest;sha256;browser;unit-test"
                TIMEOUT 30
        )
    endif()
endif()

if(EMSCRIPTEN AND COMMAND arken_register_browser_live_source_contribution)
    arken_register_browser_live_source_contribution(
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/browser-live-source-contribution.json"
    )
endif()
