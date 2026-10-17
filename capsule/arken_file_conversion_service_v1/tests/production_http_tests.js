import text from './startup-fixtures.json';
import {productionStartupPacket,productionQualificationPacket,serverSurface,convertImage} from './runtime.js';
import largePng from './jpeg-input.png';
import {createProductionStartup} from './production_startup.js';
import {createProductionWorker} from './production_worker.js';
import sandbox from './sandbox_worker.js';
import preview from './worker.js';
import {Bucket} from './r2_bucket_fixture.js';
const f=JSON.parse(text),check=(v,label)=>{if(!v)throw Error(label);};
const headers={'content-type':'application/json',accept:'application/json, text/event-stream','mcp-protocol-version':'2025-11-25'};
const req=(method,params)=>new Request('https://production.example/mcp',{method:'POST',headers,body:JSON.stringify({jsonrpc:'2.0',id:1,method,params})});
const tga=new Uint8Array([0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,30,20,10,255]);
const hex=b=>Array.from(new Uint8Array(b),x=>x.toString(16).padStart(2,'0')).join('');
export async function proveProductionHttp(bindings,retainWorker,createWorker=createProductionWorker,workload){
  for(const v of f.vectors)check(JSON.stringify(productionStartupPacket(v.input))===JSON.stringify(v.expected),'native/Wasm startup mismatch');
  for(const v of f.qualificationVectors)check(JSON.stringify(productionQualificationPacket(v.input))===JSON.stringify(v.expected),'native/Wasm qualification mismatch');
  for(const v of f.surfaces)check(JSON.stringify(serverSurface(v.input))===JSON.stringify(v.expected),'native/Wasm surface mismatch');
  if(bindings.PHASE!=='write')return;
  const largeJpeg=convertImage(largePng,f.largeJpeg.codec);
  check(!largeJpeg.error&&largeJpeg.width===1024&&largeJpeg.height===1024&&hex(await crypto.subtle.digest('SHA-256',largeJpeg.bytes))===f.largeJpeg.sha256,'portable large JPEG native/Wasm bytes differ');
  let now=1000,probes=0,failSupported=false,waitProbe=null,probeStarted=null;
  const fetcher=async(url,options)=>{
    probes++;check(options.redirect==='manual','startup redirects followed');new Request(url,options);
    if(waitProbe){const wait=waitProbe;waitProbe=null;const started=probeStarted;probeStarted=null;started?.();await wait;}
    if(url.endsWith('/supported')){
      check(options.method==='GET','startup supported method');
      return options.headers.Authorization?new Response(failSupported?'denied':f.supported.body,{status:failSupported?403:200}):new Response('',{status:401});
    }
    check(url==='https://api.developer.coinbase.com/rpc/v1/base/'+f.configuration.rpcClientKey,'startup RPC scope');
    const rpc=JSON.parse(f.rpc.body);rpc.find(r=>r.id===2).result.timestamp='0x'+now.toString(16);
    return new Response(JSON.stringify(rpc));
  };
  const startup=createProductionStartup({fetcher,clock:()=>String(now)});
  const env={ACCESS_TOKEN:f.configuration.downloadSecret,FILES:new Bucket(),PAYMENT_LEDGER:bindings.LEDGER,RELEASE_ID:f.configuration.releaseId,
    CDP_API_KEY_ID:f.configuration.keyId,CDP_API_KEY_SECRET:f.configuration.keySecret,CDP_RPC_CLIENT_KEY:f.configuration.rpcClientKey,PAYMENTS_ENABLED:'false'};
  let settles=0,verifies=0;
  const app=createWorker({startup,clock:()=>String(now),fetcher:async(url,options)=>{
    const body=JSON.parse(options.body);check(options.headers.Authorization?.startsWith('Bearer ')&&body.paymentRequirements.network==='eip155:8453','production effect not authenticated mainnet');
    const jwt=options.headers.Authorization.slice(7).split('.');const claims=JSON.parse(atob(jwt[1].replaceAll('-','+').replaceAll('_','/')));
    check(claims.sub===env.CDP_API_KEY_ID&&JSON.stringify(claims.uris)===JSON.stringify(['POST '+url.slice(8)]),'production credential or JWT scope drift');
    const payer=body.paymentPayload.payload.authorization.from;
    if(url.endsWith('/verify')){verifies++;return new Response(JSON.stringify({isValid:true,payer}));}
    settles++;check(env.FILES.outputWrites>0,'settlement preceded output');
    return new Response(JSON.stringify({success:true,payer,network:'eip155:8453',transaction:'0x'+'af'.repeat(32)}));
  }});
  retainWorker?.(app);
  const call=async(name,args,payment)=>(await (await app.fetch(req('tools/call',{name,arguments:args,...(payment?{_meta:{'x402/payment':payment}}:{})}),env)).json()).result;
  const health=async()=>(await app.fetch(new Request('https://production.example/health'),env)).json();
  const init=await (await app.fetch(req('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'fixture',version:'1'}}),{})).json();
  check(init.result.instructions.includes('Base mainnet'),'production initialization mislabeled');
  check((await health()).mode==='x402-production'&&(await health()).charging===false,'disabled production claims charging');
  await app.fetch(req('tools/list',{}),env);check(probes===0&&env.FILES.values.size===0,'static discovery did I/O');
  const source=workload?.bytes??tga,expectedHash=workload?.sha256??f.nativePngSha256;
  const uploadArgs={source_bytes:source.byteLength,input_format:workload?.inputFormat??'tga',output_format:workload?.outputFormat??'png'};
  check((await call('prepare_upload',uploadArgs)).isError&&probes===0&&env.FILES.values.size===0,'disabled upload accepted');
  const qurl='https://production.example/_arken/production/qualify';
  const qualify=(options={})=>app.fetch(new Request(qurl,{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN},...options}),env);
  for(const options of [{headers:{}},{headers:{authorization:'Bearer wrong'}},{method:'GET'},{body:'{"ready":true}'},{headers:{authorization:'Bearer '+env.ACCESS_TOKEN,origin:'https://foreign.example'}}]){
    check((await qualify(options)).status>=400&&probes===0&&env.FILES.values.size===0,'qualification request gate did I/O');}
  check((await app.fetch(new Request(qurl+'?ready=true',{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN}}),env)).status===400&&probes===0,'qualification query accepted');
  for(const other of [sandbox,preview])check((await other.fetch(new Request(qurl,{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN}}),env)).status===404,'nonproduction qualification exposed');
  failSupported=true;const failedQualification=await qualify();
  check(failedQualification.status===503&&probes===2&&env.FILES.values.size===0&&(await health()).admissionCode==='production_admission_disabled','failed qualification changed admission');failSupported=false;probes=0;
  const qualificationResults=await Promise.all([qualify(),qualify()]);
  for(const response of qualificationResults){const value=await response.json();check(response.status===200&&value.providerQualified&&value.charging===false&&value.admissionReady===false,'private provider qualification failed');
    check(!JSON.stringify(value).includes(env.CDP_API_KEY_SECRET)&&!JSON.stringify(value).includes(env.CDP_RPC_CLIENT_KEY),'private qualification leaked secret');}
  check(probes===3&&env.PAYMENTS_ENABLED==='false'&&env.FILES.values.size===0&&settles===0&&verifies===0,'qualification widened purchase or storage authority');
  check((await health()).charging===false&&(await health()).admissionReady===false&&probes===3,'qualification affected public health');
  check((await call('prepare_upload',uploadArgs)).isError&&probes===3&&env.FILES.values.size===0,'qualification cache admitted upload');
  const again=await qualify();check(again.status===200&&probes===3,'qualification cache missed');
  const rurl='https://production.example/_arken/production/readiness';
  const readiness=(options={})=>app.fetch(new Request(rurl,{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN},...options}),env);
  for(const options of [{headers:{}},{headers:{authorization:'Bearer wrong'}},{method:'GET'},{body:'{}'},{headers:{authorization:'Bearer '+env.ACCESS_TOKEN,origin:'https://foreign.example'}}]){
    check((await readiness(options)).status>=400&&probes===3&&env.FILES.values.size===0,'readiness request gate did I/O');}
  check((await app.fetch(new Request(rurl+'?ready=true',{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN}}),env)).status===400&&probes===3,'readiness query accepted');
  for(const other of [sandbox,preview])check((await other.fetch(new Request(rurl,{method:'POST',headers:{authorization:'Bearer '+env.ACCESS_TOKEN}}),env)).status===404,'nonproduction readiness exposed');
  check((await readiness()).status===503&&probes===3&&env.FILES.values.size===0,'readiness bypassed disabled admission');
  const emptyStream=new ReadableStream({start(controller){controller.close();}});
  check((await qualify({body:emptyStream})).status===200&&probes===3,'empty POST stream treated as nonempty input');
  const originalRelease=env.RELEASE_ID;let qualifyRelease;waitProbe=new Promise(resolve=>{qualifyRelease=resolve;});
  const startedQualification=new Promise(resolve=>{probeStarted=resolve;});
  env.RELEASE_ID='d'.repeat(64);const changingQualification=qualify();await startedQualification;env.RELEASE_ID='e'.repeat(64);qualifyRelease();
  check((await changingQualification).status===503&&!startup.status(env).ready&&env.FILES.values.size===0,'late qualification accepted changed environment');env.RELEASE_ID=originalRelease;probes=0;
  env.PAYMENTS_ENABLED='true';failSupported=true;
  check((await call('prepare_upload',uploadArgs)).isError&&probes===2&&env.FILES.values.size===0,'failed provider admitted upload');
  check((await health()).admissionCode==='provider_authentication_unavailable'&&probes===2,'health lost error or retried');failSupported=false;
  const before=probes;const admissions=await Promise.all([readiness(),readiness()]);
  for(const response of admissions){const r=await response.json();check(response.status===200&&r.providerQualified&&r.admissionReady&&r.charging,'readiness did not use normal startup');}
  check(probes===before+3&&env.FILES.values.size===0&&settles===0&&verifies===0,'readiness made a purchase or checks not coalesced');
  check((await qualify()).status===503&&probes===before+3,'disabled-only qualification accepted enabled environment');
  check((await health()).charging===true&&probes===before+3,'ready health probed again');
  const readyCalls=probes;
  const prepared=await call('prepare_upload',uploadArgs),lease=prepared.structuredContent;
  check(lease&&Date.parse(lease.expires_at)-Date.now()>3590000&&probes===readyCalls,'production lease or cache incorrect');
  check([...env.FILES.values.keys()].every(k=>k.startsWith('production/')),'production wrote another prefix');
  check((await app.fetch(new Request(lease.upload_url,{method:'PUT',body:source}),env)).status===204,'production upload failed');
  const target={file_id:lease.file_id,file_token:lease.file_token};const offer=(await call('convert_image',target)).structuredContent;
  check(offer.accepts[0].network==='eip155:8453','production offer wrong network');
  const payment={x402Version:2,resource:offer.resource,accepted:offer.accepts[0],payload:{signature:'0x'+'00'.repeat(65),authorization:{
    from:'0x'+'34'.repeat(20),to:offer.accepts[0].payTo,value:offer.accepts[0].amount,validAfter:String(now-10),validBefore:String(now+50),nonce:'0x'+lease.file_id}}};
  const complete=await call('convert_image',target,payment);
  check(complete._meta?.['x402/payment-response']?.network==='eip155:8453'&&verifies===1&&settles===1,'production payment flow failed');
  const receipt=complete.structuredContent,download=await app.fetch(new Request(receipt.download_url),env),bytes=new Uint8Array(await download.arrayBuffer());
  check(download.status===200&&hex(await crypto.subtle.digest('SHA-256',bytes))===receipt.sha256&&receipt.sha256===expectedHash,'production bytes differ from native result');
  const result={inputBytes:source.byteLength,inputFormat:uploadArgs.input_format,outputFormat:uploadArgs.output_format,outputBytes:bytes.length,sha256:receipt.sha256,paidHttp:true};
  env.PAYMENTS_ENABLED='false';failSupported=true;const disabledProbes=probes;
  check((await readiness()).status===503&&probes===disabledProbes,'disable depends on provider health');
  check((await call('prepare_upload',uploadArgs)).isError,'disabled new purchase accepted');
  check(JSON.stringify(await call('convert_image',target))===JSON.stringify(complete)&&settles===1,'disable stranded paid receipt');
  check((await app.fetch(new Request(receipt.download_url),env)).status===200,'disable stranded paid download');
  check(probes===disabledProbes&&verifies===1&&settles===1,'disable recontacted provider or resettled');failSupported=false;
  // Even deliberate identity copies in the same bucket cannot cross HMAC domains.
  for(const [key,value] of [...env.FILES.values])for(const prefix of ['sandbox','preview'])env.FILES.values.set(key.replace(/^production/,prefix),structuredClone(value));
  for(const other of [sandbox,preview])check((await other.fetch(new Request(receipt.download_url),env)).status===403,'cross-surface capability accepted');
  // Remove only synthetic cross-surface copies after their rejection proof.
  // Retain the real production upload/output for resource and replay checks.
  for(const key of [...env.FILES.values.keys()])if(!key.startsWith('production/'))env.FILES.values.delete(key);
  env.PAYMENTS_ENABLED='true';now=1300;const expired=probes;await startup.ensure(env);check(probes===expired+3,'expired startup cache not refreshed');
  env.RELEASE_ID='b'.repeat(64);check(!startup.status(env).ready,'changed release reused readiness');
  let release;waitProbe=new Promise(r=>{release=r;});const changing=startup.ensure(env);env.RELEASE_ID='c'.repeat(64);release();
  check(!(await changing).ready&&!startup.status(env).ready,'late startup admitted changed configuration');
  console.log('Production HTTP passed: static/disabled/failed/ready/cache/expiry/config-change, actual C++ PNG, mocked mainnet payment, retained paid results after disable and artifact/HMAC isolation. No real credentials or payments.');
  return {result,async replay(){
    const before={probes,verifies,settles,writes:env.FILES.outputWrites};
    check(JSON.stringify(await call('convert_image',target))===JSON.stringify(complete),'retained paid replay changed receipt');
    const download=await app.fetch(new Request(receipt.download_url),env);
    check(download.status===200&&hex(await crypto.subtle.digest('SHA-256',await download.arrayBuffer()))===expectedHash,'retained download native parity');
    check(probes===before.probes&&verifies===before.verifies&&settles===before.settles&&env.FILES.outputWrites===before.writes,'retained replay repeated effects');
    return {...result,completedReplay:true,providerVerifies:0,providerSettles:0,outputWrites:0};
  }};
}
export const productionHttp={async test(_controller,bindings){return proveProductionHttp(bindings);}};
