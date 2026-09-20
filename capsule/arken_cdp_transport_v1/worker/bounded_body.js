// Fetch/R2 platform byte piping only. The owning C++ command supplies limits.
// Copy on consumption: stream producers may reuse a chunk after the next pull.
export async function readBoundedBody(body,limit,limitCode='body_limit',{timeoutMs=0}={}) {
  if(!body)return new Uint8Array();
  const bytes=new Uint8Array(limit),reader=body.getReader();let size=0,timer;
  const consume=async()=>{
    for(;;){const next=await reader.read();if(next.done)break;
      if(next.value.byteLength>limit-size){await reader.cancel();throw Error(limitCode);}
      bytes.set(next.value,size);size+=next.value.byteLength;
    }return bytes.subarray(0,size);
  };
  try{return timeoutMs?await Promise.race([consume(),new Promise((_,reject)=>{timer=setTimeout(()=>{
    reader.cancel().catch(()=>console.warn('Bounded body cancellation failed'));reject(Error('body_timeout'));},timeoutMs);})]):await consume();}
  finally{clearTimeout(timer);reader.releaseLock();}
}
