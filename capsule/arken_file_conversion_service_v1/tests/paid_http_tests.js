import {createWorker} from './worker.js';
import {createPaidConversion} from './paid_conversion.js';
import {Bucket} from './r2_bucket_fixture.js';
import {paidConversionPacket,planSandboxRequest} from './runtime.js';
import sandbox from './sandbox_worker.js';
import {chainFixture} from './settlement_chain_fixture.js';
const check=(value,label)=>{if(!value)throw Error(label);};
const token='test-only-paid-conversion-access-token-long-enough';
const headers={authorization:`Bearer ${token}`,'content-type':'application/json',accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'};
const request=(method,params,id=1)=>new Request('https://converter.example/mcp',{method:'POST',headers,body:JSON.stringify({jsonrpc:'2.0',id,method,params})});
const tga=new Uint8Array([0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,30,20,10,255]);
const hex=bytes=>Array.from(new Uint8Array(bytes),x=>x.toString(16).padStart(2,'0')).join('');
export const paidHttp={async test(_controller,bindings){
  if(bindings.PHASE!=='write')return;
  const health=await (await sandbox.fetch(new Request('https://converter.example/health'),{})).json();
  check(health.displayName==='Arken Convert'&&health.mode==='x402-sandbox'&&health.charging===false,'sandbox entrypoint identity');
  const env={ACCESS_TOKEN:token,FILES:new Bucket(),PAYMENT_LEDGER:bindings.LEDGER,RELEASE_ID:'07'.repeat(32)};
  let now=Math.floor(Date.now()/1000),verifies=0,settles=0,reject=false,lose=false,replaceRelease=false,corruptAfterVerifyId=null;
  let beforeVerify=null;
  let chain=null,chainReads=0;
  const adapter=createPaidConversion({clock:()=>String(now),readChain:async message=>{chainReads++;return chain?chain(message):{status:503,body:''};},send:async message=>{
    const sent=JSON.parse(message.body),payer=sent.paymentPayload.payload.authorization.from;
    if(message.url.endsWith('/verify')){
      verifies++;if(beforeVerify){const before=beforeVerify;beforeVerify=null;await before();}if(replaceRelease)env.RELEASE_ID='08'.repeat(32);
      if(corruptAfterVerifyId){const key=[...env.FILES.values.keys()].find(k=>k.includes(corruptAfterVerifyId)&&k.endsWith('/input'));env.FILES.values.get(key).bytes[2]=0;}
      return {status:200,body:JSON.stringify({isValid:!reject,payer})};
    }
    settles++;
    const outputs=[...env.FILES.values.entries()].filter(([key])=>key.endsWith('/output'));
    check(outputs.length>0,'settled before private output');
    const receipt=JSON.parse(outputs.at(-1)[1].customMetadata.receipt);
    check((await app.fetch(new Request(receipt.download_url),env)).status===403,'unsettled output was downloadable');
    if(lose)throw Error('fixture_settlement_reply_lost');
    return {status:200,body:JSON.stringify({success:true,payer,network:'eip155:84532',transaction:'0x'+'af'.repeat(32)})};
  }});
  const observations=[],emitCallSummary=value=>observations.push(value);
  const app=createWorker(adapter,{emitCallSummary}),peer=createWorker(adapter,{emitCallSummary});
  const rpc=async(method,params,id=1)=>(await app.fetch(request(method,params,id),env)).json();
  const call=async(name,args,payment=null)=>rpc('tools/call',{name,arguments:args,...(payment?{_meta:{'x402/payment':payment}}:{})});
  const upload=async(bytes=tga)=>{
    const prepared=await call('prepare_upload',{source_bytes:bytes.length,input_format:'tga',output_format:'png'}),lease=prepared.result.structuredContent;
    check((await app.fetch(new Request(lease.upload_url,{method:'PUT',headers,body:bytes}),env)).status===204,'paid upload');return lease;
  };
  const challenge=async lease=>{
    const result=await call('convert_image',{file_id:lease.file_id});
    check(result.result.isError&&result.result.structuredContent.accepts[0].network==='eip155:84532','payment challenge absent');
    check(JSON.stringify(JSON.parse(result.result.content[0].text))===JSON.stringify(result.result.structuredContent),'challenge formats disagree');
    const required=result.result.structuredContent;
    return {x402Version:2,resource:required.resource,accepted:required.accepts[0],payload:{signature:'0x'+'00'.repeat(65),authorization:{
      from:'0x'+'12'.repeat(20),to:required.accepts[0].payTo,value:required.accepts[0].amount,
      validAfter:String(now-10),validBefore:String(now+50),nonce:'0x'+lease.file_id}}};
  };
  const init=await rpc('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'test',version:'1'}});
  check(init.result.serverInfo.name==='Arken Convert','public MCP name');
  const listed=await rpc('tools/list',{});check(listed.result.tools.find(t=>t.name==='convert_image').annotations.openWorldHint,'paid network annotation');
  const lease=await upload(),payment=await challenge(lease);
  const quoteRecords=observations.slice(-3),quoteContext=quoteRecords[1],quoteLink=quoteRecords[2];
  check(quoteRecords[0].schema==='arken.mcp.call.v1'&&quoteContext.schema==='arken.mcp.cost-context.v1'&&quoteLink.schema==='arken.mcp.operation-cost-link.v1','native quote observation records');
  check(quoteContext.service==='convert'&&quoteContext.command==='conversion.v1.convert_image'&&quoteContext.exchangeKind==='quote'&&quoteContext.release===env.RELEASE_ID&&quoteContext.httpStatus===200,'quote cost classification');
  check(Object.keys(quoteContext).length===7&&Object.keys(quoteLink).length===3&&quoteLink.service==='convert'&&/^[a-f0-9]{64}$/.test(quoteLink.operation)&&!JSON.stringify(quoteRecords).includes(lease.file_id),'quote observation leaked file identity');
  check((env.FILES.bodyReads.input||0)===0,'payment challenge opened image body');
  const free=createWorker();
  check((await (await free.fetch(request('tools/call',{name:'convert_image',arguments:{file_id:lease.file_id}}),env)).json()).result.isError,'preview bypassed sandbox namespace');
  check(verifies===0&&settles===0&&env.FILES.outputWrites===0,'unpaid conversion performed work');
  const wrong=structuredClone(payment);wrong.accepted.amount='1';
  check((await call('convert_image',{file_id:lease.file_id},wrong)).result.isError,'changed price accepted');
  check(verifies===0&&![...env.FILES.values.keys()].some(k=>k.endsWith('/payment')),'invalid payment bound file');
  const complete=await call('convert_image',{file_id:lease.file_id},payment);
  check(complete.result._meta?.['x402/payment-response'].success,'paid receipt absent');
  const receipt=complete.result.structuredContent;
  check(!JSON.stringify(complete).includes('arken.mcp.cost-context.v1')&&!JSON.stringify(complete).includes('arken.mcp.operation-cost-link.v1'),'private observations leaked into public receipt');
  check(receipt.width===1&&receipt.height===1&&verifies===1&&settles===1,'paid conversion identity');
  check(env.FILES.bodyReads.input===1&&(env.FILES.bodyReads.output||0)===0,'paid metadata inspection opened image bodies');
  const download=await app.fetch(new Request(receipt.download_url),env),bytes=new Uint8Array(await download.arrayBuffer());
  check(download.status===200&&bytes[0]===137&&bytes[1]===80,'paid PNG download');
  check(env.FILES.bodyReads.output===1,'download opened output more than once');
  check(hex(await crypto.subtle.digest('SHA-256',bytes))===receipt.sha256,'receipt digest differs from bytes');
  now++;
  const repeated=await call('convert_image',{file_id:lease.file_id},payment);
  check(JSON.stringify(repeated.result)===JSON.stringify(complete.result)&&settles===1&&env.FILES.outputWrites===1,'paid reply replay changed');
  check(observations.at(-2).exchangeKind==='operation'&&JSON.stringify(observations.at(-1))===JSON.stringify(quoteLink),'replay cost link changed or was classified as a quote');
  env.RELEASE_ID='09'.repeat(32);
  check(JSON.stringify((await call('convert_image',{file_id:lease.file_id})).result)===JSON.stringify(complete.result),'completed receipt lost across release');
  check(observations.at(-2).release===env.RELEASE_ID&&observations.at(-2).exchangeKind==='operation'&&JSON.stringify(observations.at(-1))===JSON.stringify(quoteLink),'replay observation lost executing release or stable operation');
  env.RELEASE_ID='07'.repeat(32);
  const other=structuredClone(payment);other.payload.authorization.from='0x'+'13'.repeat(20);
  check((await call('convert_image',{file_id:lease.file_id},other)).result.isError&&settles===1,'second payer rebound same file');

  const lost=await upload(),lostPayment=await challenge(lost);env.FILES.loseOutputAcknowledgement=true;
  check((await app.fetch(request('tools/call',{name:'convert_image',arguments:{file_id:lost.file_id},_meta:{'x402/payment':lostPayment}}),env)).status===500,'output acknowledgement loss fixture');
  check(settles===1&&env.FILES.outputWrites===2,'uncertain output charged prematurely');
  const recovered=await call('convert_image',{file_id:lost.file_id});
  check(recovered.result._meta?.['x402/payment-response'].success&&settles===2&&env.FILES.outputWrites===2,'output recovery re-executed or failed');

  const uncertain=await upload(),uncertainPayment=await challenge(uncertain);lose=true;
  const pending=await call('convert_image',{file_id:uncertain.file_id},uncertainPayment);lose=false;
  check(pending.result.structuredContent.code==='settlement_pending_reconciliation'&&!pending.result.structuredContent.accepts,'unknown settlement requested payment again');
  const before=settles;await call('convert_image',{file_id:uncertain.file_id});check(settles===before,'uncertain HTTP retry settled again');
  const unknownOutput=[...env.FILES.values.entries()].find(([key])=>key.includes(uncertain.file_id)&&key.endsWith('/output'));
  check((await app.fetch(new Request(JSON.parse(unknownOutput[1].customMetadata.receipt).download_url),env)).status===403,'unknown payment released bytes');
  chain=chainFixture(uncertainPayment,now);const readsBefore=chainReads;
  const recoveries=await Promise.all([app,peer].map(host=>host.fetch(request('tools/call',{name:'convert_image',arguments:{file_id:uncertain.file_id}}),env).then(r=>r.json())));
  check(recoveries.every(r=>r.result._meta?.['x402/payment-response']?.success)&&settles===before,'reconciliation failed or charged again');
  check(JSON.stringify(recoveries[0].result)===JSON.stringify(recoveries[1].result)&&chainReads>=readsBefore+3&&chainReads<=readsBefore+6,'concurrent chain observers disagreed');
  check((await app.fetch(new Request(recoveries[0].result.structuredContent.download_url),env)).status===200,'reconciled output not downloadable');
  const readsAfter=chainReads;await call('convert_image',{file_id:uncertain.file_id});
  check(chainReads===readsAfter&&settles===before,'fulfilled repeat performed chain or settlement work');chain=null;
  const invalid=await upload(),invalidPayment=await challenge(invalid);reject=true;
  check((await call('convert_image',{file_id:invalid.file_id},invalidPayment)).result.structuredContent.code==='conversion_failed_no_charge','invalid verification result');reject=false;
  check(settles===before,'invalid verification settled');
  const corrupt=await upload(new Uint8Array(22)),corruptPayment=await challenge(corrupt);
  check((await call('convert_image',{file_id:corrupt.file_id},corruptPayment)).result.structuredContent.code==='conversion_failed_no_charge','decoder rejection not no-charge');
  check(settles===before,'failed conversion settled');
  const changed=await upload(),changedPayment=await challenge(changed);replaceRelease=true;
  check((await call('convert_image',{file_id:changed.file_id},changedPayment)).result.isError,'changed executor release accepted');replaceRelease=false;env.RELEASE_ID='07'.repeat(32);
  check(settles===before,'release mismatch settled');
  const corruptSource=await upload(),corruptSourcePayment=await challenge(corruptSource);corruptAfterVerifyId=corruptSource.file_id;
  check((await call('convert_image',{file_id:corruptSource.file_id},corruptSourcePayment)).result.structuredContent.code==='conversion_failed_no_charge','changed source bytes accepted');corruptAfterVerifyId=null;
  check(settles===before,'source digest mismatch settled');
  const concurrent=await upload(),concurrentPayment=await challenge(concurrent),priorSettles=settles,priorVerifies=verifies,priorOutputs=env.FILES.outputWrites;
  const parallel=await Promise.all([app,peer].map(host=>host.fetch(request('tools/call',{name:'convert_image',arguments:{file_id:concurrent.file_id},_meta:{'x402/payment':concurrentPayment}}),env).then(r=>r.json())));
  check(parallel.some(r=>r.result._meta?.['x402/payment-response']?.success),'concurrent request never fulfilled');
  check(settles===priorSettles+1&&verifies===priorVerifies+1&&env.FILES.outputWrites===priorOutputs+1,'concurrent HTTP request duplicated effects');
  const large=new Uint8Array(18+1024*1024*4);large[2]=2;large[13]=4;large[15]=4;large[16]=32;large[17]=40;
  for(let i=18;i<large.length;i+=4){large[i]=30;large[i+1]=20;large[i+2]=10;large[i+3]=255;}
  const largeLease=await upload(large),largePayment=await challenge(largeLease);
  const requestKey=[...env.FILES.values.keys()].find(k=>k.includes(largeLease.file_id)&&k.endsWith('/request'));
  const staleHints=await (await env.FILES.get(requestKey)).json();staleHints.args.codec.output=5;await env.FILES.put(requestKey,JSON.stringify(staleHints));
  const largeResult=await call('convert_image',{file_id:largeLease.file_id},largePayment);
  check(largeResult.result._meta?.['x402/payment-response']?.success&&largeResult.result.structuredContent.width===1024&&largeResult.result.structuredContent.height===1024,'full-limit paid conversion');
  const actualLarge=new Uint8Array(await (await app.fetch(new Request(largeResult.result.structuredContent.download_url),env)).arrayBuffer());
  check(actualLarge[0]===137&&actualLarge[1]===80,'stored codec hint overrode C++ options');
  const malformed=planSandboxRequest('{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"conversion.v1.convert_image","arguments":{"file_id":"'+'11'.repeat(32)+'"},"_meta":{"x402/payment":{},"x402/payment":{}}}}');
  check(malformed.kind==='response','duplicate metadata key accepted');
  // Exact quote owner/input/options changes reject against the retained binding.
  const bindingRecord=JSON.parse(await (await env.FILES.get([...env.FILES.values.keys()].find(k=>k.includes(lease.file_id)&&k.endsWith('/payment')))).text());
  for(const field of ['tenant','input','options','operation']){
    const quote=structuredClone(bindingRecord.quote);quote[field]='ee'.repeat(32);
    const outcome=paidConversionPacket(JSON.stringify({kind:'bind',quote,payment:null,binding:JSON.stringify(bindingRecord),now:String(now)}));
    check(outcome.result?.isError,'immutable quote field changed');
  }
  const gate=()=>{let enter,resume;const entered=new Promise(r=>{enter=r;}),released=new Promise(r=>{resume=r;});return {entered,resume,wait:()=>{enter();return released;}};};
  const peerCall=async fileId=>(await peer.fetch(request('tools/call',{name:'convert_image',arguments:{file_id:fileId}}),env)).json();
  const stalled=await upload(),stalledPayment=await challenge(stalled),verifyGate=gate();beforeVerify=verifyGate.wait;
  const v0=verifies,s0=settles,delayed=call('convert_image',{file_id:stalled.file_id},stalledPayment);
  await verifyGate.entered;
  check((await peerCall(stalled.file_id)).result.structuredContent.code==='operation_pending'&&verifies===v0+1,'active verify repeated');
  now+=35;
  const resumedVerify=await peerCall(stalled.file_id);check(resumedVerify.result._meta?.['x402/payment-response']?.success&&verifies===v0+2&&settles===s0+1,'elapsed verify failed recovery');
  verifyGate.resume();const lateVerify=await delayed;
  check(JSON.stringify(lateVerify.result)===JSON.stringify(resumedVerify.result)&&settles===s0+1,'late verification changed settled result');

  const compute=await upload(),computePayment=await challenge(compute),computeGate=gate(),o0=env.FILES.outputWrites,s1=settles;
  env.FILES.beforeOutput=async(value,options)=>{await computeGate.wait();value[0]=0;options.customMetadata.receipt='invalid late candidate';};
  const delayedCompute=call('convert_image',{file_id:compute.file_id},computePayment);await computeGate.entered;
  check((await peerCall(compute.file_id)).result.structuredContent.code==='operation_pending'&&env.FILES.outputWrites===o0,'active compute repeated');
  now+=30;
  const resumedCompute=await peerCall(compute.file_id);check(resumedCompute.result._meta?.['x402/payment-response']?.success&&env.FILES.outputWrites===o0+1&&settles===s1+1,'elapsed compute failed recovery');
  computeGate.resume();const lateCompute=await delayedCompute;
  check(JSON.stringify(lateCompute.result)===JSON.stringify(resumedCompute.result)&&env.FILES.outputWrites===o0+1&&settles===s1+1,'late compute overwrote winner or charged again');
  const retainedBytes=new Uint8Array(await (await app.fetch(new Request(resumedCompute.result.structuredContent.download_url),env)).arrayBuffer());
  check(retainedBytes[0]===137&&hex(await crypto.subtle.digest('SHA-256',retainedBytes))===resumedCompute.result.structuredContent.sha256,'late candidate replaced authoritative bytes');

  const expired=await upload(),expiredPayment=await challenge(expired),expiryGate=gate(),s2=settles;beforeVerify=expiryGate.wait;
  const delayedExpired=call('convert_image',{file_id:expired.file_id},expiredPayment);await expiryGate.entered;now+=50;
  check((await peerCall(expired.file_id)).result.structuredContent.code==='conversion_failed_no_charge'&&settles===s2,'expiry not a durable no-charge outcome');
  expiryGate.resume();check((await delayedExpired).result.structuredContent.code==='conversion_failed_no_charge'&&settles===s2,'late expired callback reopened operation');
  console.log('Paid HTTP acceptance: real C++ PNG, bounded verify/compute recovery, immutable output, late callbacks, expiry, one settlement, private download and identity rejections. R2/facilitator are fixtures.');
}};
