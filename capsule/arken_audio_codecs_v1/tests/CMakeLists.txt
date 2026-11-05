cmake_minimum_required(VERSION 3.25)
project(ArkenAudioCodecsQualification LANGUAGES C CXX)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# The capsule's own manifest declares flac-1.5.0-v1; this project resolves the
# first-party graph from it rather than restating the external dependency.
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenModuleGraph.cmake")
arken_resolve_enabled_capsules(modules ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json"
  CAPSULE_ROOT "${workspace}/capsule")
if(NOT modules STREQUAL "arken_audio_codecs_v1")
  message(FATAL_ERROR "Audio codec qualification must resolve only arken_audio_codecs_v1, not: ${modules}")
endif()
arken_get_resolved_module_manifests(manifests)
# The flac-1.5.0 archive's UTF-8 member names are handled by the SDK, which lists
# and extracts archives with a UTF-8 locale when the host provides none.
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json"
  MODULE_MANIFESTS ${manifests} WORKSPACE_ROOT "${workspace}")
add_subdirectory("${workspace}/capsule/arken_audio_codecs_v1" "capsule")

add_executable(arken-audio-codec-tests audio_codec_tests.cpp)
target_link_libraries(arken-audio-codec-tests PRIVATE arken::audio_codecs_v1)
target_compile_options(arken-audio-codec-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
enable_testing()
foreach(check WavRoundTrip FlacRoundTrip ChannelMask Malformed Limits Determinism LongSignal Conversion
    AiffRate AiffRoundTrip AiffVariants AiffMalformed AiffLimits AiffDeterminism)
  add_test(NAME AudioCodecs.${check} COMMAND arken-audio-codec-tests --check ${check})
  set_tests_properties(AudioCodecs.${check} PROPERTIES TIMEOUT 120)
endforeach()
# Optional independent oracle: registered only when the host provides FFmpeg.
# It is a reference decoder/encoder, never a product dependency.
find_program(ARKEN_AUDIO_INTEROP_FFMPEG ffmpeg)
if(ARKEN_AUDIO_INTEROP_FFMPEG)
  add_test(NAME AudioCodecs.FfmpegInterop COMMAND "${CMAKE_COMMAND}"
    -DTOOL=$<TARGET_FILE:arken-audio-codec-tests> -DFFMPEG=${ARKEN_AUDIO_INTEROP_FFMPEG}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/interop -P "${CMAKE_CURRENT_LIST_DIR}/interop.cmake")
  set_tests_properties(AudioCodecs.FfmpegInterop PROPERTIES TIMEOUT 120)
else()
  message(STATUS "ffmpeg not found: AudioCodecs.FfmpegInterop is not registered")
endif()

# Lossy codecs (Ogg Vorbis, Ogg Opus, MP3).
add_executable(arken-compressed-audio-tests compressed_audio_tests.cpp)
target_link_libraries(arken-compressed-audio-tests PRIVATE arken::audio_codecs_v1)
target_compile_options(arken-compressed-audio-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(check Quality Gapless Determinism Reports OggStructure Malformed Limits Mp3GaplessFloor)
  add_test(NAME CompressedAudio.${check} COMMAND arken-compressed-audio-tests --check ${check})
  set_tests_properties(CompressedAudio.${check} PROPERTIES TIMEOUT 300)
endforeach()
add_test(NAME CompressedAudio.Version COMMAND arken-compressed-audio-tests --version)
set_tests_properties(CompressedAudio.Version PROPERTIES TIMEOUT 5
  PASS_REGULAR_EXPRESSION "Xiph\\.Org libVorbis 1\\.3\\.7; libopus 1\\.5\\.2; LAME 3\\.100")
if(ARKEN_AUDIO_INTEROP_FFMPEG)
  add_test(NAME CompressedAudio.FfmpegInterop COMMAND "${CMAKE_COMMAND}"
    -DTOOL=$<TARGET_FILE:arken-compressed-audio-tests> -DFFMPEG=${ARKEN_AUDIO_INTEROP_FFMPEG}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/lossy-interop -P "${CMAKE_CURRENT_LIST_DIR}/lossy_interop.cmake")
  set_tests_properties(CompressedAudio.FfmpegInterop PROPERTIES TIMEOUT 300)
endif()

add_test(NAME AudioCodecs.Version COMMAND arken-audio-codec-tests --version --invalid)
set_tests_properties(AudioCodecs.Version PROPERTIES TIMEOUT 5
  PASS_REGULAR_EXPRESSION "arken_audio_codecs_v1 r1 \\(reference libFLAC 1\\.5\\.0 20250211\\)")
