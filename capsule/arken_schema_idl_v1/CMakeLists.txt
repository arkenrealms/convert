set(ARKEN_SCHEMA_IDL_V1_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/capsule.manifest.json)
set(ARKEN_SCHEMA_IDL_V1_MANIFEST_EMBED ${ARKEN_NATIVE_GENERATED_DIR}/arken_schema_idl_v1_manifest_embed.h)

arken_embed_text_file(
    ${ARKEN_SCHEMA_IDL_V1_MANIFEST_SOURCE}
    ${ARKEN_SCHEMA_IDL_V1_MANIFEST_EMBED}
    ARKEN_SCHEMA_IDL_V1_MANIFEST_JSON
)

if(EMSCRIPTEN)
    add_library(arken_schema_idl_v1 INTERFACE)
    return()
endif()

if(DEFINED ARKEN_NLOHMANN_TARGET AND TARGET ${ARKEN_NLOHMANN_TARGET})
    set(ARKEN_SCHEMA_IDL_V1_NLOHMANN_TARGET ${ARKEN_NLOHMANN_TARGET})
else()
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
    set(ARKEN_SCHEMA_IDL_V1_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
endif()

add_library(arken_schema_idl_v1 STATIC
    src/allocation.cpp
    src/allocation_manifest.cpp
    src/compatibility.cpp
    src/formatter.cpp
    src/json_contract.cpp
    src/lexer.cpp
    src/manifest.cpp
    src/parser.cpp
    src/semantic.cpp
    src/semantic_manifest.cpp
    src/semantic_token_parsing.cpp
    src/sha256.cpp
    src/source_graph.cpp
    src/target_support.cpp
)
target_compile_features(arken_schema_idl_v1 PUBLIC cxx_std_20)

target_include_directories(arken_schema_idl_v1
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ARKEN_NATIVE_GENERATED_DIR}
)

target_link_libraries(arken_schema_idl_v1 PRIVATE ${ARKEN_SCHEMA_IDL_V1_NLOHMANN_TARGET})

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        add_executable(arken-schema-idl-v1-tests tests/schema_idl_v1_tests.cpp)
        target_sources(arken-schema-idl-v1-tests PRIVATE
            tests/allocation_ledger_tests.cpp
            tests/compatibility_tests.cpp
            tests/catalog_identity_tests.cpp
            tests/enum_target_profile_tests.cpp
            tests/field_cardinality_tests.cpp
            tests/governance_tests.cpp
            tests/policy_contract_tests.cpp
            tests/operation_declaration_tests.cpp
            tests/semantic_ir_tests.cpp
            tests/semantic_manifest_tests.cpp
            tests/sha256_tests.cpp
            tests/source_graph_tests.cpp
            tests/source_recovery_tests.cpp
            tests/structure_tests.cpp
            tests/schema_v2_tests.cpp
            tests/type_declaration_tests.cpp
        )
        target_include_directories(arken-schema-idl-v1-tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/..
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_link_libraries(arken-schema-idl-v1-tests
            PRIVATE
                arken_schema_idl_v1
                ${ARKEN_SCHEMA_IDL_V1_NLOHMANN_TARGET}
                GTest::gtest
                GTest::gtest_main
        )
        gtest_discover_tests(arken-schema-idl-v1-tests DISCOVERY_TIMEOUT 30)
    endif()
endif()
