import {planRequest,convertImage} from './runtime.js';
const jsonHeaders={'content-type':'application/json','cache-control':'no-store','x-content-type-options':'nosniff'};
const ttlMs=15*60*1000;
const hex=bytes=>Array.from(new Uint8Array(bytes),v=>v.toString(16).padStart(2,'0')).join('');
async function digest(bytes){return hex(await crypto.subtle.digest('SHA-256',bytes));}
function problem(code,status=400){return new Response(JSON.stringify({error:code}),{status,headers:jsonHeaders});}
async function signed(secret,message) {
  const key=await crypto.subtle.importKey('raw',new TextEncoder().encode(secret),{name:'HMAC',hash:'SHA-256'},false,['sign']);
  return hex(await crypto.subtle.sign('HMAC',key,new TextEncoder().encode(message)));
}
function equal(a,b) {
  if(typeof a!=='string'||typeof b!=='string'||a.length!==b.length)return false;
  let difference=0;for(let i=0;i<a.length;i++)difference|=a.charCodeAt(i)^b.charCodeAt(i);return difference===0;
}
async function boundedBody(request,limit) {
  if(!request.body)return new Uint8Array();
  const reader=request.body.getReader(),chunks=[];let size=0;
  try {
    while(true){const next=await reader.read();if(next.done)break;
      size+=next.value.byteLength;if(size>limit){await reader.cancel();throw Error('body_limit');}chunks.push(next.value);}
  } finally {reader.releaseLock();}
  const bytes=new Uint8Array(size);let offset=0;for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}return bytes;
}
function toolReply(plan,value,error=false) {
  const result={content:[{type:'text',text:JSON.stringify(value)}],...(error?{isError:true}:{structuredContent:value})};
  return toolResult(plan,result);
}
function toolResult(plan,result){return new Response(plan.responsePrefix+JSON.stringify(result)+'}',{headers:jsonHeaders});}
function artifactRoot(owner,id,prefix){return `${prefix}/${owner}/${id}`;}
async function fileRecord(env,owner,id,prefix) {
  const key=artifactRoot(owner,id,prefix),object=await env.FILES.get(key+'/request');
  if(!object)return null;
  const record=await object.json();
  if(record.owner!==owner||record.expiresAt<=Date.now())return null;
  return {key,record};
}
async function downloadUrl(env,url,owner,id,expiresAt) {
  const token=await signed(env.ACCESS_TOKEN,`${owner}:${id}:output:${expiresAt}`);
  return `${url.origin}/files/${id}/output?owner=${owner}&expires=${expiresAt}&token=${token}`;
}
async function command(plan,env,url,owner,payment) {
  const prefix=payment?'sandbox':'preview';
  if(!env.FILES)return toolReply(plan,{code:'storage_not_configured'},true);
  if(plan.commandId===1) {
    const id=hex(crypto.getRandomValues(new Uint8Array(32))),expiresAt=Date.now()+ttlMs;
    await env.FILES.put(artifactRoot(owner,id,prefix)+'/request',JSON.stringify({owner,expiresAt,args:plan.arguments}));
    return toolReply(plan,{file_id:id,upload_url:`${url.origin}/files/${id}/input`,
      expires_at:new Date(expiresAt).toISOString(),maximum_source_bytes:8388608});
  }
  const id=plan.arguments.file_id,found=await fileRecord(env,owner,id,prefix);
  if(!found)return toolReply(plan,{code:'file_unavailable'},true);
  if(payment)return toolResult(plan,await payment.convert(plan,env,{found,url,owner,id,
    execute:(inputDigest,codec)=>convertAndStore(env,url,owner,id,found,inputDigest,codec)}));
  const previous=await env.FILES.get(found.key+'/result');
  if(previous)return toolReply(plan,await previous.json());
  const receipt=await convertAndStore(env,url,owner,id,found);
  return toolReply(plan,receipt,Boolean(receipt.code));
}
async function convertAndStore(env,url,owner,id,found,inputDigest,codec=found.record.args.codec) {
  const source=await env.FILES.get(found.key+'/input');
  if(!source)return {code:'upload_required'};
  if(source.size>8388608||source.size!==found.record.args.source_bytes)return {code:'source_size_mismatch'};
  const bytes=await source.arrayBuffer();
  if(inputDigest&&await digest(bytes)!==inputDigest)return {code:'source_digest_mismatch'};
  const result=convertImage(bytes,codec);
  if(result.error)return {code:'conversion_rejected',...result};
  const format=found.record.args.output_format;
  const mediaType=format==='jpeg'?'image/jpeg':format==='tga'?'image/x-tga':`image/${format}`;
  const receipt={file_id:id,download_url:await downloadUrl(env,url,owner,id,found.record.expiresAt),
    expires_at:new Date(found.record.expiresAt).toISOString(),width:result.width,height:result.height,
    output_bytes:result.bytes.byteLength,sha256:await digest(result.bytes),media_type:mediaType,
    alpha_disposition:result.alphaFlattened?'flattened_white':'preserved'};
  // One output-object write retains the bytes and their metadata together.
  // A lost acknowledgement can be recovered without re-running conversion.
  await env.FILES.put(found.key+'/output',result.bytes,{httpMetadata:{contentType:mediaType},customMetadata:{receipt:JSON.stringify(receipt)}});
  await env.FILES.put(found.key+'/result',JSON.stringify(receipt));return receipt;
}
export function createWorker(payment=null){let bodyInFlight=false;return {
  async fetch(request,env) {
    const url=new URL(request.url);
    if(url.pathname==='/health')return new Response(JSON.stringify({service:'megaconvert-mcp',displayName:'Arken Convert',version:'r4341',mode:payment?'x402-sandbox':'authenticated-preview',charging:false,testnetPayments:Boolean(payment),releaseId:env.RELEASE_ID||'unpublished',deploymentId:env.VERSION_METADATA?.id||'unpublished'}),{headers:jsonHeaders});
    if(!env.ACCESS_TOKEN||env.ACCESS_TOKEN.length<32)return problem('preview_not_configured',503);
    const origin=request.headers.get('origin');
    if(origin&&origin!==url.origin)return problem('origin_denied',403);
    const file=url.pathname.match(/^\/files\/([a-f0-9]{64})\/(input|output)$/);
    const capabilityDownload=file&&file[2]==='output'&&request.method==='GET';
    let owner;
    if(capabilityDownload) {
      owner=url.searchParams.get('owner');const expires=url.searchParams.get('expires'),token=url.searchParams.get('token');
      if(!/^[a-f0-9]{64}$/.test(owner||'')||!/^\d{13}$/.test(expires||'')||Number(expires)<=Date.now()
        ||!equal(token,await signed(env.ACCESS_TOKEN,`${owner}:${file[1]}:output:${expires}`)))return problem('download_denied',403);
    } else {
      if(!equal(request.headers.get('authorization'),`Bearer ${env.ACCESS_TOKEN}`))return problem('authorization_required',401);
      owner=await digest(new TextEncoder().encode(env.ACCESS_TOKEN));
    }
    try {
      if(file) {
        if(!env.FILES)return problem('storage_not_configured',503);
        const found=await fileRecord(env,owner,file[1],payment?'sandbox':'preview');if(!found)return problem('file_unavailable',404);
        if(capabilityDownload) {
          if(String(found.record.expiresAt)!==url.searchParams.get('expires'))return problem('download_denied',403);
          if(payment&&!await payment.downloadAllowed(env,found))return problem('payment_not_fulfilled',403);
          const object=await env.FILES.get(found.key+'/output');if(!object)return problem('output_not_ready',404);
          return new Response(object.body,{headers:{'content-type':object.httpMetadata?.contentType||'application/octet-stream',
            'content-disposition':'attachment','cache-control':'no-store','x-content-type-options':'nosniff'}});
        }
        if(file[2]!=='input'||request.method!=='PUT')return problem('method_not_allowed',405);
        if(bodyInFlight)return problem('busy',429);bodyInFlight=true;
        try {
          const bytes=await boundedBody(request,found.record.args.source_bytes);
          if(bytes.byteLength!==found.record.args.source_bytes)return problem('source_size_mismatch');
          const stored=await env.FILES.put(found.key+'/input',bytes,{onlyIf:{etagDoesNotMatch:'*'},customMetadata:{sha256:await digest(bytes)}});
          return stored?new Response(null,{status:204}):problem('upload_already_committed',409);
        } finally {bodyInFlight=false;}
      }
      if(url.pathname!=='/mcp')return problem('not_found',404);
      if(request.method!=='POST')return problem('method_not_allowed',405);
      if(!(request.headers.get('content-type')||'').split(';')[0].trim().match(/^application\/json$/i))return problem('json_required',415);
      const version=request.headers.get('mcp-protocol-version');
      if(version&&version!=='2025-11-25')return problem('protocol_version_unsupported');
      const accept=request.headers.get('accept')||'';
      if(!accept.includes('application/json')||!accept.includes('text/event-stream'))return problem('accept_header_required',406);
      if(bodyInFlight)return problem('busy',429);bodyInFlight=true;
      try {
        const bytes=await boundedBody(request,16384);
        let text;
        try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}
        catch(error){console.warn('invalid UTF-8 MCP request');return problem('invalid_utf8',400);}
        const plan=(payment?.planRequest||planRequest)(text);
        if(!version&&!plan.initialize)return problem('protocol_version_required',400);
        if(plan.kind==='response')return new Response(plan.status===202?null:plan.body,{status:plan.status,headers:jsonHeaders});
        return await command(plan,env,url,owner,payment);
      } finally {bodyInFlight=false;}
    } catch(error) {
      console.error('conversion request failed',{code:error.message==='body_limit'?'body_limit':'request_failed'});
      return problem(error.message==='body_limit'?'body_limit':'request_failed',error.message==='body_limit'?413:500);
    }
  }
};}
export default createWorker();
