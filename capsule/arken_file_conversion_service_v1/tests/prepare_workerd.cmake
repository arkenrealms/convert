foreach(required WASM OUTPUT PAYMENT_PACKETS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
get_filename_component(owner "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY_FILE "${WASM}" "${OUTPUT}/conversion.wasm" ONLY_IF_DIFFERENT)
include("${CMAKE_CURRENT_LIST_DIR}/../../arken_cdp_transport_v1/cmake/WorkerAssets.cmake")
arken_cdp_transport_stage_worker("${OUTPUT}")
foreach(file runtime.js worker.js public_admission.js)
    file(COPY_FILE "${owner}/platform/cloudflare/${file}" "${OUTPUT}/${file}" ONLY_IF_DIFFERENT)
endforeach()
if(OBSERVED_PACKAGE)
    # Telemetry-only qualification keeps the actual unchanged production
    # dependencies; do not accidentally qualify newer source instead.
    foreach(file bounded_body.js public_admission.js)
        file(COPY_FILE "${OBSERVED_PACKAGE}/${file}" "${OUTPUT}/${file}" ONLY_IF_DIFFERENT)
    endforeach()
endif()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/worker_tests.js" "${OUTPUT}/tests.js" ONLY_IF_DIFFERENT)
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/r2_bucket_fixture.js" "${OUTPUT}/r2_bucket_fixture.js" ONLY_IF_DIFFERENT)
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_worker_tests.js" "${OUTPUT}/payment_worker_tests.js" ONLY_IF_DIFFERENT)
file(COPY_FILE "${PAYMENT_PACKETS}" "${OUTPUT}/payment-packets.json" ONLY_IF_DIFFERENT)
file(WRITE "${OUTPUT}/config.capnp" [=[
using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
 services=[(name="tests",worker=(
  compatibilityDate="2026-09-11",
  modules=[(name="tests.js",esModule=embed "tests.js"),
           (name="worker.js",esModule=embed "worker.js"),
           (name="bounded_body.js",esModule=embed "bounded_body.js"),
           (name="public_admission.js",esModule=embed "public_admission.js"),
           (name="r2_bucket_fixture.js",esModule=embed "r2_bucket_fixture.js"),
           (name="runtime.js",esModule=embed "runtime.js"),
           (name="payment_worker_tests.js",esModule=embed "payment_worker_tests.js"),
           (name="payment-packets.json",text=embed "payment-packets.json"),
           (name="conversion.wasm",wasm=embed "conversion.wasm")]
  )),(name="internet",network=(allow=[]))]
);
]=])
