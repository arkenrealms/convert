foreach(required WASM WORKERD ACTOR_FIXTURES X402_FIXTURES RECONCILIATION_FIXTURES PUBLIC_ADMISSION_FIXTURES PROVIDER_FIXTURES STARTUP_FIXTURES OUTPUT)
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
foreach(file runtime.js worker.js payment_ledger.js x402_facilitator.js paid_conversion.js sandbox_worker.js bounded_body.js settlement_reconciliation.js public_admission.js cdp_authorization.js production_worker.js production_startup.js)
  file(COPY_FILE "${owner}/platform/cloudflare/${file}" "${run}/${file}")
endforeach()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_actor_tests.js" "${run}/tests.js")
file(COPY_FILE "${ACTOR_FIXTURES}" "${run}/payment-actor-fixtures.json")
file(COPY_FILE "${X402_FIXTURES}" "${run}/x402-fixtures.json")
file(COPY_FILE "${RECONCILIATION_FIXTURES}" "${run}/reconciliation-fixtures.json")
file(COPY_FILE "${PUBLIC_ADMISSION_FIXTURES}" "${run}/public-admission-fixtures.json")
file(COPY_FILE "${PROVIDER_FIXTURES}" "${run}/provider-fixtures.json")
file(COPY_FILE "${STARTUP_FIXTURES}" "${run}/startup-fixtures.json")
foreach(file public_admission_actor_tests.js public_http_tests.js cdp_authorization_tests.js mainnet_profile_tests.js production_http_tests.js)
  file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/${file}" "${run}/${file}")
endforeach()
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/settlement_chain_fixture.js" "${run}/settlement_chain_fixture.js")
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
           (name="settlement_reconciliation.js",esModule=embed "settlement_reconciliation.js"),
           (name="public_admission.js",esModule=embed "public_admission.js"),
           (name="cdp_authorization.js",esModule=embed "cdp_authorization.js"),
           (name="cdp_authorization_tests.js",esModule=embed "cdp_authorization_tests.js"),
           (name="mainnet_profile_tests.js",esModule=embed "mainnet_profile_tests.js"),
           (name="production_http_tests.js",esModule=embed "production_http_tests.js"),
           (name="production_worker.js",esModule=embed "production_worker.js"),
           (name="production_startup.js",esModule=embed "production_startup.js"),
           (name="startup-fixtures.json",text=embed "startup-fixtures.json"),
           (name="provider-fixtures.json",text=embed "provider-fixtures.json"),
           (name="public-admission-fixtures.json",text=embed "public-admission-fixtures.json"),
           (name="public_admission_actor_tests.js",esModule=embed "public_admission_actor_tests.js"),
           (name="public_http_tests.js",esModule=embed "public_http_tests.js"),
           (name="settlement_chain_fixture.js",esModule=embed "settlement_chain_fixture.js"),
           (name="reconciliation-fixtures.json",text=embed "reconciliation-fixtures.json"),
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
file(SHA256 "${run}/reconciliation-fixtures.json" reconciliation_hash)
file(SHA256 "${run}/public-admission-fixtures.json" public_admission_hash)
file(SHA256 "${run}/provider-fixtures.json" provider_hash)
file(READ "${run}/provider-fixtures.json" provider_fixtures)
string(JSON provider_vectors LENGTH "${provider_fixtures}" vectors)
string(JSON provider_signatures LENGTH "${provider_fixtures}" signingCases)
file(READ "${run}/public-admission-fixtures.json" public_admission_fixtures)
string(JSON public_admission_vectors LENGTH "${public_admission_fixtures}")
file(READ "${run}/reconciliation-fixtures.json" reconciliation_fixtures)
string(JSON reconciliation_vectors LENGTH "${reconciliation_fixtures}" vectors)
file(READ "${run}/x402-fixtures.json" x402_fixtures)
string(JSON x402_vectors LENGTH "${x402_fixtures}" vectors)
string(JSON paid_vectors LENGTH "${x402_fixtures}" paidVectors)
set(source_hashes "{}")
foreach(file runtime.js worker.js payment_ledger.js x402_facilitator.js paid_conversion.js sandbox_worker.js bounded_body.js settlement_reconciliation.js public_admission.js cdp_authorization.js cdp_authorization_tests.js mainnet_profile_tests.js production_worker.js production_startup.js production_http_tests.js tests.js x402_actor_tests.js paid_http_tests.js r2_bucket_fixture.js payment_recovery_actor_tests.js settlement_chain_fixture.js public_admission_actor_tests.js public_http_tests.js)
  file(SHA256 "${run}/${file}" source_hash)
  string(JSON source_hashes SET "${source_hashes}" "${file}" "\"${source_hash}\"")
endforeach()
file(WRITE "${run}/proof.json" "{\"status\":\"passed\",\"storage\":\"workerd-disk-backed-durable-objects\",\"processPhases\":2,\"network\":\"denied\",\"facilitator\":\"mock\",\"r2\":\"fixture\",\"chain\":\"fixture\",\"moduleSha256\":\"${module_hash}\",\"fixtureSha256\":\"${fixture_hash}\",\"x402FixtureSha256\":\"${x402_hash}\",\"reconciliationFixtureSha256\":\"${reconciliation_hash}\",\"publicAdmissionFixtureSha256\":\"${public_admission_hash}\",\"publicAdmissionNativeWasmVectors\":${public_admission_vectors},\"reconciliationNativeWasmVectors\":${reconciliation_vectors},\"x402NativeWasmVectors\":${x402_vectors},\"paidControllerNativeWasmVectors\":${paid_vectors},\"sourceHashes\":${source_hashes}}\n")
message(STATUS "Durable payment proof: ${run}/proof.json")
file(READ "${run}/proof.json" proof)
string(JSON proof SET "${proof}" providerNativeWasmVectors "${provider_vectors}")
string(JSON proof SET "${proof}" providerNativeWorkerSignatures "${provider_signatures}")
string(JSON proof SET "${proof}" providerFixtureSha256 "\"${provider_hash}\"")
file(READ "${run}/startup-fixtures.json" startup_fixtures)
file(SHA256 "${run}/startup-fixtures.json" startup_hash)
string(JSON startup_vectors LENGTH "${startup_fixtures}" vectors)
string(JSON proof SET "${proof}" startupNativeWasmVectors "${startup_vectors}")
string(JSON qualification_vectors LENGTH "${startup_fixtures}" qualificationVectors)
string(JSON proof SET "${proof}" qualificationNativeWasmVectors "${qualification_vectors}")
string(JSON proof SET "${proof}" startupFixtureSha256 "\"${startup_hash}\"")
file(WRITE "${run}/proof.json" "${proof}\n")
