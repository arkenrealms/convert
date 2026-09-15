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
