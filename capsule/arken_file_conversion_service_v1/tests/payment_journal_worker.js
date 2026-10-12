// ABI/parity assertions only; all record generation, validation and projection
// are native. Durable storage and protected HTTP are proved by the sibling recipe.
import module from './conversion-journal-report.wasm';
import fixturesText from './payment-journal-fixtures.json';
import accessText from './payment-journal-access-fixtures.json';
export {JournalLedger} from './payment_journal_http.js';
import {proveConversionJournalHttp} from './payment_journal_http.js';
import {paymentStorageAdvance} from './runtime.js';
export const journal = {async test(_controller,env) {
  let checks=0,packets=0,frozenTransitions=0;
  const check=(ok,message)=>{++checks;if(!ok)throw Error(message);};
  check(WebAssembly.Module.imports(module).length===0,'Report imports host capabilities');
  const expected=['_initialize','memory','arken_convert_report_clear','arken_convert_report_prepare',
    'arken_convert_report_commit','arken_convert_report_run','arken_convert_report_result','arken_convert_report_limit'];
  const exported=WebAssembly.Module.exports(module).map(row=>row.name).sort().join(',');
  check(exported===expected.sort().join(','),'Report exposes unexpected capabilities: '+exported);
  const abi=new WebAssembly.Instance(module,{}).exports;abi._initialize();
  const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
  const packet=(mode,text)=>{
    const bytes=encoder.encode(text),ptr=abi.arken_convert_report_prepare(bytes.length);
    check(Boolean(ptr),'Report prepare failed');
    try{
      new Uint8Array(abi.memory.buffer,ptr,bytes.length).set(bytes);
      check(abi.arken_convert_report_commit(bytes.length),'Report commit failed');
      const size=abi.arken_convert_report_run(mode);
      check(size>0&&size<=abi.arken_convert_report_limit(),'Report output limit');
      return decoder.decode(new Uint8Array(abi.memory.buffer,abi.arken_convert_report_result(),size));
    }finally{abi.arken_convert_report_clear();}
  };
  const fixtures=[...JSON.parse(fixturesText),...JSON.parse(accessText).vectors];
  for(const fixture of fixtures){
    if(fixture.mode===2){
      check(JSON.stringify(paymentStorageAdvance(fixture.input))===fixture.output,'Frozen payment codec differs');
      ++frozenTransitions;
    }else{
      check(packet(fixture.mode,fixture.input)===fixture.output,'Native/Wasm journal projection differs');++packets;
    }
  }
  check(packets>=40&&frozenTransitions>=10,'Missing production-shaped report coverage');
  check(abi.arken_convert_report_run(10)===0,'Run without admission');
  check(!abi.arken_convert_report_commit(1),'Commit without prepare');
  check(!abi.arken_convert_report_prepare(0)&&!abi.arken_convert_report_prepare(1048577),'Unbounded prepare');
  check(Boolean(abi.arken_convert_report_prepare(1))&&!abi.arken_convert_report_commit(2),'Oversized commit');
  for(const mode of [0,1,2,3,4,5,6,7,8,14,0xffffffff]){
    check(Boolean(abi.arken_convert_report_prepare(2)),'Prepare for denied mode');
    check(abi.arken_convert_report_commit(2)&&abi.arken_convert_report_run(mode)===0,'Effect-capable report mode');
  }
  const sample=fixtures.find(row=>row.mode===10&&!JSON.parse(row.output).packetError);
  packet(10,sample.input);const memory=abi.memory.buffer.byteLength;
  for(let i=0;i<100;++i)check(packet(10,sample.input)===sample.output,'Repeated journal read changed');
  check(abi.memory.buffer.byteLength===memory&&memory<=16777216,'Report memory did not stabilize');
  const http=await proveConversionJournalHttp(env);
  console.log('ARKEN_CONVERT_REPORT_PROOF '+JSON.stringify({status:'passed',checks,packets,frozenTransitions,wasmBytes:memory,http,
    hostImports:0,paidModuleRebuilt:false,reportStorageWrites:0,fixtureStorageWrites:http.fixtureStorageWrites,networkRequests:0,financialEffects:0,
    evidence:'codec-parity-and-protected-durable-http',productionDeployed:false}));
}};
