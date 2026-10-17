// Complete guarded production HTTP across two native-owned Workerd lifetimes.
import {createResourceWorker,createDefaultResourceWorker,resourceGuardEvidence} from './resource_subject.js';
import {createProductionStartup} from './production_startup.js';
import {Bucket} from './r2_bucket_fixture.js';
import {proveConversionJournalHttp} from './payment_journal_http.js';
import {saveObjects,loadObjects} from './maintenance_state.js';
import fixtureText from './startup-fixtures.json';
import maximumPng from './maximum-input.png';
import maximumText from './resource-fixture.json';
export {ConversionPaymentLedger} from './resource_subject.js';
export {JournalLedger} from './payment_journal_http.js';
export {MaintenanceState} from './maintenance_state.js';
const headers={'content-type':'application/json',accept:'application/json,text/event-stream','mcp-protocol-version':'2025-11-25'};
const origin='https://convert.mcp.arken.gg';
export default {async test(_controller,bindings){
 let checks=0,verifies=0,settles=0,mode='success';const check=(v,label)=>{checks++;if(!v)throw Error(label);};
 const actor=bindings.FIXTURE_STATE.get(bindings.FIXTURE_STATE.idFromName('proof')),instance=await actor.enter(bindings.PHASE);
 const f=JSON.parse(fixtureText),maximum=JSON.parse(maximumText),store=new Bucket(),now=1000;
 if(bindings.PHASE==='read')store.values=await loadObjects(actor,'bytes');
 const startup=createProductionStartup({clock:()=>String(now),fetcher:async(url,options)=>{
  check(options.redirect==='manual','startup redirect policy');
  if(url.endsWith('/supported'))return options.headers.Authorization?new Response(f.supported.body):new Response('',{status:401});
  check(url==='https://api.developer.coinbase.com/rpc/v1/base/'+f.configuration.rpcClientKey,'unexpected startup provider');
  const rpc=JSON.parse(f.rpc.body);rpc.find(r=>r.id===2).result.timestamp='0x'+now.toString(16);return Response.json(rpc);
 }});
 const env={...bindings,FILES:store,ACCESS_TOKEN:f.configuration.downloadSecret,PAYMENT_LEDGER:bindings.LEDGER,
  RELEASE_ID:f.configuration.releaseId,PAYMENTS_ENABLED:'true',MCP_ENDPOINT_PATH:'/v1',MCP_PUBLIC_ORIGIN:origin,
  CDP_API_KEY_ID:f.configuration.keyId,CDP_API_KEY_SECRET:f.configuration.keySecret,CDP_RPC_CLIENT_KEY:f.configuration.rpcClientKey};
 const worker=createResourceWorker({startup,clock:()=>String(now),readChain:async()=>{throw Error('fixture chain unavailable');},fetcher:async(url,options)=>{
  check(url.startsWith('https://api.cdp.coinbase.com/platform/v2/x402/'),'unexpected payment provider');
  const body=JSON.parse(options.body),payer=body.paymentPayload.payload.authorization.from;
  if(url.endsWith('/verify')){verifies++;return Response.json({isValid:true,payer});}
  check(url.endsWith('/settle'),'unexpected payment method');settles++;
  return mode==='uncertain'?Response.json({},{status:503}):Response.json({success:true,payer,network:'eip155:8453',transaction:'0x'+'af'.repeat(32)});
 }});
 const request=(method,params)=>new Request(origin+'/v1',{method:'POST',headers,body:JSON.stringify({jsonrpc:'2.0',id:1,method,params})});
 const call=async(name,args,payment)=>{
  const response=await worker.fetch(request('tools/call',{name,arguments:args,...(payment?{_meta:{'x402/payment':payment}}:{})}),env);
  check(response.status===200,'MCP HTTP status');return (await response.json()).result;
 };
 const discovery=await createDefaultResourceWorker().fetch(request('tools/list',{}),env);
 check(discovery.status===200&&(await discovery.json()).result.tools.length===8,'complete guarded default discovery');
 let saved;
 if(bindings.PHASE==='write'){
  check(maximumPng.byteLength===8388608&&maximum.inputBytes===maximumPng.byteLength,'maximum encoded input');
  const process=async()=>{
   const prepared=await call('prepare_upload',{source_bytes:maximumPng.byteLength,input_format:'png',output_format:'jpeg'}),ticket=prepared.structuredContent;
   check(ticket,'upload preparation '+JSON.stringify(prepared));
   check((await worker.fetch(new Request(ticket.upload_url,{method:'PUT',body:maximumPng}),env)).status===204,'maximum upload');
   const args={file_id:ticket.file_id,file_token:ticket.file_token},quoted=await call('convert_image',args),offer=quoted.structuredContent;
   check(offer?.x402Version===2,'production quote '+JSON.stringify(quoted));
   const complete=await call('convert_image',args,{x402Version:2,resource:offer.resource,accepted:offer.accepts[0],payload:{signature:'0x'+'00'.repeat(65),authorization:{
    from:'0x'+'34'.repeat(20),to:offer.accepts[0].payTo,value:offer.accepts[0].amount,validAfter:String(now-10),validBefore:String(now+50),nonce:'0x'+ticket.file_id}}});
   return {args,complete};
  };
  const complete=await process();check(!complete.complete.isError,'predecessor paid conversion '+JSON.stringify(complete.complete));
  mode='uncertain';const uncertain=await process();check(uncertain.complete.isError,'uncertain conversion disclosed result');
  saved={...complete,uncertainArgs:uncertain.args,uncertain:uncertain.complete};
  await actor.put('requests',saved);await saveObjects(actor,store.values,'bytes');
 }else saved=await actor.get('requests');
 const writes=store.outputWrites,v=verifies,s=settles;
 const replay=await call('convert_image',saved.args);
 check(JSON.stringify(replay)===JSON.stringify(saved.complete),'completed conversion replay changed receipt '+JSON.stringify(replay));
 const response=await worker.fetch(new Request(replay.structuredContent.download_url),env),bytes=await response.arrayBuffer();
 const sha=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
 check(response.status===200&&sha===maximum.outputSha256&&sha===f.largeJpeg.sha256,'retained maximum JPEG native parity');
 const uncertain=await call('convert_image',saved.uncertainArgs);
 check(uncertain.isError&&uncertain.structuredContent?.code==='settlement_pending_reconciliation'&&!uncertain._meta?.['x402/payment-response'],'uncertain conversion lost '+JSON.stringify(uncertain));
 const pending=[...store.values.entries()].find(([key])=>key.includes(saved.uncertainArgs.file_id)&&key.endsWith('/output'));
 check(pending,'uncertain output was never prepared');
 const hidden=JSON.parse(pending[1].customMetadata.receipt).download_url;
 const withheld=await worker.fetch(new Request(hidden),env);
 check(withheld.status===403&&(await withheld.json()).error==='payment_not_fulfilled','uncertain output disclosed');
 check(store.outputWrites===writes&&verifies===v&&settles===s,'conversion replay repeated effects');
 const journal=await proveConversionJournalHttp(bindings);
 console.log('ARKEN_MAINTENANCE_RESTART='+JSON.stringify({status:'passed',service:'convert',phase:bindings.PHASE,instance,checks,journal,
  guard:resourceGuardEvidence(),nativeByteParity:true,completedReplay:true,uncertainWithheld:true,
  providerVerifies:verifies,providerSettles:settles,outputWrites:store.outputWrites,maximumPaidHttp:true,
  sourceBytes:maximumPng.byteLength,outputWidth:1024,outputHeight:1024}));
}};
