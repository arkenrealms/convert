cmake_minimum_required(VERSION 3.25)
# Memory-only HTML/Markdown/subtitle conversion library. No external
# dependencies; tests/CMakeLists.txt is the standalone qualification.
if(NOT TARGET arken_markup_conversion_v1)
    add_library(arken_markup_conversion_v1 STATIC
        src/api.cpp src/entities.cpp src/html_tree.cpp src/html_to_markdown.cpp
        src/markdown_to_html.cpp src/subtitles.cpp)
    add_library(arken::markup_conversion_v1 ALIAS arken_markup_conversion_v1)
    target_compile_features(arken_markup_conversion_v1 PUBLIC cxx_std_20)
    target_include_directories(arken_markup_conversion_v1 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(arken_markup_conversion_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
