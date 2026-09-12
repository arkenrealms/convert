foreach(required WASM WORKERD ACTOR_FIXTURES X402_FIXTURES OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} required")
  endif()
endforeach()
execute_process(COMMAND "${WORKERD}" --version RESULT_VARIABLE version_result OUTPUT_VARIABLE version TIMEOUT 5)
if(NOT version_result EQUAL 0 OR NOT version MATCHES "workerd 2026-09-11")
  message(FATAL_ERROR "Wrong Worker runtime: ${version}")
endif()
get_filename_component(owner "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(run "${OUTPUT}/run-${run_id}")
file(MAKE_DIRECTORY "${run}/state")
file(COPY_FILE "${WASM}" "${run}/conversion.wasm")
foreach(file runtime.js worker.js payment_ledger.js x402_facilitator.js paid_conversion.js sandbox_worker.js bounded_body.js)
  file(COPY_FILE "${owner}/platform/cloudflare/${file}" "${run}/${file}")
endforeach()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_actor_tests.js" "${run}/tests.js")
file(COPY_FILE "${ACTOR_FIXTURES}" "${run}/payment-actor-fixtures.json")
file(COPY_FILE "${X402_FIXTURES}" "${run}/x402-fixtures.json")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/x402_actor_tests.js" "${run}/x402_actor_tests.js")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/paid_http_tests.js" "${run}/paid_http_tests.js")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/r2_bucket_fixture.js" "${run}/r2_bucket_fixture.js")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_recovery_actor_tests.js" "${run}/payment_recovery_actor_tests.js")
set(template [=[
using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
 services=[(name="tests",worker=(
  compatibilityDate="2026-09-11",
  modules=[(name="tests.js",esModule=embed "tests.js"),
           (name="payment_ledger.js",esModule=embed "payment_ledger.js"),
           (name="runtime.js",esModule=embed "runtime.js"),
           (name="bounded_body.js",esModule=embed "bounded_body.js"),
           (name="x402_facilitator.js",esModule=embed "x402_facilitator.js"),
           (name="worker.js",esModule=embed "worker.js"),
           (name="paid_conversion.js",esModule=embed "paid_conversion.js"),
           (name="sandbox_worker.js",esModule=embed "sandbox_worker.js"),
           (name="paid_http_tests.js",esModule=embed "paid_http_tests.js"),
           (name="r2_bucket_fixture.js",esModule=embed "r2_bucket_fixture.js"),
           (name="payment_recovery_actor_tests.js",esModule=embed "payment_recovery_actor_tests.js"),
           (name="x402_actor_tests.js",esModule=embed "x402_actor_tests.js"),
           (name="x402-fixtures.json",text=embed "x402-fixtures.json"),
           (name="payment-actor-fixtures.json",text=embed "payment-actor-fixtures.json"),
           (name="conversion.wasm",wasm=embed "conversion.wasm")],
  bindings=[(name="LEDGER",durableObjectNamespace=(className="Ledger")),(name="PHASE",text="@phase@")],
  durableObjectNamespaces=[(className="Ledger",uniqueKey="arken-conversion-payment-fixture-v1",enableSql=true)],
  durableObjectStorage=(localDisk="state")
  )),(name="state",disk=(path="@run@/state",writable=true)),(name="internet",network=(allow=[]))]
);
]=])
include("${CMAKE_CURRENT_LIST_DIR}/run_workerd_process.cmake")
foreach(phase write read)
  string(CONFIGURE "${template}" config @ONLY)
  file(WRITE "${run}/${phase}.capnp" "${config}")
  run_conversion_workerd("${run}/${phase}.capnp" 25 result out error metrics)
  file(WRITE "${run}/${phase}-process-metrics.json" "${metrics}\n")
  file(WRITE "${run}/${phase}.log" "${out}${error}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Payment actor ${phase} failed: ${out}${error}")
  endif()
endforeach()
file(SHA256 "${run}/conversion.wasm" module_hash)
file(SHA256 "${run}/payment-actor-fixtures.json" fixture_hash)
file(SHA256 "${run}/x402-fixtures.json" x402_hash)
file(READ "${run}/x402-fixtures.json" x402_fixtures)
string(JSON x402_vectors LENGTH "${x402_fixtures}" vectors)
string(JSON paid_vectors LENGTH "${x402_fixtures}" paidVectors)
set(source_hashes "{}")
foreach(file runtime.js worker.js payment_ledger.js x402_facilitator.js paid_conversion.js sandbox_worker.js bounded_body.js tests.js x402_actor_tests.js paid_http_tests.js r2_bucket_fixture.js payment_recovery_actor_tests.js)
  file(SHA256 "${run}/${file}" source_hash)
  string(JSON source_hashes SET "${source_hashes}" "${file}" "\"${source_hash}\"")
endforeach()
file(WRITE "${run}/proof.json" "{\"status\":\"passed\",\"storage\":\"workerd-disk-backed-durable-objects\",\"processPhases\":2,\"network\":\"denied\",\"facilitator\":\"mock\",\"r2\":\"fixture\",\"moduleSha256\":\"${module_hash}\",\"fixtureSha256\":\"${fixture_hash}\",\"x402FixtureSha256\":\"${x402_hash}\",\"x402NativeWasmVectors\":${x402_vectors},\"paidControllerNativeWasmVectors\":${paid_vectors},\"sourceHashes\":${source_hashes}}\n")
message(STATUS "Durable payment proof: ${run}/proof.json")
