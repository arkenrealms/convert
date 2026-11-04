include_guard(GLOBAL)
function(arken_add_rooted_mcp_proof name server fixtures)
 configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/rooted_stdio.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/${name}-stdio.cmake" COPYONLY)
 add_test(NAME ${name}.Stdio COMMAND ${CMAKE_COMMAND}
  "-DSERVER=$<TARGET_FILE:${server}>" "-DFIXTURES=$<TARGET_FILE:${fixtures}>"
  "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stdio-proof" -P "${CMAKE_CURRENT_BINARY_DIR}/${name}-stdio.cmake")
 set_tests_properties(${name}.Stdio PROPERTIES TIMEOUT 90)
endfunction()
