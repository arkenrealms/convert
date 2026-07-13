set(ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/capsule.manifest.json)
set(ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED ${ARKEN_NATIVE_GENERATED_DIR}/arken_schema_cpp_v1_manifest_embed.h)

arken_embed_text_file(
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE}
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED}
    ARKEN_SCHEMA_CPP_V1_MANIFEST_JSON
)

add_library(arken_schema_cpp_v1 STATIC
    src/generator.cpp
    src/manifest.cpp
)

target_include_directories(arken_schema_cpp_v1
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${ARKEN_NATIVE_GENERATED_DIR}
)

target_link_libraries(arken_schema_cpp_v1 PUBLIC arken_schema_idl_v1)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        add_executable(arken-schema-cpp-v1-tests tests/generator_tests.cpp)
        target_include_directories(arken-schema-cpp-v1-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(arken-schema-cpp-v1-tests
            PRIVATE
                arken_schema_cpp_v1
                GTest::gtest
                GTest::gtest_main
        )
        gtest_discover_tests(arken-schema-cpp-v1-tests DISCOVERY_TIMEOUT 30)

        add_executable(arken-schema-cpp-v1-fixture-emitter tests/cpp_fixture_emitter.cpp)
        target_include_directories(
            arken-schema-cpp-v1-fixture-emitter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_link_libraries(arken-schema-cpp-v1-fixture-emitter PRIVATE arken_schema_cpp_v1)

        set(ARKEN_SCHEMA_CPP_V1_GENERATED_TEST_ROOT ${CMAKE_CURRENT_BINARY_DIR}/generated)
        set(
            ARKEN_SCHEMA_CPP_V1_GENERATED_ACCOUNT_HEADER
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_TEST_ROOT}/arken_schema_generated/account_contract/v1.hpp
        )
        set(
            ARKEN_SCHEMA_CPP_V1_GENERATED_PROFILE_HEADER
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_TEST_ROOT}/arken_schema_generated/profile_contract/v1.hpp
        )
        add_custom_command(
            OUTPUT
                ${ARKEN_SCHEMA_CPP_V1_GENERATED_ACCOUNT_HEADER}
                ${ARKEN_SCHEMA_CPP_V1_GENERATED_PROFILE_HEADER}
            COMMAND
                $<TARGET_FILE:arken-schema-cpp-v1-fixture-emitter>
                ${ARKEN_SCHEMA_CPP_V1_GENERATED_TEST_ROOT}
            DEPENDS
                arken-schema-cpp-v1-fixture-emitter
                tests/cpp_fixture.hpp
            VERBATIM
        )
        set_source_files_properties(
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_ACCOUNT_HEADER}
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_PROFILE_HEADER}
            PROPERTIES GENERATED TRUE
        )

        add_executable(
            arken-schema-cpp-v1-generated-tests
            tests/generated_api_tests.cpp
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_ACCOUNT_HEADER}
            ${ARKEN_SCHEMA_CPP_V1_GENERATED_PROFILE_HEADER}
        )
        target_include_directories(
            arken-schema-cpp-v1-generated-tests PRIVATE ${ARKEN_SCHEMA_CPP_V1_GENERATED_TEST_ROOT}
        )
        target_link_libraries(arken-schema-cpp-v1-generated-tests
            PRIVATE
                arken_schema_cpp_v1
                GTest::gtest
                GTest::gtest_main
        )
        gtest_discover_tests(arken-schema-cpp-v1-generated-tests DISCOVERY_TIMEOUT 30)
    endif()
endif()
