set(ARKEN_SCHEMA_CORE_V1_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/capsule.manifest.json)
set(ARKEN_SCHEMA_CORE_V1_MANIFEST_EMBED ${ARKEN_NATIVE_GENERATED_DIR}/arken_schema_core_v1_manifest_embed.h)

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
set(ARKEN_NLOHMANN_TARGET nlohmann_json::nlohmann_json)

arken_embed_text_file(
    ${ARKEN_SCHEMA_CORE_V1_MANIFEST_SOURCE}
    ${ARKEN_SCHEMA_CORE_V1_MANIFEST_EMBED}
    ARKEN_SCHEMA_CORE_V1_MANIFEST_JSON
)

add_library(arken_schema_core_v1 STATIC
    src/catalog_view.cpp
    src/manifest.cpp
    src/schema.cpp
    src/schema_parse.cpp
    src/value.cpp
)

target_include_directories(arken_schema_core_v1
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${ARKEN_NATIVE_GENERATED_DIR}
)

target_link_libraries(arken_schema_core_v1
    PUBLIC
        arken_base_dev
        ${ARKEN_NLOHMANN_TARGET}
)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        add_executable(arken-schema-core-v1-tests tests/schema_core_v1_tests.cpp)
        target_include_directories(arken-schema-core-v1-tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/..
        )
        target_link_libraries(arken-schema-core-v1-tests
            PRIVATE
                arken_schema_core_v1
                GTest::gtest
                GTest::gtest_main
        )
        gtest_discover_tests(arken-schema-core-v1-tests DISCOVERY_TIMEOUT 30)
    endif()
endif()
