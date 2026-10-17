import worker,{createWorker} from './worker.js';
import {Bucket} from './r2_bucket_fixture.js';
import {readBoundedBody} from './bounded_body.js';
export {payments} from './payment_worker_tests.js';
const token='test-only-access-token-at-least-32-characters';
const check=(value,message)=>{if(!value)throw Error(message);};
const env=()=>({ACCESS_TOKEN:token,FILES:new Bucket()});
const headers={authorization:`Bearer ${token}`,'content-type':'application/json',
  accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'};
const request=(data,extra={})=>new Request('https://converter.example/mcp',{
  method:'POST',headers:{...headers,...extra},body:typeof data==='string'?data:JSON.stringify(data)});
const rpc=(method,params={},id=1)=>({jsonrpc:'2.0',id,method,params});
const call=(name,args,id=1)=>rpc('tools/call',{name,arguments:args},id);
// Independent minimal 1x1 BGRA TGA. No external/private assets.
const tga=new Uint8Array([0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,30,20,10,255]);
export const protocol={async test(){
  const e=env();
  // Observe through the factory's owned emitter seam, never by patching
  // console, runtime, payment or storage authorities. This protects privacy
  // and response neutrality; domain classification is tested natively.
  const summaries=[],observed=createWorker(null,{emitCallSummary:value=>summaries.push(value)});
  const failing=createWorker(null,{emitCallSummary:()=>{throw Error('private test diagnostic');}});
  const privateCall=call('convert_image',{unexpected:'PRIVATE_INPUT'});
  privateCall.id='PRIVATE_INPUT';privateCall.params._meta={private:'PRIVATE_INPUT'};
  const baseline=await worker.fetch(request(privateCall),e),logged=await observed.fetch(request(privateCall),e),unlogged=await failing.fetch(request(privateCall),e);
  const body=await baseline.text();
  check(await logged.text()===body&&await unlogged.text()===body&&logged.status===baseline.status&&unlogged.status===baseline.status,'logging changed public response');
  check(JSON.stringify([...logged.headers])===JSON.stringify([...baseline.headers])&&e.FILES.values.size===0,'logging changed headers or storage');
  check(summaries.length===1&&Object.keys(summaries[0]).length===5&&summaries[0].tool==='conversion.v1.convert_image'&&summaries[0].httpStatus===baseline.status&&!JSON.stringify(summaries).includes('PRIVATE_INPUT'),'private native summary projection');
  await observed.fetch(request(call('PRIVATE_INPUT',{})),e);
  check(summaries.length===2&&summaries[1].tool==='unknown','unknown name was logged');
  await observed.fetch(request('{'),e);
  check(summaries.length===2,'malformed body generated a summary');
  const failedTarget=call('convert_image',{file_id:'1'.repeat(64),file_token:'2'.repeat(64)});
  const costReply=await observed.fetch(request(failedTarget),{...e,RELEASE_ID:'a'.repeat(64)});
  const costBody=await costReply.text(),costLogs=summaries.slice(-2);
  check(costLogs[0].schema==='arken.mcp.call.v1'&&costLogs[1].schema==='arken.mcp.cost-context.v1'&&
    costLogs[1].command==='conversion.v1.convert_image'&&costLogs[1].httpStatus===costReply.status&&
    !JSON.stringify(costLogs).includes('2'.repeat(64))&&!costBody.includes('cost-context'),'Failed attempt cost privacy');
  const init=await (await worker.fetch(request(rpc('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'test',version:'1'}})),e)).json();
  check(init.result.serverInfo.name==='Arken Convert','initialize');
  const listed=await (await worker.fetch(request(rpc('tools/list')),e)).json();
  check(listed.result.tools.length===8,'generated tool count');
  const upload=listed.result.tools.find(t=>t.name==='prepare_upload');
  check(upload.inputSchema.properties.input_format.enum.includes('dds'),'generated input enum');
  check(!upload.inputSchema.properties.output_format.enum.includes('dds'),'unimplemented output advertised');
  const exact=await worker.fetch(request('{"jsonrpc":"2.0","id":9007199254740993,"method":"ping"}'),e);
  check((await exact.text()).includes('"id":9007199254740993'),'MCP integer ID narrowed');
  const notification=await worker.fetch(request({jsonrpc:'2.0',method:'notifications/initialized'}),e);
  check(notification.status===202&&(await notification.text())==='','notification response');
  check((await worker.fetch(new Request('https://converter.example/mcp',{headers}),e)).status===405,'GET without SSE');
  check((await worker.fetch(request(rpc('ping'),{origin:'https://untrusted.example'}),e)).status===403,'Origin check');
  check((await worker.fetch(request(rpc('ping'),{authorization:'Bearer incorrect'}),e)).status===401,'auth check');
  check((await worker.fetch(request(rpc('ping'),{'mcp-protocol-version':'2030-01-01'}),e)).status===400,'protocol version');
  const noVersion=request(rpc('ping'));noVersion.headers.delete('mcp-protocol-version');
  check((await worker.fetch(noVersion,e)).status===400,'missing negotiated version');
  const malformed=await (await worker.fetch(request('{'),e)).json();check(malformed.error.code===-32700,'JSON error');
  for(const args of [
    {source_bytes:0,input_format:'png',output_format:'png'},
    {source_bytes:8388609,input_format:'png',output_format:'png'},
    {source_bytes:22,input_format:'unknown',output_format:'png'},
    {source_bytes:22,input_format:'png',output_format:'dds'},
    {source_bytes:22,input_format:'png',output_format:'png',jpeg_quality:90},
    {source_bytes:22,input_format:'png',output_format:'png',jpeg_quality:95},
    {source_bytes:22,input_format:'png',output_format:'png',surprise:1}]) {
    const result=await (await worker.fetch(request(call('prepare_upload',args)),e)).json();
    check(result.error?.code===-32602,'invalid options reached storage');
  }
  check(e.FILES.values.size===0,'rejected input mutated storage');
  check((await worker.fetch(new Request('https://converter.example/mcp',{method:'POST',headers,body:new Uint8Array([195,40])}),e)).status===400,'invalid UTF-8');
}};
export const admission={async test(){
  const e=env();
  check((await worker.fetch(request(rpc('ping')),{})).status===503,'missing credential must fail closed');
  const noStorage=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),{ACCESS_TOKEN:token})).json();
  check(noStorage.result.isError===true,'missing storage must fail closed');
  const prepared=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  const lease=prepared.result.structuredContent;
  let begin,release;
  const entered=new Promise(resolve=>{begin=resolve;}),released=new Promise(resolve=>{release=resolve;});
  const stream=new ReadableStream({async pull(controller){begin();await released;controller.enqueue(tga);controller.close();}},{highWaterMark:0});
  const pending=worker.fetch(new Request(lease.upload_url,{method:'PUT',headers,body:stream}),e);
  await entered;
  // Zero eager buffering makes the first read prove admission. No production object is patched.
  const busy=await worker.fetch(request(rpc('ping')),e);
  release();await pending;
  check(busy.status===429,'concurrent request buffered during upload');
  check((await worker.fetch(request(rpc('ping')),e)).status===200,'admission not released');
  const tooLarge=await worker.fetch(request(' '.repeat(16385)),e);
  check(tooLarge.status===413,'MCP byte limit');
  const again=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  const input=again.result.structuredContent.upload_url;
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:new Uint8Array(23)}),e)).status===413,'upload byte limit');
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:new Uint8Array(21)}),e)).status===400,'short upload');
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:tga}),e)).status===204,'failed upload poisoned admission');
}};
export const conversion={async test(){
  const e=env();
  const prepared=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  const lease=prepared.result.structuredContent;
  check(lease.file_id.length===64&&lease.upload_url.endsWith('/input'),'upload capability');
  const put=()=>worker.fetch(new Request(lease.upload_url,{method:'PUT',headers:{authorization:`Bearer ${token}`},body:tga}),e);
  check((await put()).status===204,'upload');check((await put()).status===409,'immutable upload');
  const mismatch=await (await worker.fetch(request(call('png_to_jpeg',{file_id:lease.file_id})),e)).json();
  check(mismatch.result.isError===true&&JSON.parse(mismatch.result.content[0].text).code==='conversion_format_mismatch','named format mismatch admitted');
  const converted=await (await worker.fetch(request(call('tga_to_png',{file_id:lease.file_id})),e)).json();
  const receipt=converted.result.structuredContent;
  check(receipt.width===1&&receipt.height===1&&receipt.media_type==='image/png','C++ output metadata');
  const repeated=await (await worker.fetch(request(call('convert_image',{file_id:lease.file_id})),e)).json();
  check(JSON.stringify(repeated.result.structuredContent)===JSON.stringify(receipt),'repeat changed receipt');
  const download=await worker.fetch(new Request(receipt.download_url),e);
  check(download.status===200,'signed download');
  const bytes=new Uint8Array(await download.arrayBuffer());check(bytes[0]===137&&bytes[1]===80&&bytes[2]===78,'actual PNG output');
  check(bytes.length===receipt.output_bytes,'download extent');
  const changed=new URL(receipt.download_url);changed.searchParams.set('token','0'.repeat(64));
  check((await worker.fetch(new Request(changed),e)).status===403,'forged download');
  const ownerChanged={ACCESS_TOKEN:'different-owner-token-at-least-32-characters',FILES:e.FILES};
  const foreign=await (await worker.fetch(request(call('convert_image',{file_id:lease.file_id}),{'authorization':`Bearer ${ownerChanged.ACCESS_TOKEN}`}),ownerChanged)).json();
  check(foreign.result.isError===true,'cross-owner file access');
  const recordKey=[...e.FILES.values.keys()].find(k=>k.endsWith('/request'));
  const record=await (await e.FILES.get(recordKey)).json();record.expiresAt=1;await e.FILES.put(recordKey,JSON.stringify(record));
  check((await worker.fetch(new Request(receipt.download_url),e)).status===404,'expired file retained access');
}};
export const largeImage={async test(){
  const e=env(),bytes=new Uint8Array(18+1024*1024*4);
  bytes[2]=2;bytes[13]=4;bytes[15]=4;bytes[16]=32;bytes[17]=40;
  for(let i=18;i<bytes.length;i+=4){bytes[i]=30;bytes[i+1]=20;bytes[i+2]=10;bytes[i+3]=255;}
  const prepared=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:bytes.length,input_format:'tga',output_format:'png'})),e)).json();
  const lease=prepared.result.structuredContent;
  check((await worker.fetch(new Request(lease.upload_url,{method:'PUT',headers,body:bytes}),e)).status===204,'large upload');
  const result=await (await worker.fetch(request(call('convert_image',{file_id:lease.file_id})),e)).json();
  check(result.result.structuredContent.width===1024&&result.result.structuredContent.height===1024,'large C++ conversion');
}};
export const fragmentedUpload={async test(){
  const e=env();
  const prepared=await (await worker.fetch(request(call('prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  let index=0;const reused=new Uint8Array(1);
  const body=new ReadableStream({pull(controller){if(index===tga.length){controller.close();return;}reused[0]=tga[index++];controller.enqueue(reused);}},{highWaterMark:0});
  check((await worker.fetch(new Request(prepared.result.structuredContent.upload_url,{method:'PUT',headers,body}),e)).status===204,'fragmented upload');
  const stored=[...e.FILES.values.entries()].find(([key])=>key.endsWith('/input'))[1].bytes;
  check(stored.length===tga.length&&stored.every((byte,i)=>byte===tga[i]),'upload retained mutable stream chunks');
  const capacity=8388608,chunk=new Uint8Array(1024).fill(37);let chunks=0;
  const large=await (await worker.fetch(request(call('prepare_upload',{source_bytes:capacity,input_format:'png',output_format:'png'})),e)).json();
  const stream=new ReadableStream({pull(controller){if(chunks===8192){controller.close();return;}chunks++;controller.enqueue(chunk);}},{highWaterMark:0});
  check((await worker.fetch(new Request(large.result.structuredContent.upload_url,{method:'PUT',headers,body:stream}),e)).status===204,'maximum fragmented upload');
  const item=[...e.FILES.values.entries()].find(([key])=>key.includes(large.result.structuredContent.file_id)&&key.endsWith('/input'))[1];
  check(item.bytes.length===capacity&&item.bytes[0]===37&&item.bytes[capacity-1]===37&&chunks===8192,'maximum upload extent changed');
}};
export const boundedStreams={async test(){
  check((await readBoundedBody(null,8)).byteLength===0,'absent body');
  let at=0;const reused=new Uint8Array(1);
  const source=new ReadableStream({pull(controller){if(at===3){controller.close();return;}reused[0]=++at;controller.enqueue(reused);}},{highWaterMark:0});
  const value=await readBoundedBody(source,8);
  check(value.length===3&&value.buffer.byteLength===8&&value[0]===1&&value[2]===3&&!source.locked,'bounded buffer ownership');
  let cancelled=false,pulls=0;
  const excessive=new ReadableStream({pull(controller){pulls++;controller.enqueue(new Uint8Array(4));},cancel(){cancelled=true;}},{highWaterMark:0});
  let rejected=false;try{await readBoundedBody(excessive,5);}catch(error){rejected=error.message==='body_limit';}
  check(rejected&&cancelled&&!excessive.locked&&pulls===2,'overflow not cancelled and released');
  const broken=new ReadableStream({pull(controller){controller.error(Error('fixture stream failed'));}},{highWaterMark:0});
  rejected=false;try{await readBoundedBody(broken,5);}catch(error){rejected=error.message==='fixture stream failed';}
  check(rejected&&!broken.locked,'failed stream reader retained');
}};
