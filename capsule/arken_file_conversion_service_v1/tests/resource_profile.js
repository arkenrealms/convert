// Real candidate actions and assertions. Image codecs, payment and report
// decisions remain in the frozen C++ modules. Native CDP owns memory accounting.
import {proveProductionHttp} from './production_http_tests.js';
import {proveConversionJournalHttp} from './payment_journal_http.js';
import {convertImage} from './runtime.js';
import fixtureText from './startup-fixtures.json';
import maximumPng from './maximum-input.png';
import resourceFixtureText from './resource-fixture.json';
export {ConversionPaymentLedger as Ledger} from './production_worker.js';
export {JournalLedger} from './payment_journal_http.js';
const fixture=JSON.parse(fixtureText),resourceFixture=JSON.parse(resourceFixtureText),retained=[];
let calibrationArray,calibrationWasm,production=false,journal=false,conversions=0;
const check=(ok,label)=>{if(!ok)throw Error(label);};
const retain=worker=>{check(retained.length<2,'Resource instance retention exceeded');retained.push(worker);};
export default {async fetch(request,env){try{
 const phase=new URL(request.url).pathname;
 let result=null;
 if(phase==='/calibrate-array')calibrationArray=new ArrayBuffer(4194304);
 else if(phase==='/calibrate-wasm')calibrationWasm=new WebAssembly.Memory({initial:128,maximum:128});
 else if(phase==='/production'){
  check(!production,'Production resource workload repeated');
  await proveProductionHttp({...env,PHASE:'write'},retain);production=true;
 }else if(phase==='/journal'){
  check(production&&!journal,'Journal resource workload order');
  result=await proveConversionJournalHttp({...env,PHASE:'write'},retain);journal=true;
 }else if(phase==='/maximum'||phase==='/retained'){
  check(production&&journal&&retained.length===2,'Required production instances missing');
  // Native-generated, CRC-valid PNG at both encoded-byte and pixel limits.
  const input=new Uint8Array(maximumPng);
  check(input.byteLength===8388608&&input.byteLength===resourceFixture.inputBytes,'Maximum source size');
  const output=convertImage(input,fixture.largeJpeg.codec);
  if(output.error)throw Error(`Maximum source conversion rejected: ${output.error}/${output.decodeError}/${output.encodeError}`);
  const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',output.bytes)),b=>b.toString(16).padStart(2,'0')).join('');
  check(output.width===1024&&output.height===1024&&hash===fixture.largeJpeg.sha256&&hash===resourceFixture.outputSha256,'Maximum source/pixel result differs from native');
  conversions++;result={inputBytes:input.length,width:output.width,height:output.height,outputBytes:output.bytes.length,sha256:hash};
 }else throw Error('Unknown resource action');
 return Response.json({status:'passed',phase,result,production,journal,conversions,retainedWorkers:retained.length,
  calibrationArrayBytes:calibrationArray?.byteLength??0,calibrationWasmBytes:calibrationWasm?.buffer.byteLength??0,
  financialEffects:0,externalRequests:0});
}catch(error){console.error('Convert resource qualification failed',error);return Response.json({status:'failed',error:String(error)},{status:500});}}};
