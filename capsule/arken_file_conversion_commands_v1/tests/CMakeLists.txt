add_executable(arken-convert-command-tests commands.cpp)
target_link_libraries(arken-convert-command-tests PRIVATE arken_file_conversion_commands_v1
 arken_file_conversion_images_v1 arken_file_workspace_files_v1 arken_digest_v1)
add_dependencies(arken-convert-command-tests arken-convert-commands)
target_compile_options(arken-convert-command-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME ConvertFiles.Version COMMAND arken-convert-commands --stdio --root /nonexistent --version)
add_test(NAME ConvertFiles.TestsVersion COMMAND arken-convert-command-tests --invalid --version)
set_tests_properties(ConvertFiles.Version ConvertFiles.TestsVersion PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "r6262")
add_test(NAME ConvertFiles.Stdio COMMAND ${CMAKE_COMMAND}
 "-DSERVER=$<TARGET_FILE:arken-convert-commands>" "-DFIXTURES=$<TARGET_FILE:arken-convert-command-tests>"
 "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stdio-proof" -P "${CMAKE_CURRENT_LIST_DIR}/stdio.cmake")
set_tests_properties(ConvertFiles.Stdio PROPERTIES TIMEOUT 90)
