// R2 I/O fixture only. The Worker, C++ codec and payment actor are real.
export class Bucket {
  values=new Map();
  outputWrites=0;
  loseOutputAcknowledgement=false;
  async put(key,value,options={}) {
    if(options.onlyIf?.etagDoesNotMatch==='*'&&this.values.has(key))return null;
    const bytes=typeof value==='string'?new TextEncoder().encode(value):new Uint8Array(value).slice();
    this.values.set(key,{bytes,httpMetadata:options.httpMetadata,customMetadata:options.customMetadata});
    if(key.endsWith('/output')){
      this.outputWrites++;
      if(this.loseOutputAcknowledgement){this.loseOutputAcknowledgement=false;throw Error('fixture_output_acknowledgement_lost');}
    }
    return {etag:key};
  }
  async get(key) {
    const item=this.values.get(key);if(!item)return null;const bytes=item.bytes.slice();
    return {size:bytes.length,body:new Response(bytes).body,httpMetadata:item.httpMetadata,customMetadata:item.customMetadata,
      arrayBuffer:async()=>bytes.buffer,text:async()=>new TextDecoder().decode(bytes),json:async()=>JSON.parse(new TextDecoder().decode(bytes))};
  }
}
