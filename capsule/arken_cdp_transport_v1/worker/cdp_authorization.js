import {readBoundedBody} from './bounded_body.js';
export const apiVersion='arken.cdp.transport.worker.v1';
const invoke=(providerPacket,value)=>{if(typeof providerPacket!=='function')throw Error('cdp_provider_unavailable');const result=providerPacket(JSON.stringify(value));if(result.packetError)throw Error(result.packetError);return result;};
const clock=()=>String(Math.floor(Date.now()/1000));
const randomNonce=()=>Array.from(crypto.getRandomValues(new Uint8Array(16)),x=>x.toString(16).padStart(2,'0')).join('');

// Private Cloudflare crypto plumbing. C++ owns request/key representation,
// claims and complete JWT bytes. No provider selection or public signing tool.
export async function authorizeCdpRequest(request,credential,{providerPacket,now=clock(),nonce=randomNonce(),subtle=crypto.subtle}={}) {
  const context={request:{method:request.method,url:request.url},keyId:credential?.keyId,now,nonce};
  const prepared=invoke(providerPacket,{kind:'prepare',...context,keySecret:credential?.keySecret});
  const privateBytes=new Uint8Array(prepared.privateKeyPkcs8),publicBytes=new Uint8Array(prepared.publicKey);
  prepared.privateKeyPkcs8.fill(0);
  const input=new TextEncoder().encode(prepared.signingInput);
  let signature;
  try {
    const privateKey=await subtle.importKey('pkcs8',privateBytes,{name:'Ed25519'},false,['sign']);
    privateBytes.fill(0);
    const publicKey=await subtle.importKey('raw',publicBytes,{name:'Ed25519'},false,['verify']);
    signature=new Uint8Array(await subtle.sign({name:'Ed25519'},privateKey,input));
    if(!await subtle.verify({name:'Ed25519'},publicKey,signature,input))throw Error('cdp_key_public_half_mismatch');
    const signed=invoke(providerPacket,{kind:'complete',...context,signature:Array.from(signature)});
    return {method:signed.request.method,url:signed.request.url,authorization:signed.authorization};
  } catch {throw Error('cdp_authorization_unavailable');}
  finally {privateBytes.fill(0);publicBytes.fill(0);input.fill(0);signature?.fill(0);}
}

// Private adapter: only the injected C++ codec may approve endpoint and body.
// Other facilitator profiles are rejected, never rewritten into CDP requests.
export function createCdpTransport(credential,{providerPacket,fetcher=(url,options)=>fetch(url,options),clock:readClock=clock,nonce:readNonce=randomNonce,subtle=crypto.subtle}={}) {
  if(typeof providerPacket!=='function')throw Error('cdp_provider_unavailable');
  const selected={keyId:credential?.keyId,keySecret:credential?.keySecret};
  return async request=>{
    const plan=invoke(providerPacket,{kind:'transport',request}),submitted=plan.request;
    const signed=await authorizeCdpRequest(submitted,selected,{providerPacket,now:readClock(),nonce:readNonce(),subtle});
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),plan.deadlineMs);
    try {
      const response=await fetcher(signed.url,{method:signed.method,headers:{'content-type':'application/json',Authorization:signed.authorization},
        body:submitted.body??undefined,redirect:'manual',signal:controller.signal});
      const bytes=await readBoundedBody(response.body,plan.responseLimit,'cdp_response_limit');
      return {status:response.status,body:new TextDecoder('utf-8',{fatal:true}).decode(bytes)};
    } catch {throw Error('cdp_transport_unavailable');}
    finally {clearTimeout(timer);}
  };
}
// Platform binding identity and I/O only; C++ selects probes/cache policy.
export function createProviderReadiness({packet,providerPacket,configuration,bindings,fetcher=(url,options)=>fetch(url,options),clock}){
 const entries=new WeakMap();
 const entry=env=>{const observed=bindings(env);let value=entries.get(env);
  if(!value){value={bindings:observed,generation:1,cached:null};entries.set(env,value);}
  if(observed.length!==value.bindings.length||observed.some((x,i)=>x!==value.bindings[i])){value.bindings=observed;value.generation++;value.cached=null;}
  return value;};
 const input=(env,value)=>({configuration:configuration(env,String(value.generation)),cached:value.cached,anonymous:null,supported:null,rpc:null,now:String(clock())});
 return async env=>{
  const value=entry(env),captured=input(env,value),initial=packet(captured);
  const send=createCdpTransport(captured.configuration,{providerPacket,fetcher,clock:()=>String(clock())});
  let result=initial,probes=0;
  for(;result.kind==='probe'&&probes<3;++probes){let response;
   try{
    if(result.request.authentication==='cdp')response=await send(result.request);
    else{const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
     try{const r=await fetcher(result.request.url,{method:result.request.method,body:result.request.body||undefined,headers:{'content-type':'application/json'},redirect:'manual',signal:abort.signal});
      response={status:r.status,body:new TextDecoder('utf-8',{fatal:true}).decode(await readBoundedBody(r.body,262144,'provider_response_limit'))};
     }finally{clearTimeout(timer);}}
   }catch(error){console.warn('Provider readiness transport unavailable',{code:'provider_transport_unavailable'});response={status:0,body:''};}
   captured[result.slot]=response;captured.now=String(clock());result=packet(captured);
  }
  const current=entry(env),fresh=packet(input(env,current));
  if(fresh.fingerprint!==initial.fingerprint)return {ready:false,code:'provider_context_changed',probes};
  if(result.ready)current.cached=result.record;
  const accepted=result.ready?packet(input(env,current)):result;
  return {ready:accepted.ready===true,code:accepted.code,probes,reused:result.reused===true};
 };
}
