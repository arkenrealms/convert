foreach(required WASM WORKERD ACTOR_FIXTURES OUTPUT)
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
foreach(file runtime.js payment_ledger.js)
  file(COPY_FILE "${owner}/platform/cloudflare/${file}" "${run}/${file}")
endforeach()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_actor_tests.js" "${run}/tests.js")
file(COPY_FILE "${ACTOR_FIXTURES}" "${run}/payment-actor-fixtures.json")
set(template [=[
using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
 services=[(name="tests",worker=(
  compatibilityDate="2026-09-11",
  modules=[(name="tests.js",esModule=embed "tests.js"),
           (name="payment_ledger.js",esModule=embed "payment_ledger.js"),
           (name="runtime.js",esModule=embed "runtime.js"),
           (name="payment-actor-fixtures.json",text=embed "payment-actor-fixtures.json"),
           (name="conversion.wasm",wasm=embed "conversion.wasm")],
  bindings=[(name="LEDGER",durableObjectNamespace=(className="Ledger")),(name="PHASE",text="@phase@")],
  durableObjectNamespaces=[(className="Ledger",uniqueKey="arken-conversion-payment-fixture-v1",enableSql=true)],
  durableObjectStorage=(localDisk="state")
  )),(name="state",disk=(path="@run@/state",writable=true)),(name="internet",network=(allow=[]))]
);
]=])
foreach(phase write read)
  string(CONFIGURE "${template}" config @ONLY)
  file(WRITE "${run}/${phase}.capnp" "${config}")
  execute_process(COMMAND "${WORKERD}" test "${run}/${phase}.capnp"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error TIMEOUT 25)
  file(WRITE "${run}/${phase}.log" "${out}${error}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Payment actor ${phase} failed: ${out}${error}")
  endif()
endforeach()
file(SHA256 "${WASM}" module_hash)
file(SHA256 "${ACTOR_FIXTURES}" fixture_hash)
file(WRITE "${run}/proof.json" "{\"status\":\"passed\",\"storage\":\"workerd-disk-backed-durable-objects\",\"processPhases\":2,\"network\":\"denied\",\"moduleSha256\":\"${module_hash}\",\"fixtureSha256\":\"${fixture_hash}\"}\n")
message(STATUS "Durable payment proof: ${run}/proof.json")
