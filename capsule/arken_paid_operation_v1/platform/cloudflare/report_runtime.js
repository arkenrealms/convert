// Byte transport only. The closed C++ ABI owns authorization, paging and record
// validation; this runtime has no paid exchange or provider dependency.
export function createPaidJournalReporter(module,{exportPrefix='arken_paid_report'}={}){
 let instance=null;
 const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
 return {
  packet(mode,value){
   if(!instance){
    if(WebAssembly.Module.imports(module).length)throw Error('Journal report imports host capabilities');
    const candidate=new WebAssembly.Instance(module,{});candidate.exports._initialize();instance=candidate;
   }
   const abi=instance.exports,text=typeof value==='string'?value:JSON.stringify(value),call=name=>abi[exportPrefix+'_'+name];
   try{
    const capacity=Math.min(call('limit')(),text.length*3),pointer=call('prepare')(capacity);
    if(!pointer)throw Error('Journal report input limit');
    const encoded=encoder.encodeInto(text,new Uint8Array(abi.memory.buffer,pointer,capacity));
    if(encoded.read!==text.length||!call('commit')(encoded.written))throw Error('Journal report input limit');
    const size=call('run')(mode);
    if(!size||size>call('limit')())throw Error('Journal report output limit');
    return JSON.parse(decoder.decode(new Uint8Array(abi.memory.buffer,call('result')(),size)));
   }finally{call('clear')();}
  },
  memoryBytes:()=>instance?.exports.memory.buffer.byteLength??0,
  instanceCount:()=>instance?1:0
 };
}
