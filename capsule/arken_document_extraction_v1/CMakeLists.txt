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
    # Tabular conversion (tabular.hpp): CSV/TSV/JSON core, XLSX and ODS readers.
    target_sources(arken_document_extraction_v1 PRIVATE src/tabular.cpp src/xlsx.cpp src/ods.cpp)
    add_library(arken::document_extraction_v1 ALIAS arken_document_extraction_v1)
    target_compile_features(arken_document_extraction_v1 PUBLIC cxx_std_20)
    target_include_directories(arken_document_extraction_v1 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(arken_document_extraction_v1 PRIVATE arken::external_stb_image_v1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(arken_document_extraction_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
# Presentations, OpenDocument and EPUB (text_documents.hpp): PPTX, ODT, ODP and
# EPUB extractors feeding the same model and serializers. Added once per build.
get_property(arken_document_text_documents_added GLOBAL PROPERTY ARKEN_DOCUMENT_EXTRACTION_TEXT_DOCUMENTS)
if(NOT arken_document_text_documents_added)
    set_property(GLOBAL PROPERTY ARKEN_DOCUMENT_EXTRACTION_TEXT_DOCUMENTS TRUE)
    target_sources(arken_document_extraction_v1 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/text_documents.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/pptx.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/odf_text.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/epub.cpp")
endif()
