export {ConversionPaymentLedger as SubjectLedger} from './journal_entry.js';
import current,{createJournalWorker} from './journal_entry.js';
import unwrapped from './production_worker.js';
export const createReportSubject=clock=>createJournalWorker({clock});
export async function provePublicPassthrough(worker,env){
 env={...env,MCP_ENDPOINT_PATH:'/v1',MCP_PUBLIC_ORIGIN:'https://convert.mcp.arken.gg',PAYMENTS_ENABLED:'false'};
 const headers={'content-type':'application/json',accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'};
 const packets=[{method:'initialize',params:{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'fixture',version:'1'}}},
  {method:'tools/list',params:{}},{method:'tools/call',params:{name:'prepare_upload',arguments:{source_bytes:22,input_format:'tga',output_format:'png'}}}];
 let toolCount=0,checks=0;
 for(const packet of packets){
  const request=()=>new Request('https://convert.mcp.arken.gg/v1',{method:'POST',headers,body:JSON.stringify({jsonrpc:'2.0',id:1,...packet})});
  const before=await unwrapped.fetch(request(),env),text=await before.text();
  for(const app of [worker,current]){
   const after=await app.fetch(request(),env);
   if(before.status!==after.status||JSON.stringify([...before.headers])!==JSON.stringify([...after.headers])||text!==await after.text())throw Error('Source public response changed');
   ++checks;
  }
  if(packet.method==='tools/list')toolCount=JSON.parse(text).result.tools.length;
 }
 if(toolCount!==8)throw Error('Source command inventory changed');
 return {passed:true,productionEntry:false,currentSourceEntry:true,defaultExport:true,toolCount,checks};
}
