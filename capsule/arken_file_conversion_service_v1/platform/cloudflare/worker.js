import {planRequest,convertImage,serverSurface,endpointPacket,conversionTargetPacket} from './runtime.js';
import {readBoundedBody} from './bounded_body.js';
import {accessPlan,fileOwner,reservePublicUpload} from './public_admission.js';
const jsonHeaders={'content-type':'application/json','cache-control':'no-store','x-content-type-options':'nosniff'};
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
async function downloadUrl(env,url,owner,id,expiresAt,surface) {
  const token=await signed(env.ACCESS_TOKEN,`${surface.downloadDomain}${owner}:${id}:output:${expiresAt}`);
  return `${url.origin}/files/${id}/output?owner=${owner}&expires=${expiresAt}&token=${token}`;
}
async function command(plan,env,url,owner,payment,surface,publicRequest=false) {
  const prefix=surface.artifactPrefix;
  if(!env.FILES)return toolReply(plan,{code:'storage_not_configured'},true);
  if(plan.commandId===1) {
    if(payment?.startup){const admission=await payment.startup.ensure(env);if(!admission.ready)return toolReply(plan,{code:admission.code},true);}
    const id=hex(crypto.getRandomValues(new Uint8Array(32))),expiresAt=Date.now()+surface.accessMilliseconds;
    let fileToken;
    if(publicRequest){
      const admission=await reservePublicUpload(env.PAYMENT_LEDGER,id,plan.arguments.source_bytes,Math.floor(Date.now()/1000));
      if(!admission.allowed)return new Response(JSON.stringify({error:admission.code}),{status:admission.retryAfterSeconds?429:503,headers:{...jsonHeaders,...(admission.retryAfterSeconds?{'retry-after':String(admission.retryAfterSeconds)}:{})}});
      fileToken=hex(crypto.getRandomValues(new Uint8Array(32)));owner=fileOwner(id,fileToken);
    }
    const stored=await env.FILES.put(artifactRoot(owner,id,prefix)+'/request',JSON.stringify({owner,expiresAt,args:plan.arguments,publicFile:publicRequest}),{onlyIf:{etagDoesNotMatch:'*'}});
    if(stored===null)return toolReply(plan,{code:'file_identity_conflict'},true);
    return toolReply(plan,{file_id:id,upload_url:`${url.origin}/files/${id}/input${fileToken?'?file_token='+fileToken:''}`,
      expires_at:new Date(expiresAt).toISOString(),maximum_source_bytes:8388608,...(fileToken?{file_token:fileToken}:{})});
  }
  const id=plan.arguments.file_id,found=await fileRecord(env,owner,id,prefix);
  if(!found)return toolReply(plan,{code:'file_unavailable'},true);
  if(publicRequest&&found.record.publicFile!==true)return toolReply(plan,{code:'file_unavailable'},true);
  const target=conversionTargetPacket(JSON.stringify({commandId:plan.commandId,arguments:found.record.args}));
  if(!target.allowed)return toolReply(plan,{code:target.code},true);
  if(payment)return toolResult(plan,await payment.convert(plan,env,{found,url,owner,id,
    execute:(inputDigest,codec)=>convertAndStore(env,url,owner,id,found,surface,inputDigest,codec)}));
  const previous=await env.FILES.get(found.key+'/result');
  if(previous)return toolReply(plan,await previous.json());
  const receipt=await convertAndStore(env,url,owner,id,found,surface);
  return toolReply(plan,receipt,Boolean(receipt.code));
}
async function convertAndStore(env,url,owner,id,found,surface,inputDigest,codec=found.record.args.codec) {
  const source=await env.FILES.get(found.key+'/input');
  if(!source)return {code:'upload_required'};
  if(source.size>8388608||source.size!==found.record.args.source_bytes)return {code:'source_size_mismatch'};
  const bytes=await source.arrayBuffer();
  if(inputDigest&&await digest(bytes)!==inputDigest)return {code:'source_digest_mismatch'};
  const result=convertImage(bytes,codec);
  if(result.error)return {code:'conversion_rejected',...result};
  const format=found.record.args.output_format;
  const mediaType=format==='jpeg'?'image/jpeg':format==='tga'?'image/x-tga':`image/${format}`;
  const receipt={file_id:id,download_url:await downloadUrl(env,url,owner,id,found.record.expiresAt,surface),
    expires_at:new Date(found.record.expiresAt).toISOString(),width:result.width,height:result.height,
    output_bytes:result.bytes.byteLength,sha256:await digest(result.bytes),media_type:mediaType,
    alpha_disposition:result.alphaFlattened?'flattened_white':'preserved'};
  // One output-object write retains the bytes and their metadata together.
  // A lost acknowledgement can be recovered without re-running conversion.
  const committed=await env.FILES.put(found.key+'/output',result.bytes,{onlyIf:{etagDoesNotMatch:'*'},httpMetadata:{contentType:mediaType},customMetadata:{receipt:JSON.stringify(receipt)}});
  if(committed===null){
    const retained=await env.FILES.head(found.key+'/output');
    if(!retained?.customMetadata?.receipt)return {code:'output_receipt_unavailable'};
    return JSON.parse(retained.customMetadata.receipt);
  }
  await env.FILES.put(found.key+'/result',JSON.stringify(receipt));return receipt;
}
export function createWorker(payment=null){let bodyInFlight=false;const profileId=payment?.profileId??null;
const describe=ready=>{const result=serverSurface(JSON.stringify({profileId,ready}));if(result.packetError)throw Error(result.packetError);return result;};
const surface=describe(false);return {
  async fetch(request,env) {
    const url=new URL(request.url);
    const endpoint=endpointPacket(JSON.stringify({configuration:{origin:env.MCP_PUBLIC_ORIGIN??null,path:env.MCP_ENDPOINT_PATH??'/mcp',priorArtifactOrigin:env.MCP_PRIOR_ARTIFACT_ORIGIN??null},
      request:{method:request.method,path:url.pathname,origin:url.origin,hasOrigin:request.headers.has('origin'),originHeader:request.headers.get('origin')||'',contentType:request.headers.get('content-type')||'',accept:request.headers.get('accept')||'',version:request.headers.get('mcp-protocol-version')||''}}));
    if(!endpoint.allowed)return problem(endpoint.code,endpoint.status);
    if(url.pathname==='/health'){const admission=payment?.startup?.status(env),view=describe(admission?.ready===true);
      return new Response(JSON.stringify({service:'megaconvert-mcp',displayName:'Arken Convert',version:'r4502',mode:view.mode,publicDiscovery:view.publicAccess,charging:view.charging,testnetPayments:view.testnetPayments,...(admission?{admissionReady:admission.ready,admissionCode:admission.code}:{}),releaseId:env.RELEASE_ID||'unpublished',deploymentId:env.VERSION_METADATA?.id||'unpublished'}),{headers:jsonHeaders});}
    const secretValid=typeof env.ACCESS_TOKEN==='string'&&env.ACCESS_TOKEN.length>=32;
    if(!secretValid&&!surface.publicAccess)return problem('server_not_configured',503);
    const origin=request.headers.get('origin');
    if(origin&&origin!==url.origin)return problem('origin_denied',403);
    const file=url.pathname.match(/^\/files\/([a-f0-9]{64})\/(input|output)$/);
    if(file&&!secretValid)return problem('server_not_configured',503);
    const capabilityDownload=file&&file[2]==='output'&&request.method==='GET';
    let owner,publicRequest=false;
    const authorization=request.headers.get('authorization');
    const context={publicAccess:surface.publicAccess,hasAuthorization:authorization!==null,administrator:secretValid&&equal(authorization,`Bearer ${env.ACCESS_TOKEN}`)};
    if(url.pathname==='/_arken/production/qualify'||url.pathname==='/_arken/production/readiness'){
      const action=url.pathname==='/_arken/production/qualify'?'qualify':'readiness';
      if(!payment?.startup?.[action])return problem('not_found',404);
      const result=await payment.startup[action](env,{kind:'request',administrator:context.administrator,method:request.method,hasBody:false,hasQuery:url.search!==''},request.body);
      return new Response(JSON.stringify(result.body),{status:result.status,headers:jsonHeaders});
    }
    if(capabilityDownload) {
      owner=url.searchParams.get('owner');const expires=url.searchParams.get('expires'),token=url.searchParams.get('token');
      if(!/^[a-f0-9]{64}$/.test(owner||'')||!/^\d{13}$/.test(expires||'')||Number(expires)<=Date.now()
        ||!equal(token,await signed(env.ACCESS_TOKEN,`${surface.downloadDomain}${owner}:${file[1]}:output:${expires}`)))return problem('download_denied',403);
    } else {
      const access=accessPlan(context,'discovery');
      if(access.mode==='denied')return problem(access.code,401);
      publicRequest=access.mode==='public';
      if(!publicRequest)owner=await digest(new TextEncoder().encode(env.ACCESS_TOKEN));
    }
    try {
      if(file) {
        if(!env.FILES)return problem('storage_not_configured',503);
        if(publicRequest){const access=accessPlan(context,'file',file[1],url.searchParams.get('file_token'));if(access.mode==='denied')return problem(access.code,401);owner=access.owner;}
        const found=await fileRecord(env,owner,file[1],surface.artifactPrefix);if(!found)return problem('file_unavailable',404);
        if(publicRequest&&found.record.publicFile!==true)return problem('file_unavailable',404);
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
          const bytes=await readBoundedBody(request.body,found.record.args.source_bytes);
          if(bytes.byteLength!==found.record.args.source_bytes)return problem('source_size_mismatch');
          const stored=await env.FILES.put(found.key+'/input',bytes,{onlyIf:{etagDoesNotMatch:'*'},customMetadata:{sha256:await digest(bytes)}});
          return stored?new Response(null,{status:204}):problem('upload_already_committed',409);
        } finally {bodyInFlight=false;}
      }
      if(endpoint.kind!=='mcp')return problem('not_found',404);
      const version=request.headers.get('mcp-protocol-version');
      if(bodyInFlight)return problem('busy',429);bodyInFlight=true;
      try {
        const bytes=await readBoundedBody(request.body,16384);
        let text;
        try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}
        catch(error){console.warn('invalid UTF-8 MCP request');return problem('invalid_utf8',400);}
        const plan=(payment?.planRequest||planRequest)(text);
        if(!version&&!plan.initialize)return problem('protocol_version_required',400);
        if(plan.kind==='response')return new Response(plan.status===202?null:plan.body,{status:plan.status,headers:jsonHeaders});
        if(!secretValid)return toolReply(plan,{code:'server_not_configured'},true);
        if(publicRequest){const access=accessPlan(context,plan.commandId===1?'upload':'file',plan.arguments.file_id??null,plan.arguments.file_token??null);if(access.mode==='denied')return problem(access.code,401);owner=access.owner;}
        return await command(plan,env,url,owner,payment,surface,publicRequest);
      } finally {bodyInFlight=false;}
    } catch(error) {
      console.error('conversion request failed',{code:error.message==='body_limit'?'body_limit':'request_failed'});
      return problem(error.message==='body_limit'?'body_limit':'request_failed',error.message==='body_limit'?413:500);
    }
  }
};}
export default createWorker();
