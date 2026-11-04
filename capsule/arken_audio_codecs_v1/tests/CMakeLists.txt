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
# The pinned flac-1.5.0 release archive contains three test files with UTF-8
# names (test/flac-to-flac-metadata-test-files/). The SDK lists archives with
# `cmake -E tar`, whose libarchive cannot represent them under the POSIX "C"
# locale and refuses the archive. Give the child processes a UTF-8 character
# locale when the host provides none; the archive digest check is unchanged.
set(effective_ctype "$ENV{LC_ALL}")
if(effective_ctype STREQUAL "")
  set(effective_ctype "$ENV{LC_CTYPE}")
endif()
if(effective_ctype STREQUAL "")
  set(effective_ctype "$ENV{LANG}")
endif()
if(CMAKE_HOST_UNIX AND NOT effective_ctype MATCHES "[Uu][Tt][Ff]-?8")
  if(CMAKE_HOST_APPLE)
    set(ENV{LC_ALL} "en_US.UTF-8")
  else()
    set(ENV{LC_ALL} "C.UTF-8")
  endif()
  message(STATUS "Using LC_ALL=$ENV{LC_ALL} for UTF-8 archive member names")
endif()
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json"
  MODULE_MANIFESTS ${manifests} WORKSPACE_ROOT "${workspace}")
add_subdirectory("${workspace}/capsule/arken_audio_codecs_v1" "capsule")

add_executable(arken-audio-codec-tests audio_codec_tests.cpp)
target_link_libraries(arken-audio-codec-tests PRIVATE arken::audio_codecs_v1)
target_compile_options(arken-audio-codec-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
enable_testing()
foreach(check WavRoundTrip FlacRoundTrip ChannelMask Malformed Limits Determinism LongSignal Conversion)
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
add_test(NAME AudioCodecs.Version COMMAND arken-audio-codec-tests --version --invalid)
set_tests_properties(AudioCodecs.Version PROPERTIES TIMEOUT 5
  PASS_REGULAR_EXPRESSION "arken_audio_codecs_v1 r1 \\(reference libFLAC 1\\.5\\.0 20250211\\)")
