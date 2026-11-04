cmake_minimum_required(VERSION 3.25)
project(ArkenImageHeifDependencies LANGUAGES C CXX)
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json" WORKSPACE_ROOT "${workspace}")
get_target_property(heif_source arken::external_heif_hevc_v1 ARKEN_EXTERNAL_PACKAGE_SOURCE_ROOT)
add_executable(arken-heif-dependency-check check.cpp)
target_compile_features(arken-heif-dependency-check PRIVATE cxx_std_20)
target_link_libraries(arken-heif-dependency-check PRIVATE arken::external_heif_hevc_v1 arken::external_de265_v1)
if(CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-unknown-emscripten")
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ArkenHeifBrowserRuntime.cmake")
  arken_add_heif_browser_runtime()
  target_link_libraries(arken-heif-dependency-check PRIVATE arken_image_heif_browser_runtime_v1 ${ARKEN_NATIVE_CPP_RUNTIME_LIBRARIES})
  target_link_options(arken-heif-dependency-check PRIVATE -Wl,--export=arken_heif_dependency_check
    -Wl,--export=arken_heif_dependency_decode -Wl,--export=arken_heif_dependency_width
    -Wl,--export=arken_heif_dependency_height -Wl,--export=arken_heif_dependency_pixels
    -Wl,--export=malloc -Wl,--export=free)
  set_target_properties(arken-heif-dependency-check PROPERTIES SUFFIX ".wasm")
  if(HEIF_QUALIFICATION_ASSET_ROOT)
    if(NOT IS_ABSOLUTE "${HEIF_QUALIFICATION_ASSET_ROOT}")
      message(FATAL_ERROR "HEIF_QUALIFICATION_ASSET_ROOT must be the explicit existing product asset root")
    endif()
    set(staged "${HEIF_QUALIFICATION_ASSET_ROOT}/capsules/arken_image_codecs_v1/qualification")
    add_custom_target(arken-heif-dependency-assets
      COMMAND ${CMAKE_COMMAND} -E make_directory "${staged}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:arken-heif-dependency-check> "${staged}/heif-dependencies.wasm"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${heif_source}/examples/example.heic" "${staged}/example.heic"
      DEPENDS arken-heif-dependency-check "${heif_source}/examples/example.heic" VERBATIM)
  endif()
else()
  enable_testing()
  add_test(NAME HeifDependencies.Version COMMAND arken-heif-dependency-check --version --invalid)
  set_tests_properties(HeifDependencies.Version PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "r6364")
  add_test(NAME HeifDependencies.Admission COMMAND arken-heif-dependency-check)
  set_tests_properties(HeifDependencies.Admission PROPERTIES TIMEOUT 15)
  add_test(NAME HeifDependencies.Sample COMMAND arken-heif-dependency-check --decode "${heif_source}/examples/example.heic")
  set_tests_properties(HeifDependencies.Sample PROPERTIES TIMEOUT 30)
endif()
