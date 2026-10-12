cmake_minimum_required(VERSION 3.25)
foreach(required REPORT_MODULE PAYMENT_TESTS WORKERD FROZEN_PACKAGE OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Supply exact ${required}")
  endif()
endforeach()
if(EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Payment journal proof requires a new evidence directory")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${PAYMENT_TESTS}" --version --invalid RESULT_VARIABLE status OUTPUT_VARIABLE version TIMEOUT 5)
if(NOT status EQUAL 0 OR NOT version MATCHES "^arken-conversion-payment-tests r5729")
  message(FATAL_ERROR "Incorrect native payment journal owner")
endif()
set(configuration_argument)
if(JOURNAL_PACKAGE)
 set(configuration_argument "${JOURNAL_PACKAGE}/configuration.json")
endif()
execute_process(COMMAND "${PAYMENT_TESTS}" --write-fixtures "${OUTPUT}" ${configuration_argument} RESULT_VARIABLE status OUTPUT_VARIABLE native ERROR_VARIABLE error TIMEOUT 15)
file(WRITE "${OUTPUT}/native.log" "${native}${error}")
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Native payment journal proof failed: ${error}")
endif()
# Preserve the accepted paid module and its actual platform transport. Report
# qualification must not quietly substitute a freshly rebuilt paid engine.
foreach(pair
    "conversion.wasm|567512707e9f220282f820afbfaad85f4ea1d911264b381bd238b5df260648dc"
    "runtime.js|e7afdc8ce8e7b25d45108a4960dae0a7c5bed72c7511963211d21941ef2cb42f"
    "payment_ledger.js|76d1f9efcab28b4652a24632c69b0a6ccf6c5a1ee72b6c0266f40cfa284e9b01"
    "public_admission.js|d00176227c2903dedf0e7b4c6171b2e55d285165c18eff4cc5f7361d9cb2e445")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 name)
  list(GET fields 1 expected)
  file(SHA256 "${FROZEN_PACKAGE}/${name}" observed)
  if(NOT observed STREQUAL expected)
    message(FATAL_ERROR "Frozen Convert dependency changed: ${name}")
  endif()
  file(COPY_FILE "${FROZEN_PACKAGE}/${name}" "${OUTPUT}/${name}")
endforeach()
file(SIZE "${REPORT_MODULE}" bytes)
if(bytes GREATER 8388608)
  message(FATAL_ERROR "Convert report exceeds deployment module cap")
endif()
file(COPY_FILE "${REPORT_MODULE}" "${OUTPUT}/conversion-journal-report.wasm")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_journal_worker.js" "${OUTPUT}/tests.js")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/payment_journal_http.js" "${OUTPUT}/payment_journal_http.js")
include("${CMAKE_CURRENT_LIST_DIR}/../../arken_paid_operation_v1/cmake/WorkerAssets.cmake")
arken_paid_operation_stage_report_transport("${OUTPUT}")
include("${CMAKE_CURRENT_LIST_DIR}/../../arken_cdp_transport_v1/cmake/WorkerAssets.cmake")
arken_cdp_transport_stage_worker("${OUTPUT}")
foreach(name journal_worker.js journal_ledger.js journal_report_runtime.js)
 file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/../platform/cloudflare/${name}" "${OUTPUT}/${name}")
endforeach()
file(WRITE "${OUTPUT}/report_subject.js" [=[
export {ConversionJournalLedger as SubjectLedger} from './journal_ledger.js';
import {withConversionJournalReport} from './journal_worker.js';
export const createReportSubject=clock=>withConversionJournalReport({fetch:async()=>new Response('public-route-retained',{status:202})},{clock});
export async function provePublicPassthrough(worker,env){return {passed:(await worker.fetch(new Request('https://convert.mcp.arken.gg/v1'),env)).status===202,productionEntry:false};}
]=])
set(extra_modules "")
if(JOURNAL_PACKAGE)
 include("${CMAKE_CURRENT_LIST_DIR}/stage_journal_candidate.cmake")
 stage_conversion_journal_candidate("${OUTPUT}")
