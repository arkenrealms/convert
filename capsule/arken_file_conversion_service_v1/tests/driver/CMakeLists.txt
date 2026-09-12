cmake_minimum_required(VERSION 3.25)
project(FileConversionWorkerProof LANGUAGES NONE)
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json" WORKSPACE_ROOT "${workspace}")
get_target_property(workerd arken::external_workerd_v1 IMPORTED_LOCATION)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/workerd-path.txt" CONTENT "${workerd}\n" @ONLY)
set(CONVERSION_WASM "" CACHE FILEPATH "Exact qualified conversion service module")
if(CONVERSION_WASM)
    if(NOT EXISTS "${CONVERSION_WASM}")
        message(FATAL_ERROR "CONVERSION_WASM does not exist")
    endif()
    enable_testing()
    add_test(NAME FileConversionWorker.Protocol COMMAND "${CMAKE_COMMAND}"
        "-DWASM=${CONVERSION_WASM}" "-DWORKERD=${workerd}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/worker-proof"
        -P "${CMAKE_CURRENT_LIST_DIR}/../run_workerd.cmake")
    set_tests_properties(FileConversionWorker.Protocol PROPERTIES TIMEOUT 30)
endif()
