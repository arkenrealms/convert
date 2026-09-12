import module from './conversion.wasm';
// Platform ABI plumbing only. The codec and generated command validation are C++.
const instance=new WebAssembly.Instance(module,{});
const api=instance.exports;
api._initialize();
const encoder=new TextEncoder(), decoder=new TextDecoder();
export function planRequest(text) {
  return invokePacket(text,api.arken_mcp_plan);
}
export function planPaidRequest(text) {return invokePacket(text,api.arken_mcp_paid_plan);}
export function paidConversionPacket(text) {return invokePacket(text,api.arken_paid_conversion_packet);}
// Private actor/storage integration only; this is not exposed as an MCP tool.
export function advancePayment(text) {
  return invokePacket(text,api.arken_payment_advance);
}
export function paymentStorageLookup(text) {return invokePacket(text,api.arken_payment_storage_lookup);}
export function paymentStorageAdvance(text) {return invokePacket(text,api.arken_payment_storage_advance);}
export function x402Packet(text) {return invokePacket(text,api.arken_x402_packet);}
export function paymentStorageDispatch(text) {return invokePacket(text,api.arken_payment_storage_dispatch);}
function invokePacket(text,invoke) {
  const bytes=encoder.encode(text);
  const pointer=api.arken_mcp_prepare(bytes.length);
  if(!pointer)throw Error('request_limit');
  new Uint8Array(api.memory.buffer,pointer,bytes.length).set(bytes);
  invoke();
  return JSON.parse(decoder.decode(new Uint8Array(api.memory.buffer,api.arken_mcp_output(),api.arken_mcp_output_size())));
}
export function convertImage(bytes,codec) {
  if(!api.arken_conversion_prepare(bytes.byteLength))throw Error('conversion_busy_or_input_limit');
  try {
    new Uint8Array(api.memory.buffer,api.arken_conversion_input(),bytes.byteLength).set(new Uint8Array(bytes));
    const error=api.arken_conversion_run(codec.input,codec.output,codec.quality,codec.alpha);
    if(error) return {error,decodeError:api.arken_conversion_decode_error(),encodeError:api.arken_conversion_encode_error()};
    return {width:api.arken_conversion_width(),height:api.arken_conversion_height(),
      alphaFlattened:Boolean(api.arken_conversion_alpha_flattened()),
      bytes:new Uint8Array(api.memory.buffer,api.arken_conversion_output(),api.arken_conversion_output_size()).slice()};
  } finally {api.arken_conversion_release();}
}
