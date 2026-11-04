cmake_minimum_required(VERSION 3.25)
# Memory-only DOCX/RTF extraction library. Consumers resolve this capsule's
# external_dependencies (stb-image-2.30-v1 "image") before adding it, exactly as
# for arken_image_codecs_v1; tests/CMakeLists.txt is the standalone qualification.
if(NOT TARGET arken::external_stb_image_v1)
    message(FATAL_ERROR "arken_document_extraction_v1 requires the resolved stb-image-2.30-v1 package (arken::external_stb_image_v1)")
endif()
if(NOT TARGET arken_document_extraction_v1)
    add_library(arken_document_extraction_v1 STATIC
        src/api.cpp src/docx.cpp src/inflate.cpp src/rtf.cpp src/serialize.cpp src/xml.cpp src/zip.cpp)
    add_library(arken::document_extraction_v1 ALIAS arken_document_extraction_v1)
    target_compile_features(arken_document_extraction_v1 PUBLIC cxx_std_20)
    target_include_directories(arken_document_extraction_v1 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(arken_document_extraction_v1 PRIVATE arken::external_stb_image_v1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(arken_document_extraction_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