endif()
file(READ "${OUTPUT}/payment-journal-access-fixtures.json" fixtures)
string(JSON configuration GET "${fixtures}" seed admission serviceConfiguration)
file(WRITE "${OUTPUT}/report-configuration.json" "${configuration}")
file(MAKE_DIRECTORY "${OUTPUT}/state")
set(template [=[
using Workerd = import "/workerd/workerd.capnp";
const config:Workerd.Config=(services=[(name="tests",worker=(compatibilityDate="2026-09-11",modules=[
 (name="tests.js",esModule=embed "tests.js"),
 (name="payment-journal-fixtures.json",text=embed "payment-journal-fixtures.json"),
 (name="payment-journal-access-fixtures.json",text=embed "payment-journal-access-fixtures.json"),
 (name="payment-journal-actors.json",text=embed "payment-journal-actors.json"),
 (name="payment_journal_http.js",esModule=embed "payment_journal_http.js"),
 (name="report_subject.js",esModule=embed "report_subject.js"),
 @extra_modules@
 (name="journal_worker.js",esModule=embed "journal_worker.js"),
 (name="journal_ledger.js",esModule=embed "journal_ledger.js"),
 (name="journal_report_runtime.js",esModule=embed "journal_report_runtime.js"),
 (name="report_http_transport.js",esModule=embed "report_http_transport.js"),
 (name="report_runtime.js",esModule=embed "report_runtime.js"),
 (name="bounded_body.js",esModule=embed "bounded_body.js"),
 (name="payment_ledger.js",esModule=embed "payment_ledger.js"),
 (name="public_admission.js",esModule=embed "public_admission.js"),
 (name="conversion-journal-report.wasm",wasm=embed "conversion-journal-report.wasm"),
 (name="runtime.js",esModule=embed "runtime.js"),
 (name="conversion.wasm",wasm=embed "conversion.wasm")],
 bindings=[(name="PAYMENT_LEDGER",durableObjectNamespace=(className="JournalLedger")),
  (name="CONVERT_REPORT_CONFIGURATION",text=embed "report-configuration.json"),(name="PHASE",text="@phase@")],
 durableObjectNamespaces=[(className="JournalLedger",uniqueKey="conversion-journal-proof",enableSql=true)],
 durableObjectStorage=(localDisk="state")
)),(name="state",disk=(path="@OUTPUT@/state",writable=true)),(name="internet",network=(allow=[]))]);
]=])
execute_process(COMMAND "${WORKERD}" --version RESULT_VARIABLE status OUTPUT_VARIABLE version TIMEOUT 5)
if(NOT status EQUAL 0 OR NOT version MATCHES "workerd 2026-09-11")
  message(FATAL_ERROR "Incorrect pinned Workerd runtime")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/run_workerd_process.cmake")
foreach(phase write read)
 string(CONFIGURE "${template}" config @ONLY)
 file(WRITE "${OUTPUT}/${phase}.capnp" "${config}")
 run_conversion_workerd("${OUTPUT}/${phase}.capnp" 35 status report error metrics)
 file(WRITE "${OUTPUT}/${phase}.log" "${report}${error}")
 if(NOT status EQUAL 0)
  message(FATAL_ERROR "Convert report ${phase} proof failed: ${report}${error}")
 endif()
 string(REGEX MATCHALL "ARKEN_CONVERT_REPORT_PROOF [^\r\n]+" records "${report}")
 list(LENGTH records record_count)
 if(NOT record_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one owned report proof record")
 endif()
 list(GET records 0 record)
 string(REGEX REPLACE "^ARKEN_CONVERT_REPORT_PROOF " "" report "${record}")
 string(JSON passed GET "${report}" status)
 if(NOT passed STREQUAL "passed")
  message(FATAL_ERROR "Convert report acceptance absent")
 endif()
 string(JSON report SET "${report}" processMetrics "${metrics}")
 file(WRITE "${OUTPUT}/${phase}-proof.json" "${report}\n")
endforeach()
string(JSON report SET "${report}" processPhases 2)
string(JSON report SET "${report}" moduleBytes ${bytes})
foreach(name conversion.wasm runtime.js payment_ledger.js public_admission.js conversion-journal-report.wasm
 payment-journal-fixtures.json payment-journal-access-fixtures.json payment-journal-actors.json tests.js payment_journal_http.js
 journal_worker.js journal_ledger.js journal_report_runtime.js report_runtime.js report_http_transport.js bounded_body.js report-configuration.json write.capnp read.capnp write-proof.json)
  file(SHA256 "${OUTPUT}/${name}" hash)
  string(JSON report SET "${report}" "${name}" "\"${hash}\"")
endforeach()
file(SHA256 "${OUTPUT}/report_subject.js" subject_hash)
string(JSON report SET "${report}" report_subject.js "\"${subject_hash}\"")
if(JOURNAL_PACKAGE)
 string(JSON report SET "${report}" candidateSha256 "\"${candidate_hash}\"")
 string(JSON report SET "${report}" candidateFiles "${candidate_files}")
 string(JSON report SET "${report}" productionEntryQualified true)
endif()
file(WRITE "${OUTPUT}/proof.json" "${report}\n")
message(STATUS "Convert report companion proof: ${OUTPUT}/proof.json")
