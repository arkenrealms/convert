cmake_minimum_required(VERSION 3.25)
project(ArkenImageAvifDependencies LANGUAGES C CXX)
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json" WORKSPACE_ROOT "${workspace}")
get_target_property(heif_source arken::external_heif_av1_v1 ARKEN_EXTERNAL_PACKAGE_SOURCE_ROOT)
add_executable(arken-avif-dependency-check check.cpp)
target_compile_features(arken-avif-dependency-check PRIVATE cxx_std_20)
target_compile_options(arken-avif-dependency-check PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(arken-avif-dependency-check PRIVATE arken::external_heif_av1_v1 arken::external_aom_v1)
if(CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-unknown-emscripten")
  # Browser parity has not been qualified for AVIF. Refuse rather than
  # producing an unreviewed module with an unchecked import surface.
  message(FATAL_ERROR "The AVIF dependency check is native-only until its browser runtime is qualified")
else()
  enable_testing()
  add_test(NAME AvifDependencies.Version COMMAND arken-avif-dependency-check --version --invalid)
  set_tests_properties(AvifDependencies.Version PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "avif-deps-1")
  add_test(NAME AvifDependencies.Admission COMMAND arken-avif-dependency-check)
  set_tests_properties(AvifDependencies.Admission PROPERTIES TIMEOUT 15)
  # Decoded-pixel fingerprints observed with GCC 13 / Linux x86-64. They are
  # dependency evidence (libheif default YCbCr->RGB, no ICC handling), pinned so
  # that a compiler, flag or source change that alters arithmetic is noticed.
  add_test(NAME AvifDependencies.Sample COMMAND arken-avif-dependency-check --decode "${heif_source}/examples/example.avif")
  set_tests_properties(AvifDependencies.Sample PROPERTIES TIMEOUT 60
    PASS_REGULAR_EXPRESSION "\"status\":\"passed\",\"code\":0,\"width\":800,\"height\":533,.*\"rgbaFnv1a64\":\"fa6ee36f0fba9dfd\"")
  add_test(NAME AvifDependencies.SampleTransform COMMAND arken-avif-dependency-check --decode "${heif_source}/tests/data/clap_cropped_irot_imir.avif")
  set_tests_properties(AvifDependencies.SampleTransform PROPERTIES TIMEOUT 30
    PASS_REGULAR_EXPRESSION "\"status\":\"passed\",\"code\":0,\"width\":64,\"height\":64,.*\"rgbaFnv1a64\":\"9a67e30862ebb142\"")
  # The low-overhead 'mif3'/'mini' reduced-header form is not admitted.
  add_test(NAME AvifDependencies.RejectMini COMMAND arken-avif-dependency-check --decode "${heif_source}/tests/data/simple_osm_tile_alpha.avif")
  set_tests_properties(AvifDependencies.RejectMini PROPERTIES TIMEOUT 15 PASS_REGULAR_EXPRESSION "\"status\":\"failed\",\"code\":11,")
  add_test(NAME AvifDependencies.RoundTrip COMMAND arken-avif-dependency-check --roundtrip)
  set_tests_properties(AvifDependencies.RoundTrip PROPERTIES TIMEOUT 120)
  add_test(NAME AvifDependencies.RejectHeic COMMAND arken-avif-dependency-check --decode "${heif_source}/examples/example.heic")
  # An HEVC-coded HEIC is refused by brand (code 11) before any decoder runs.
  set_tests_properties(AvifDependencies.RejectHeic PROPERTIES TIMEOUT 15 PASS_REGULAR_EXPRESSION "\"status\":\"failed\",\"code\":11,")
endif()
