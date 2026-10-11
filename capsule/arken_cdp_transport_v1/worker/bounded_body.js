// Fetch/R2 platform byte piping only. The owning C++ command supplies limits.
// Copy on consumption: stream producers may reuse a chunk after the next pull.
export async function readBoundedBody(body,limit,limitCode='body_limit',{timeoutMs=0}={}) {
  if(!body)return new Uint8Array();
  const reader=body.getReader();let bytes=new Uint8Array(),size=0,timer,timeoutError;
  const consume=async()=>{
    for(;;){const next=await reader.read();
      // A cancelled read may finish after timeout cleanup detached our scratch.
      if(timeoutError)throw timeoutError;
      if(next.done)break;
      if(next.value.byteLength>limit-size){await reader.cancel();throw Error(limitCode);}
      // The limit is a rejection boundary, not an allocation request. Small
      // provider replies must not retain a maximum-size backing allocation.
      // Still copy before the next pull because producers may reuse chunks.
      if(size+next.value.byteLength>bytes.byteLength){
        const grown=new Uint8Array(Math.min(limit,Math.max(size+next.value.byteLength,bytes.byteLength*2,4096)));
        grown.set(bytes.subarray(0,size));
        // Only this reader's allocation is ours to detach, never next.value.
        bytes.buffer.transfer(0);bytes=grown;
      }
      bytes.set(next.value,size);size+=next.value.byteLength;
    }return bytes.subarray(0,size);
  };
  try{return timeoutMs?await Promise.race([consume(),new Promise((_,reject)=>{timer=setTimeout(()=>{
    timeoutError=Error('body_timeout');reject(timeoutError);
    reader.cancel().catch(()=>console.warn('Bounded body cancellation failed'));},timeoutMs);})]):await consume();}
  catch(error){bytes.buffer.transfer(0);throw error;}
  finally{clearTimeout(timer);reader.releaseLock();}
}

// Text consumers do not retain a second byte representation until the next GC.
// The binary API above still transfers the valid final view to its caller.
export async function readBoundedText(body,limit,limitCode='body_limit',options={}) {
  const bytes=await readBoundedBody(body,limit,limitCode,options);
  try{return new TextDecoder('utf-8',{fatal:true}).decode(bytes);}
  finally{bytes.buffer.transfer(0);}
}
