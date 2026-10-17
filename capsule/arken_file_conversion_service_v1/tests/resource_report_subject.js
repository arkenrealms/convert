// Resource proof measures only the candidate, not a second predecessor engine.
export {ConversionPaymentLedger as SubjectLedger} from './production_worker.js';
import {createResourceWorker,createDefaultResourceWorker} from './resource_subject.js';
export const createReportSubject=clock=>createResourceWorker({clock});
export async function provePublicPassthrough(worker,env){
 const request=()=>new Request('https://convert.mcp.arken.gg/v1',{method:'POST',headers:{'content-type':'application/json',
  accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'},body:JSON.stringify({jsonrpc:'2.0',id:1,method:'tools/list',params:{}})});
 env={...env,MCP_ENDPOINT_PATH:'/v1',MCP_PUBLIC_ORIGIN:'https://convert.mcp.arken.gg',PAYMENTS_ENABLED:'false'};
 for(const app of [worker,createDefaultResourceWorker()]){
  const response=await app.fetch(request(),env),body=await response.json();
  if(response.status!==200||body.result.tools.length!==8)throw Error('Production resource discovery changed');
 }
 return {passed:true,productionEntry:true,defaultExport:true,toolCount:8,checks:2};
}
