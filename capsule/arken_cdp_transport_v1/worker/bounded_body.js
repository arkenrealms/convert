// Fetch/R2 platform byte piping only. The owning C++ command supplies limits.
// Copy on consumption: stream producers may reuse a chunk after the next pull.
export async function readBoundedBody(body,limit,limitCode='body_limit') {
  if(!body)return new Uint8Array();
  const bytes=new Uint8Array(limit),reader=body.getReader();let size=0;
  try {
    for(;;){const next=await reader.read();if(next.done)break;
      if(next.value.byteLength>limit-size){await reader.cancel();throw Error(limitCode);}
      bytes.set(next.value,size);size+=next.value.byteLength;
    }
  } finally {reader.releaseLock();}
  return bytes.subarray(0,size);
}
