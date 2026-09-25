if(NOT TARGET arken_identity_v1)
    message(FATAL_ERROR "arken_ao_schema_v1 requires arken_identity_v1")
endif()

add_library(arken_ao_schema_v1 STATIC
    src/registry.cpp
)
add_library(arken::ao_schema_v1 ALIAS arken_ao_schema_v1)
target_compile_features(arken_ao_schema_v1 PUBLIC cxx_std_20)
target_include_directories(arken_ao_schema_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(arken_ao_schema_v1 PUBLIC arken_identity_v1)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_ao_schema_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING OR ARKEN_AO_SCHEMA_V1_BUILD_TESTS OR OASIS_NATIVE_BUILD_HOST_TESTS)
    add_executable(arken-ao-schema-v1-tests tests/registry_tests.cpp)
    target_link_libraries(arken-ao-schema-v1-tests PRIVATE arken_ao_schema_v1)
    if(NOT MSVC)
        target_compile_options(arken-ao-schema-v1-tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ArkenAoSchemaV1.Registry COMMAND arken-ao-schema-v1-tests)
    add_test(NAME ArkenAoSchemaV1.RegistryVersion COMMAND arken-ao-schema-v1-tests --version --invalid)
    set_tests_properties(ArkenAoSchemaV1.RegistryVersion PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "arken-ao-schema-v1-tests-r4572")
endif()

if(EMSCRIPTEN AND COMMAND arken_register_browser_live_source_contribution)
    arken_register_browser_live_source_contribution(
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/browser-live-source-contribution.json"
    )
endif()
