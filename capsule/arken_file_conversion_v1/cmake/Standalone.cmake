include_guard(GLOBAL)
# Standalone native MCP composition: the bounded image owner plus the standalone audio
# (WAV/FLAC/AIFF), document (DOCX/RTF/PPTX/ODT/ODP/EPUB to TXT/Markdown), table
# (CSV/TSV/JSON/XLSX/ODS to CSV/TSV/JSON) and markup (HTML/Markdown, SRT/WebVTT) owners.
# Selected only by a root composition that also resolves the audio capsule's flac-1.5.0-v1
# recipe; the images component used by other consumers is unchanged.
include("${CMAKE_CURRENT_LIST_DIR}/Contract.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ArkenFileConversionImages.cmake")
arken_add_file_conversion_images()
arken_add_file_conversion_standalone_media()
