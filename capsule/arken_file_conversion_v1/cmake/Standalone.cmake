include_guard(GLOBAL)
# Standalone native MCP composition: the bounded image owner plus the standalone audio
# (WAV/FLAC) and document (DOCX/RTF to TXT/Markdown) owners. Selected only by a root
# composition that also resolves the audio capsule's flac-1.5.0-v1 recipe; the images
# component used by other consumers is unchanged.
include("${CMAKE_CURRENT_LIST_DIR}/Contract.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ArkenFileConversionImages.cmake")
arken_add_file_conversion_images()
arken_add_file_conversion_standalone_media()
