cmake_minimum_required(VERSION 3.25)
project(ArkenNativeBrowserCppBootstrap LANGUAGES NONE)
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    message(FATAL_ERROR "The declared WASI SDK 34 tool package currently supports only the macOS arm64 build host")
endif()
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json"
    WORKSPACE_ROOT "${workspace}")
get_target_property(sdk_root arken::external_wasi_sdk_v1 INTERFACE_INCLUDE_DIRECTORIES)
file(REAL_PATH "${sdk_root}" sdk_root)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/toolchain-root.txt" CONTENT "${sdk_root}\n" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/toolchain.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/toolchain.cmake" @ONLY)
include(CTest)
if(BUILD_TESTING)
    add_test(NAME NativeBrowserCpp.MemoryProfiles
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${workspace}"
            "-DTOOLCHAIN=${CMAKE_CURRENT_BINARY_DIR}/toolchain.cmake"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/memory-profile-proof"
            -P "${CMAKE_CURRENT_LIST_DIR}/../tests/browser_cpp_memory_profile.cmake")
    set_tests_properties(NativeBrowserCpp.MemoryProfiles PROPERTIES TIMEOUT 60)
endif()
