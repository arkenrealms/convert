if(NOT COMMAND arken_embed_text_file)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/arken_native_defaults.cmake)
endif()
if(NOT DEFINED ARKEN_NATIVE_GENERATED_DIR OR ARKEN_NATIVE_GENERATED_DIR STREQUAL "")
    set(ARKEN_NATIVE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()
file(MAKE_DIRECTORY ${ARKEN_NATIVE_GENERATED_DIR})

option(
    ARKEN_SCHEMA_CPP_V1_RUNTIME_ONLY
    "Build only the generated-contract runtime without the schema generator."
    OFF
)

set(ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/capsule.manifest.json)
set(ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED ${ARKEN_NATIVE_GENERATED_DIR}/arken_schema_cpp_v1_manifest_embed.h)

arken_embed_text_file(
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_SOURCE}
    ${ARKEN_SCHEMA_CPP_V1_MANIFEST_EMBED}
    ARKEN_SCHEMA_CPP_V1_MANIFEST_JSON
)

if(DEFINED ARKEN_NLOHMANN_TARGET AND TARGET ${ARKEN_NLOHMANN_TARGET})
    set(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET ${ARKEN_NLOHMANN_TARGET})
else()
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
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
)
target_compile_features(arken_schema_cpp_v1_runtime PUBLIC cxx_std_20)
target_link_libraries(arken_schema_cpp_v1_runtime PUBLIC arken_schema_core_v1)
add_library(arken_schema_cpp_v1_tool_json INTERFACE)
target_link_libraries(arken_schema_cpp_v1_tool_json INTERFACE
    arken_schema_cpp_v1_runtime ${ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET})

if(ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET)
    target_link_libraries(arken_schema_cpp_v1_runtime
        PRIVATE ${ARKEN_SCHEMA_CPP_V1_NLOHMANN_TARGET}
    )
endif()

if(EMSCRIPTEN OR ARKEN_SCHEMA_CPP_V1_RUNTIME_ONLY)
    add_library(arken_schema_cpp_v1 INTERFACE)
    target_link_libraries(arken_schema_cpp_v1 INTERFACE arken_schema_cpp_v1_runtime)
else()
    add_library(arken_schema_cpp_v1 STATIC
        src/command_catalog.cpp
        src/command_json_codegen.cpp
        src/generator.cpp
        src/generator_registry.cpp
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

if(NOT EMSCRIPTEN AND NOT ARKEN_SCHEMA_CPP_V1_RUNTIME_ONLY)
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

if(BUILD_TESTING AND TARGET arken-schema-cpp-v1-codegen)
    add_test(NAME ArkenSchemaCppV1.CodegenVersion COMMAND arken-schema-cpp-v1-codegen --version --invalid)
    set_tests_properties(ArkenSchemaCppV1.CodegenVersion PROPERTIES TIMEOUT 5
        PASS_REGULAR_EXPRESSION "^arken-schema-cpp-v1-codegen r5768")
    add_test(NAME ArkenSchemaCppV1.CodegenTerminalVersion COMMAND arken-schema-cpp-v1-codegen --invalid --version)
    set_tests_properties(ArkenSchemaCppV1.CodegenTerminalVersion PROPERTIES TIMEOUT 5
        PASS_REGULAR_EXPRESSION "^arken-schema-cpp-v1-codegen r5768")
    add_test(NAME ArkenSchemaCppV1.LargeCommandCatalog COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:arken-schema-cpp-v1-codegen>" "-DCXX=${CMAKE_CXX_COMPILER}"
        "-DRUNTIME=$<TARGET_FILE:arken_schema_cpp_v1_runtime>" "-DCORE=$<TARGET_FILE:arken_schema_core_v1>"
        "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../.." "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/large-catalog-proof"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/large_command_catalog.cmake")
    add_test(NAME ArkenSchemaCppV1.CompactCommandCatalog COMMAND "${CMAKE_COMMAND}"
        "-DTOOL=$<TARGET_FILE:arken-schema-cpp-v1-codegen>" "-DCXX=${CMAKE_CXX_COMPILER}"
        "-DRUNTIME=$<TARGET_FILE:arken_schema_cpp_v1_runtime>" "-DCORE=$<TARGET_FILE:arken_schema_core_v1>"
        "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../.." "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/compact-catalog-proof"
        -DCOMPACT_SOURCE=ON -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/large_command_catalog.cmake")
endif()

if(BUILD_TESTING AND NOT ARKEN_SCHEMA_CPP_V1_RUNTIME_ONLY)
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
                -DSCHEMA=${CMAKE_CURRENT_LIST_DIR}/../arken_schema_compiler_v1/tests/fixtures/account.arken-schema
                -DCOMMON_SCHEMA=${CMAKE_CURRENT_LIST_DIR}/../arken_schema_compiler_v1/tests/fixtures/common.arken-schema
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/codegen-overwrite-guard
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen_overwrite_guard_test.cmake
        )
    endif()
endif()
