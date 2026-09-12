cmake_minimum_required(VERSION 3.25)
project(FileConversionWorkerProof LANGUAGES NONE)
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${workspace}/sdk/arken_sdk_dev/cmake/ArkenExternalPackageGraph.cmake")
arken_resolve_external_packages(ROOT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/lib.manifest.json" WORKSPACE_ROOT "${workspace}")
get_target_property(workerd arken::external_workerd_v1 IMPORTED_LOCATION)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/workerd-path.txt" CONTENT "${workerd}\n" @ONLY)
set(CONVERSION_WASM "" CACHE FILEPATH "Exact qualified conversion service module")
set(CONVERSION_PAYMENT_PACKETS "" CACHE FILEPATH "Native arken-conversion-payment-tests --write-fixtures payment-packets.json output")
set(CONVERSION_PAYMENT_ACTORS "" CACHE FILEPATH "Native payment-actor-fixtures.json output")
set(CONVERSION_X402_FIXTURES "" CACHE FILEPATH "Native x402-fixtures.json output")
if(CONVERSION_WASM)
    if(NOT EXISTS "${CONVERSION_WASM}")
        message(FATAL_ERROR "CONVERSION_WASM does not exist")
    endif()
    if(NOT EXISTS "${CONVERSION_PAYMENT_PACKETS}")
        message(FATAL_ERROR "Generate CONVERSION_PAYMENT_PACKETS with the owning native payment tests first")
    endif()
    enable_testing()
    add_test(NAME FileConversionWorker.Protocol COMMAND "${CMAKE_COMMAND}"
        "-DWASM=${CONVERSION_WASM}" "-DWORKERD=${workerd}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/worker-proof"
        "-DPAYMENT_PACKETS=${CONVERSION_PAYMENT_PACKETS}"
        -P "${CMAKE_CURRENT_LIST_DIR}/../run_workerd.cmake")
    set_tests_properties(FileConversionWorker.Protocol PROPERTIES TIMEOUT 30)
    if(CONVERSION_PAYMENT_ACTORS)
        if(NOT EXISTS "${CONVERSION_PAYMENT_ACTORS}")
            message(FATAL_ERROR "CONVERSION_PAYMENT_ACTORS does not exist")
        endif()
        if(NOT EXISTS "${CONVERSION_X402_FIXTURES}")
            message(FATAL_ERROR "Generate CONVERSION_X402_FIXTURES with arken-conversion-x402-tests first")
        endif()
        add_test(NAME FileConversionWorker.DurablePayments COMMAND "${CMAKE_COMMAND}"
            "-DWASM=${CONVERSION_WASM}" "-DWORKERD=${workerd}"
            "-DACTOR_FIXTURES=${CONVERSION_PAYMENT_ACTORS}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/actor-proof"
            "-DX402_FIXTURES=${CONVERSION_X402_FIXTURES}"
            -P "${CMAKE_CURRENT_LIST_DIR}/../run_payment_actors.cmake")
        set_tests_properties(FileConversionWorker.DurablePayments PROPERTIES TIMEOUT 60)
    endif()
endif()
