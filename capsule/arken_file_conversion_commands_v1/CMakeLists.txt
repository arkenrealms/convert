if(NOT TARGET arken-schema-cpp-v1-codegen)
 message(FATAL_ERROR "Convert commands require the native schema generator")
endif()
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${generated}/generation.stamp"
 BYPRODUCTS "${generated}/arken_schema_generated/conversion_files/v1.hpp" "${generated}/arken_schema_generated/command-json.hpp"
 COMMAND $<TARGET_FILE:arken-schema-cpp-v1-codegen> --entry conversion_files.arken-schema
  --source "conversion_files.arken-schema=${CMAKE_CURRENT_LIST_DIR}/schema/conversion_files.arken-schema"
  --output "${generated}" --command-json --overwrite-existing
 COMMAND ${CMAKE_COMMAND} -E touch "${generated}/generation.stamp"
 DEPENDS arken-schema-cpp-v1-codegen "${CMAKE_CURRENT_LIST_DIR}/schema/conversion_files.arken-schema" VERBATIM)
add_custom_target(arken-conversion-file-schema DEPENDS "${generated}/generation.stamp")
add_library(arken_file_conversion_commands_v1 STATIC src/commands.cpp)
target_compile_features(arken_file_conversion_commands_v1 PUBLIC cxx_std_20)
target_include_directories(arken_file_conversion_commands_v1 PUBLIC "${CMAKE_CURRENT_LIST_DIR}/.." PRIVATE "${generated}")
target_link_libraries(arken_file_conversion_commands_v1 PUBLIC arken_mcp_protocol_v1
 PRIVATE arken_file_conversion_images_v1 arken_file_conversion_audio_v1 arken_file_conversion_documents_v1 arken_file_conversion_markup_v1
 arken_file_workspace_files_v1 arken_digest_v1)
target_compile_options(arken_file_conversion_commands_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_dependencies(arken_file_conversion_commands_v1 arken-conversion-file-schema)
add_executable(arken-convert-commands tools/cli.cpp)
target_link_libraries(arken-convert-commands PRIVATE arken_file_conversion_commands_v1 arken_mcp_stdio_v1)
