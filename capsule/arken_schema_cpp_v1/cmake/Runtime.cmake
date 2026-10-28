# Reuse the normal owner target definitions with only the generated-contract runtime.
set(ARKEN_SCHEMA_CPP_V1_RUNTIME_ONLY ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/schema-runtime")
