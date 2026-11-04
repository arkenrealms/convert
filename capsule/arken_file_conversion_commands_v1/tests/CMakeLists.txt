add_executable(arken-convert-command-tests commands.cpp)
target_link_libraries(arken-convert-command-tests PRIVATE arken_file_conversion_commands_v1
 arken_file_conversion_images_v1 arken_file_conversion_audio_v1 arken_file_conversion_documents_v1
 arken_audio_codecs_v1 arken_document_extraction_v1 arken_file_workspace_files_v1 arken_digest_v1)
add_dependencies(arken-convert-command-tests arken-convert-commands)
target_compile_options(arken-convert-command-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME ConvertFiles.Version COMMAND arken-convert-commands --stdio --root /nonexistent --version)
add_test(NAME ConvertFiles.TestsVersion COMMAND arken-convert-command-tests --invalid --version)
set_tests_properties(ConvertFiles.Version ConvertFiles.TestsVersion PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "r6262")
include("${CMAKE_CURRENT_LIST_DIR}/../../arken_mcp_protocol_v1/cmake/RootedProof.cmake")
arken_add_rooted_mcp_proof(ConvertFiles arken-convert-commands arken-convert-command-tests)
