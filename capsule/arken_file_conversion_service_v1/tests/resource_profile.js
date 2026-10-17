// Real candidate actions and assertions. Image codecs, payment and report
// decisions remain in the frozen C++ modules. Native CDP owns memory accounting.
import {proveProductionHttp} from './production_http_tests.js';
import {proveConversionJournalHttp} from './payment_journal_http.js';
import {createResourceWorker,resourceGuardEvidence} from './resource_subject.js';
import fixtureText from './startup-fixtures.json';
import maximumPng from './maximum-input.png';
import resourceFixtureText from './resource-fixture.json';
export {ConversionPaymentLedger as Ledger} from './production_worker.js';
export {JournalLedger} from './payment_journal_http.js';
const fixture=JSON.parse(fixtureText),resourceFixture=JSON.parse(resourceFixtureText),retained=[];
let calibrationArray,calibrationWasm,production=false,journal=false,maximum,conversions=0;
const check=(ok,label)=>{if(!ok)throw Error(label);};
const retain=worker=>{check(retained.length<3,'Resource instance retention exceeded');retained.push(worker);};
export default {async fetch(request,env){try{
 const phase=new URL(request.url).pathname;
 let result=null;
 if(phase==='/calibrate-array')calibrationArray=new ArrayBuffer(4194304);
 else if(phase==='/calibrate-wasm')calibrationWasm=new WebAssembly.Memory({initial:128,maximum:128});
 else if(phase==='/production'){
  check(!production,'Production resource workload repeated');
  await proveProductionHttp({...env,PHASE:'write'},retain,createResourceWorker);production=true;
 }else if(phase==='/journal'){
  check(production&&!journal,'Journal resource workload order');
  result=await proveConversionJournalHttp({...env,PHASE:'write'},retain);journal=true;
 }else if(phase==='/maximum'){
  check(production&&journal&&retained.length===2&&!maximum,'Required production instances missing');
  // Native-generated, CRC-valid PNG at both encoded-byte and pixel limits.
  const input=new Uint8Array(maximumPng);
  check(input.byteLength===8388608&&input.byteLength===resourceFixture.inputBytes,'Maximum source size');
  check(fixture.largeJpeg.sha256===resourceFixture.outputSha256,'Maximum native fixture authority differs');
  maximum=await proveProductionHttp({...env,PHASE:'write'},retain,createResourceWorker,
   {bytes:input,inputFormat:'png',outputFormat:'jpeg',sha256:resourceFixture.outputSha256});
  conversions++;result=maximum.result;
 }else if(phase==='/retained'){
  check(maximum&&retained.length===3,'Maximum paid HTTP state missing');
  result=await maximum.replay();
 }else throw Error('Unknown resource action');
 return Response.json({status:'passed',phase,result,guard:resourceGuardEvidence(),production,journal,conversions,retainedWorkers:retained.length,
  calibrationArrayBytes:calibrationArray?.byteLength??0,calibrationWasmBytes:calibrationWasm?.buffer.byteLength??0,
  financialEffects:0,externalRequests:0});
}catch(error){console.error('Convert resource qualification failed',error);return Response.json({status:'failed',error:String(error)},{status:500});}}};
