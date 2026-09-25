import fixtureText from './provider-fixtures.json';
import module from './conversion.wasm';
import {cdpProviderPacket} from './runtime.js';
import {authorizeCdpRequest,createCdpTransport} from './cdp_authorization.js';
const fixtures=JSON.parse(fixtureText);
const check=(value,label)=>{if(!value)throw Error(label);};
const equal=(a,b,label)=>check(JSON.stringify(a)===JSON.stringify(b),label);
async function rejected(call,label){let failure;try{await call();}catch(error){failure=error;}check(failure instanceof Error,label);return failure;}
const credential=p=>({keyId:p.keyId,keySecret:p.keySecret});
const timing=p=>({providerPacket:cdpProviderPacket,now:p.now,nonce:p.nonce});

export const cdpAuthorization={async test(){
  check(fixtures.credentialFixture==='rfc8032-test1-not-live','wrong signing fixture');
  for(const vector of fixtures.vectors)equal(cdpProviderPacket(vector.input),vector.expected,'C++ native/Wasm provider packet mismatch');
  for(const value of fixtures.signingCases){const p=value.prepare;
    const result=await authorizeCdpRequest(p.request,credential(p),timing(p));
    equal(result,{method:p.request.method,url:p.request.url,authorization:'Bearer '+value.jwt},'native OpenSSL/WebCrypto JWT mismatch');
  }
  const parallel=await Promise.all(fixtures.signingCases.map(value=>authorizeCdpRequest(value.prepare.request,credential(value.prepare),timing(value.prepare))));
  parallel.forEach((result,i)=>check(result.authorization==='Bearer '+fixtures.signingCases[i].jwt,'concurrent C++ scratch or key cross-talk'));
  const value=fixtures.signingCases[0],p=value.prepare;
  const imports=[],keys=[];
  const subtle={
    async importKey(format,bytes,algorithm,extractable,usages){imports.push({format,bytes,extractable,usages});const key=await crypto.subtle.importKey(format,bytes,algorithm,extractable,usages);keys.push(key);return key;},
    sign:(...args)=>crypto.subtle.sign(...args),verify:(...args)=>crypto.subtle.verify(...args)
  };
  await authorizeCdpRequest(p.request,credential(p),{...timing(p),subtle});
  check(imports.length===2&&imports[0].format==='pkcs8'&&imports[1].format==='raw','incorrect key import boundary');
  equal(imports.map(x=>x.usages),[['sign'],['verify']],'key usages widened');
  check(keys.every(key=>!key.extractable)&&imports.every(x=>!x.extractable&&x.bytes.every(b=>b===0)),'key extractable or typed scratch retained');
  await rejected(()=>crypto.subtle.exportKey('pkcs8',keys[0]),'private key exported');
  const mismatch=fixtures.mismatchedKey;
  await rejected(()=>authorizeCdpRequest(mismatch.request,credential(mismatch),timing(mismatch)),'mismatched public half accepted');
  let fetches=0;
  await rejected(()=>createCdpTransport(credential(p)),'missing C++ authority accepted');
  const never=()=>{fetches++;throw Error('unexpected network');};
  for(const candidate of [undefined,{keyId:p.keyId},{keyId:'wrong',keySecret:p.keySecret},credential(mismatch)]){
    const send=createCdpTransport(candidate,{providerPacket:cdpProviderPacket,fetcher:never,clock:()=>p.now,nonce:()=>p.nonce});
    await rejected(()=>send(p.request),'malformed credential reached network');
  }
  for(const request of [{...p.request,method:'POST'},{...p.request,url:'https://x402.org/facilitator/supported'},
    {...p.request,url:p.request.url+'?redirect=1'},{...p.request,url:p.request.url.replace('https:','http:')}]){
    await rejected(()=>createCdpTransport(credential(p),{providerPacket:cdpProviderPacket,fetcher:never,clock:()=>p.now,nonce:()=>p.nonce})(request),'request scope widened');
  }
  check(fetches===0,'rejected auth made an external request');
  for(const request of [{...p.request,body:'not-empty'},{...p.request,authentication:'none'},{...p.request,headers:{Authorization:'foreign'}}]){
    await rejected(()=>createCdpTransport(credential(p),{providerPacket:cdpProviderPacket,fetcher:never})(request),'transport request bypassed C++ admission');
  }
  check(fetches===0,'invalid transport body or headers reached network');
  const failedCrypto={...subtle,sign:async()=>{throw Error(p.keySecret);}};
  const cryptoError=await rejected(()=>authorizeCdpRequest(p.request,credential(p),{...timing(p),subtle:failedCrypto}),'crypto exception accepted');
  check(!cryptoError.message.includes(p.keySecret)&&cryptoError.message==='cdp_authorization_unavailable','crypto failure leaked secret');
  // Mutating caller input while imports await must not retarget an authorization.
  const selected=credential(p),request={...p.request};let calls=0;
  const send=createCdpTransport(selected,{providerPacket:cdpProviderPacket,clock:()=>p.now,nonce:()=>p.nonce,fetcher:async(url,options)=>{
    calls++;check(url===p.request.url&&options.method==='GET','request changed across asynchronous import');
    check(options.body===undefined,'GET was passed a Fetch body');
    check(options.headers.Authorization==='Bearer '+value.jwt&&options.redirect==='manual'&&options.signal instanceof AbortSignal,'request authorization or redirect policy wrong');
    return new Response('redirect-not-followed',{status:302,headers:{location:'https://foreign.invalid'}});
  }});
  selected.keyId='changed';selected.keySecret='changed';const pending=send(request);request.url='https://foreign.invalid';request.method='POST';
  equal(await pending,{status:302,body:'redirect-not-followed'},'redirect not preserved for caller rejection');check(calls===1,'redirect followed or request retried');
  for(const status of [401,403,429,500]){let count=0;
    const result=await createCdpTransport(credential(p),{providerPacket:cdpProviderPacket,clock:()=>p.now,nonce:()=>p.nonce,fetcher:async()=>{count++;return new Response('rejected',{status});}})(p.request);
    check(result.status===status&&count===1,'authentication rejection triggered fallback/retry');
  }
  const error=await rejected(()=>createCdpTransport(credential(p),{providerPacket:cdpProviderPacket,clock:()=>p.now,nonce:()=>p.nonce,fetcher:async()=>{throw Error(p.keySecret);}})(p.request),'transport exception lost');
  check(error.message==='cdp_transport_unavailable'&&!error.message.includes(p.keySecret),'network diagnostic leaked key');
  await rejected(()=>createCdpTransport(credential(p),{providerPacket:cdpProviderPacket,clock:()=>p.now,nonce:()=>p.nonce,fetcher:async()=>new Response('x'.repeat(16385))})(p.request),'unbounded provider response');
  // Explicit ABI buffers are erased; this does not claim GC/allocator-wide erasure.
  const isolated=new WebAssembly.Instance(module,{}).exports;isolated._initialize();
  const encoded=new TextEncoder().encode(JSON.stringify(p));const pointer=isolated.arken_mcp_prepare(encoded.length);
  new Uint8Array(isolated.memory.buffer,pointer,encoded.length).set(encoded);isolated.arken_cdp_provider_packet();
  const output=isolated.arken_mcp_output(),size=isolated.arken_mcp_output_size();check(size>0,'private ABI did not produce output');
  isolated.arken_mcp_clear();check(isolated.arken_mcp_output_size()===0,'private output length retained');
  check(new Uint8Array(isolated.memory.buffer,pointer,encoded.length).every(x=>x===0)&&new Uint8Array(isolated.memory.buffer,output,size).every(x=>x===0),'explicit ABI scratch not erased');
  console.log('CDP provider agreement passed: '+fixtures.vectors.length+' native/Wasm packets, '+fixtures.signingCases.length+' OpenSSL/WebCrypto signatures; no real credentials or payments');
}};
