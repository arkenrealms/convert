foreach(required WASM OUTPUT PAYMENT_PACKETS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
get_filename_component(owner "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY_FILE "${WASM}" "${OUTPUT}/conversion.wasm" ONLY_IF_DIFFERENT)
foreach(file runtime.js worker.js)
    file(COPY_FILE "${owner}/platform/cloudflare/${file}" "${OUTPUT}/${file}" ONLY_IF_DIFFERENT)
endforeach()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/worker_tests.js" "${OUTPUT}/tests.js" ONLY_IF_DIFFERENT)
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_worker_tests.js" "${OUTPUT}/payment_worker_tests.js" ONLY_IF_DIFFERENT)
file(COPY_FILE "${PAYMENT_PACKETS}" "${OUTPUT}/payment-packets.json" ONLY_IF_DIFFERENT)
file(WRITE "${OUTPUT}/config.capnp" [=[
using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
 services=[(name="tests",worker=(
  compatibilityDate="2026-09-11",
  modules=[(name="tests.js",esModule=embed "tests.js"),
           (name="worker.js",esModule=embed "worker.js"),
           (name="runtime.js",esModule=embed "runtime.js"),
           (name="payment_worker_tests.js",esModule=embed "payment_worker_tests.js"),
           (name="payment-packets.json",text=embed "payment-packets.json"),
           (name="conversion.wasm",wasm=embed "conversion.wasm")]
  )),(name="internet",network=(allow=[]))]
);
]=])
