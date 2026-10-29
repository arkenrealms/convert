get_filename_component(_arken_digest_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(arken_digest_v1 STATIC
    "${_arken_digest_root}/src/digest.cpp"
    "${_arken_digest_root}/src/hmac.cpp"
    "${_arken_digest_root}/src/source_digest.cpp")
add_library(arken::digest_v1 ALIAS arken_digest_v1)
target_compile_features(arken_digest_v1 PUBLIC cxx_std_20)
target_include_directories(arken_digest_v1 PUBLIC "${_arken_digest_root}/include")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_digest_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror $<$<CONFIG:Debug>:-O2>)
endif()
