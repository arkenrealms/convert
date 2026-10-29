include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/LocalIO.cmake")
get_filename_component(_qualification_owner "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(arken_cloudflare_qualification_v1 STATIC
    "${_qualification_owner}/src/qualification_process.cpp"
    "${_qualification_owner}/src/private_file.cpp"
    "${_qualification_owner}/src/private_file_read.cpp")
target_link_libraries(arken_cloudflare_qualification_v1 PUBLIC arken_cloudflare_local_io_v1)
target_compile_features(arken_cloudflare_qualification_v1 PUBLIC cxx_std_20)
target_compile_options(arken_cloudflare_qualification_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
