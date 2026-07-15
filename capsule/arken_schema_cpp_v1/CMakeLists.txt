set(ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/capsule.manifest.json)
set(ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED ${ARKEN_NATIVE_GENERATED_DIR}/arken_schema_cpp_v1_manifest_embed.h)

arken_embed_text_file(
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE}
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED}
    ARKEN_SCHEMA_CPP_V1_MANIFEST_JSON
)

if(EMSCRIPTEN)
    set(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET "")
elseif(DEFINED ARKEN_NLOHMANN_TARGET AND TARGET ${ARKEN_NLOHMANN_TARGET})
    set(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET ${ARKEN_NLOHMANN_TARGET})
else()
    find_package(nlohmann_json CONFIG REQUIRED)
    set(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
endif()

add_library(arken_schema_cpp_v1_runtime STATIC
    src/binding.cpp
    src/manifest.cpp
    src/wire.cpp
)

target_include_directories(arken_schema_cpp_v1_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${ARKEN_NATIVE_GENERATED_DIR}
        ${EXTERNAL_DIR}
)

if(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET)
    target_link_libraries(arken_schema_cpp_v1_runtime
        PRIVATE ${ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET}
    )
endif()

if(EMSCRIPTEN)
    add_library(arken_schema_cpp_v1 INTERFACE)
    target_link_libraries(arken_schema_cpp_v1 INTERFACE arken_schema_cpp_v1_runtime)
else()
    add_library(arken_schema_cpp_v1 STATIC
        src/generator.cpp
        src/generator_wire_names.cpp
    )
    target_include_directories(arken_schema_cpp_v1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(arken_schema_cpp_v1
        PUBLIC
            arken_schema_cpp_v1_runtime
            arken_schema_idl_v1
        PRIVATE
            ${ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET}
    )
endif()

if(NOT EMSCRIPTEN)
    add_executable(
        arken-schema-cpp-v1-codegen
        tools/generate_cpp_contract.cpp
    )
    target_link_libraries(
        arken-schema-cpp-v1-codegen
        PRIVATE
            arken_schema_cpp_v1
            ${ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET}
    )
endif()

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
            tests/wire_codec_tests.cpp
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

    if(TARGET arken-schema-cpp-v1-codegen)
        add_test(
            NAME ArkenSchemaCppV1.CodegenOverwriteGuard
            COMMAND
                ${CMAKE_COMMAND}
                -DCODEGEN=$<TARGET_FILE:arken-schema-cpp-v1-codegen>
                -DSCHEMA=${CMAKE_CURRENT_LIST_DIR}/../arken_chat_v1/schema/chat_v1.arken-schema
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/codegen-overwrite-guard
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen_overwrite_guard_test.cmake
        )
    endif()
endif()
