import worker from './worker.js';
export {payments} from './payment_worker_tests.js';
const token='test-only-access-token-at-least-32-characters';
const check=(value,message)=>{if(!value)throw Error(message);};
// Storage is a fixture; C++ conversion and the Worker runtime are real.
class Bucket {
  values=new Map();
  async put(key,value,options={}) {
    if(options.onlyIf?.etagDoesNotMatch==='*'&&this.values.has(key))return null;
    const bytes=typeof value==='string'?new TextEncoder().encode(value):new Uint8Array(value).slice();
    this.values.set(key,{bytes,httpMetadata:options.httpMetadata});return {etag:key};
  }
  async get(key) {
    const item=this.values.get(key);if(!item)return null;const bytes=item.bytes.slice();
    return {size:bytes.length,body:new Response(bytes).body,httpMetadata:item.httpMetadata,
      arrayBuffer:async()=>bytes.buffer,json:async()=>JSON.parse(new TextDecoder().decode(bytes))};
  }
}
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
  const init=await (await worker.fetch(request(rpc('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'test',version:'1'}})),e)).json();
  check(init.result.serverInfo.name==='MegaConvert','initialize');
  const listed=await (await worker.fetch(request(rpc('tools/list')),e)).json();
  check(listed.result.tools.length===2,'generated tool count');
  const upload=listed.result.tools.find(t=>t.name==='conversion.v1.prepare_upload');
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
    const result=await (await worker.fetch(request(call('conversion.v1.prepare_upload',args)),e)).json();
    check(result.error?.code===-32602,'invalid options reached storage');
  }
  check(e.FILES.values.size===0,'rejected input mutated storage');
  check((await worker.fetch(new Request('https://converter.example/mcp',{method:'POST',headers,body:new Uint8Array([195,40])}),e)).status===400,'invalid UTF-8');
}};
export const admission={async test(){
  const e=env();
  check((await worker.fetch(request(rpc('ping')),{})).status===503,'missing credential must fail closed');
  const noStorage=await (await worker.fetch(request(call('conversion.v1.prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),{ACCESS_TOKEN:token})).json();
  check(noStorage.result.isError===true,'missing storage must fail closed');
  const prepared=await (await worker.fetch(request(call('conversion.v1.prepare_upload',{
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
  const again=await (await worker.fetch(request(call('conversion.v1.prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  const input=again.result.structuredContent.upload_url;
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:new Uint8Array(23)}),e)).status===413,'upload byte limit');
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:new Uint8Array(21)}),e)).status===400,'short upload');
  check((await worker.fetch(new Request(input,{method:'PUT',headers,body:tga}),e)).status===204,'failed upload poisoned admission');
}};
export const conversion={async test(){
  const e=env();
  const prepared=await (await worker.fetch(request(call('conversion.v1.prepare_upload',{
    source_bytes:tga.length,input_format:'tga',output_format:'png'})),e)).json();
  const lease=prepared.result.structuredContent;
  check(lease.file_id.length===64&&lease.upload_url.endsWith('/input'),'upload capability');
  const put=()=>worker.fetch(new Request(lease.upload_url,{method:'PUT',headers:{authorization:`Bearer ${token}`},body:tga}),e);
  check((await put()).status===204,'upload');check((await put()).status===409,'immutable upload');
  const converted=await (await worker.fetch(request(call('conversion.v1.convert_image',{file_id:lease.file_id})),e)).json();
  const receipt=converted.result.structuredContent;
  check(receipt.width===1&&receipt.height===1&&receipt.media_type==='image/png','C++ output metadata');
  const repeated=await (await worker.fetch(request(call('conversion.v1.convert_image',{file_id:lease.file_id})),e)).json();
  check(JSON.stringify(repeated.result.structuredContent)===JSON.stringify(receipt),'repeat changed receipt');
  const download=await worker.fetch(new Request(receipt.download_url),e);
  check(download.status===200,'signed download');
  const bytes=new Uint8Array(await download.arrayBuffer());check(bytes[0]===137&&bytes[1]===80&&bytes[2]===78,'actual PNG output');
  check(bytes.length===receipt.output_bytes,'download extent');
  const changed=new URL(receipt.download_url);changed.searchParams.set('token','0'.repeat(64));
  check((await worker.fetch(new Request(changed),e)).status===403,'forged download');
  const ownerChanged={ACCESS_TOKEN:'different-owner-token-at-least-32-characters',FILES:e.FILES};
  const foreign=await (await worker.fetch(request(call('conversion.v1.convert_image',{file_id:lease.file_id}),{'authorization':`Bearer ${ownerChanged.ACCESS_TOKEN}`}),ownerChanged)).json();
  check(foreign.result.isError===true,'cross-owner file access');
  const recordKey=[...e.FILES.values.keys()].find(k=>k.endsWith('/request'));
  const record=await (await e.FILES.get(recordKey)).json();record.expiresAt=1;await e.FILES.put(recordKey,JSON.stringify(record));
  check((await worker.fetch(new Request(receipt.download_url),e)).status===404,'expired file retained access');
}};
export const largeImage={async test(){
  const e=env(),bytes=new Uint8Array(18+1024*1024*4);
  bytes[2]=2;bytes[13]=4;bytes[15]=4;bytes[16]=32;bytes[17]=40;
  for(let i=18;i<bytes.length;i+=4){bytes[i]=30;bytes[i+1]=20;bytes[i+2]=10;bytes[i+3]=255;}
  const prepared=await (await worker.fetch(request(call('conversion.v1.prepare_upload',{
    source_bytes:bytes.length,input_format:'tga',output_format:'png'})),e)).json();
  const lease=prepared.result.structuredContent;
  check((await worker.fetch(new Request(lease.upload_url,{method:'PUT',headers,body:bytes}),e)).status===204,'large upload');
  const result=await (await worker.fetch(request(call('conversion.v1.convert_image',{file_id:lease.file_id})),e)).json();
  check(result.result.structuredContent.width===1024&&result.result.structuredContent.height===1024,'large C++ conversion');
}};
