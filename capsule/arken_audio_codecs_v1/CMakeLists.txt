cmake_minimum_required(VERSION 3.25)
# Configured by a composition that has already resolved this capsule's
# external dependencies through ArkenExternalPackageGraph.
foreach(required arken::external_flac_v1 arken::external_ogg_v1 arken::external_vorbis_v1
    arken::external_opus_v1 arken::external_minimp3_v1 arken::external_lame_v1)
    if(NOT TARGET ${required})
        message(FATAL_ERROR "arken_audio_codecs_v1 requires the resolved external package target ${required}")
    endif()
endforeach()
add_library(arken_audio_codecs_v1 STATIC src/audio.cpp src/wav.cpp src/flac.cpp src/aiff.cpp
    src/ogg.cpp src/vorbis.cpp src/opus.cpp src/mp3.cpp src/minimp3.c)
add_library(arken::audio_codecs_v1 ALIAS arken_audio_codecs_v1)
target_compile_features(arken_audio_codecs_v1 PUBLIC cxx_std_20)
target_include_directories(arken_audio_codecs_v1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(arken_audio_codecs_v1 PRIVATE arken::external_flac_v1 arken::external_ogg_v1
    arken::external_vorbis_v1 arken::external_opus_v1 arken::external_minimp3_v1 arken::external_lame_v1)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arken_audio_codecs_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror -ffp-contract=off)
endif()
