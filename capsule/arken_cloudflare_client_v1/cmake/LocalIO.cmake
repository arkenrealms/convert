include_guard(GLOBAL)
get_filename_component(_cloudflare_io_owner "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(arken_cloudflare_local_io_v1 STATIC
    "${_cloudflare_io_owner}/src/local_io.cpp"
    "${_cloudflare_io_owner}/src/require.cpp"
    "${_cloudflare_io_owner}/src/hash.cpp")
target_compile_features(arken_cloudflare_local_io_v1 PUBLIC cxx_std_20)
target_include_directories(arken_cloudflare_local_io_v1 PUBLIC "${_cloudflare_io_owner}/..")
target_link_libraries(arken_cloudflare_local_io_v1 PUBLIC arken_digest_v1 arken_nlohmann_json_header_only)
target_compile_options(arken_cloudflare_local_io_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
