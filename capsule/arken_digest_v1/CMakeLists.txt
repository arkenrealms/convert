if(NOT TARGET arken_identity_v1)
    message(FATAL_ERROR "arken_digest_v1 requires arken_identity_v1")
endif()

add_library(arken_digest_v1 STATIC
    src/digest.cpp
)
add_library(arken::digest_v1 ALIAS arken_digest_v1)
target_compile_features(arken_digest_v1 PUBLIC cxx_std_20)
target_include_directories(arken_digest_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(arken_digest_v1 PUBLIC arken_identity_v1)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_digest_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING OR ARKEN_DIGEST_V1_BUILD_TESTS OR OASIS_NATIVE_BUILD_HOST_TESTS)
    add_executable(arken-digest-v1-tests tests/digest_tests.cpp)
    target_link_libraries(arken-digest-v1-tests PRIVATE arken_digest_v1)
    if(NOT MSVC)
        target_compile_options(arken-digest-v1-tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ArkenDigestV1.Core COMMAND arken-digest-v1-tests)
endif()
