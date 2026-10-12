# Candidate mode of the existing report harness. Never fill missing candidate
# bytes from current product source; both compared closures are frozen inputs.
function(stage_conversion_journal_candidate output)
 file(READ "${JOURNAL_PACKAGE}/candidate.json" candidate)
 file(SHA256 "${JOURNAL_PACKAGE}/candidate.json" candidate_hash)
 string(JSON schema GET "${candidate}" schema)
 string(JSON paid GET "${candidate}" paidModuleSha256)
 string(JSON config_hash GET "${candidate}" configurationSha256)
 file(SHA256 "${JOURNAL_PACKAGE}/configuration.json" actual_config_hash)
 if(NOT schema STREQUAL "arken.convert.journal-candidate.v1" OR
    NOT paid STREQUAL "567512707e9f220282f820afbfaad85f4ea1d911264b381bd238b5df260648dc" OR
    NOT config_hash STREQUAL actual_config_hash)
  message(FATAL_ERROR "Invalid Convert journal candidate")
 endif()
 set(extra "")
 set(report_modules "")
 set(report_names)
 set(paid_modules NOTICE.txt bounded_body.js cdp_authorization.js conversion.wasm paid_conversion.js payment_ledger.js production_startup.js production_worker.js public_admission.js runtime.js settlement_reconciliation.js worker.js x402_facilitator.js)
 set(base_modules conversion.wasm runtime.js payment_ledger.js public_admission.js conversion-journal-report.wasm
  journal_worker.js journal_ledger.js journal_report_runtime.js report_runtime.js report_http_transport.js bounded_body.js)
 foreach(kind files predecessorFiles)
  string(JSON rows GET "${candidate}" ${kind})
  string(JSON count LENGTH "${rows}")
  if(kind STREQUAL "files")
   set(input "${JOURNAL_PACKAGE}/package")
   set(prefix "")
   if(NOT count EQUAL 20)
    message(FATAL_ERROR "Candidate module closure changed")
   endif()
  else()
   set(input "${JOURNAL_PACKAGE}/predecessor")
   set(prefix "predecessor/")
   file(MAKE_DIRECTORY "${output}/predecessor")
   if(NOT count EQUAL 13)
    message(FATAL_ERROR "Predecessor module closure changed")
   endif()
  endif()
  math(EXPR last "${count}-1")
  set(names)
  foreach(i RANGE 0 ${last})
   string(JSON name GET "${rows}" ${i} name)
   string(JSON hash GET "${rows}" ${i} sha256)
   string(JSON size GET "${rows}" ${i} bytes)
   if(NOT name MATCHES "^[A-Za-z0-9_.-]+$" OR name IN_LIST names)
    message(FATAL_ERROR "Invalid candidate module name")
   endif()
   list(APPEND names "${name}")
   file(SHA256 "${input}/${name}" actual)
   file(SIZE "${input}/${name}" actual_size)
   if(NOT actual STREQUAL hash OR NOT actual_size EQUAL size)
    message(FATAL_ERROR "Candidate module changed: ${name}")
   endif()
   file(COPY_FILE "${input}/${name}" "${output}/${prefix}${name}")
   if(name MATCHES "[.]wasm$")
    set(type wasm)
   elseif(name MATCHES "[.]js$")
    set(type esModule)
   elseif(name MATCHES "[.]txt$")
    set(type text)
   else()
    message(FATAL_ERROR "Invalid candidate module type")
   endif()
   if(prefix OR NOT name IN_LIST base_modules)
    string(APPEND extra "(name=\"${prefix}${name}\",${type}=embed \"${prefix}${name}\"),\n")
   endif()
   if(NOT prefix AND NOT name IN_LIST paid_modules)
    string(APPEND report_modules "(name=\"${name}\",${type}=embed \"${name}\"),\n")
    list(APPEND report_names "${name}")
   endif()
  endforeach()
 endforeach()
 file(WRITE "${output}/report_subject.js" [=[
export {ConversionPaymentLedger as SubjectLedger} from './production_worker.js';
import current,{createProductionWorker} from './production_worker.js';
import previous from './predecessor/production_worker.js';
export const createReportSubject=clock=>createProductionWorker({clock});
export async function provePublicPassthrough(worker,env){
 env={...env,MCP_ENDPOINT_PATH:'/v1',MCP_PUBLIC_ORIGIN:'https://convert.mcp.arken.gg',PAYMENTS_ENABLED:'false'};
 const headers={'content-type':'application/json',accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'};
 const packets=[{method:'initialize',params:{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'fixture',version:'1'}}},
  {method:'tools/list',params:{}},{method:'tools/call',params:{name:'prepare_upload',arguments:{source_bytes:22,input_format:'tga',output_format:'png'}}}];
 let toolCount=0,checks=0;
 for(const packet of packets){
  const request=()=>new Request('https://convert.mcp.arken.gg/v1',{method:'POST',headers,body:JSON.stringify({jsonrpc:'2.0',id:1,...packet})});
  const before=await previous.fetch(request(),env),text=await before.text();
  for(const app of [worker,current]){
   const after=await app.fetch(request(),env);
   if(before.status!==after.status||JSON.stringify([...before.headers])!==JSON.stringify([...after.headers])||text!==await after.text())throw Error('Production response changed');
   ++checks;
  }
  if(packet.method==='tools/list')toolCount=JSON.parse(text).result.tools.length;
 }
 if(toolCount!==8)throw Error('Production command inventory changed');
 return {passed:true,productionEntry:true,defaultExport:true,toolCount,checks};
}
]=])
 string(JSON candidate_files GET "${candidate}" files)
 set(candidate_hash "${candidate_hash}" PARENT_SCOPE)
 set(candidate_files "${candidate_files}" PARENT_SCOPE)
 set(extra_modules "${extra}" PARENT_SCOPE)
 set(candidate_report_modules "${report_modules}" PARENT_SCOPE)
 set(candidate_report_names "${report_names}" PARENT_SCOPE)
endfunction()
