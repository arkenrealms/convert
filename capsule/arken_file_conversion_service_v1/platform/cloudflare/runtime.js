import module from './conversion.wasm';
// Platform ABI plumbing only. The codec and generated command validation are C++.
const instance=new WebAssembly.Instance(module,{});
const api=instance.exports;
api._initialize();
const encoder=new TextEncoder(), decoder=new TextDecoder();
export function planRequest(text) {
  return invokePacket(text,api.arken_mcp_plan);
}
export function planSandboxRequest(text) {return invokePacket(text,api.arken_mcp_sandbox_plan);}
export function planProductionRequest(text) {return invokePacket(text,api.arken_mcp_production_plan);}
export function serverSurface(text) {return invokePacket(text,api.arken_server_surface);}
export function productionStartupPacket(text) {return invokePacket(text,api.arken_production_startup,api.arken_settlement_prepare,true);}
export function productionQualificationPacket(text) {return invokePacket(text,api.arken_production_qualification,api.arken_settlement_prepare,true);}
export function paidConversionPacket(text) {return invokePacket(text,api.arken_paid_conversion_packet);}
// Private actor/storage integration only; this is not exposed as an MCP tool.
export function advancePayment(text) {
  return invokePacket(text,api.arken_payment_advance);
}
export function paymentStorageLookup(text) {return invokePacket(text,api.arken_payment_storage_lookup);}
export function paymentStorageAdvance(text) {return invokePacket(text,api.arken_payment_storage_advance);}
export function x402Packet(text) {return invokePacket(text,api.arken_x402_packet);}
export function paymentStorageDispatch(text) {return invokePacket(text,api.arken_payment_storage_dispatch);}
export function paymentStorageRecover(text) {return invokePacket(text,api.arken_payment_storage_recover);}
export function publicAdmissionPacket(text) {return invokePacket(text,api.arken_public_admission_packet);}
export function cdpProviderPacket(text) {return invokePacket(text,api.arken_cdp_provider_packet,api.arken_mcp_prepare,true);}
export function settlementReconciliationPacket(text) {return invokePacket(text,api.arken_settlement_reconciliation_packet,api.arken_settlement_prepare,true);}
function invokePacket(text,invoke,prepare=api.arken_mcp_prepare,sensitive=false) {
  const bytes=encoder.encode(text);
  const pointer=prepare(bytes.length);
  try {
  if(!pointer)throw Error('request_limit');
  new Uint8Array(api.memory.buffer,pointer,bytes.length).set(bytes);
  invoke();
  return JSON.parse(decoder.decode(new Uint8Array(api.memory.buffer,api.arken_mcp_output(),api.arken_mcp_output_size())));
  } finally {if(sensitive){bytes.fill(0);api.arken_mcp_clear();}}
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
