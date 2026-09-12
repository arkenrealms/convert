import {x402Packet} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {readBoundedBody} from './bounded_body.js';

const packet=value=>{const result=x402Packet(JSON.stringify(value));if(result.packetError)throw Error(result.packetError);return result;};
const noRelease='0x'+'00'.repeat(32);
const clockSeconds=()=>String(Math.floor(Date.now()/1000));
class FacilitatorResponseError extends Error {
  constructor(status,cause){super('facilitator_response_unavailable');this.httpStatus=status;this.name=String(cause?.name||'Error');}
}
// Explicit transport injection is for fixtures/CDP integration, never a fallback.
// C++ chooses the fixed sandbox URL; no caller-controlled endpoint is accepted.
export async function sendFacilitator(request,{fetcher=(url,options)=>fetch(url,options)}={}) {
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),30000);
  try {
    // Workers supports manual/follow, not Fetch's error mode. Returning the 3xx
    // response lets C++ reject it without sending authorization to another URL.
    const response=await fetcher(request.url,{method:'POST',headers:{'content-type':'application/json'},body:request.body,redirect:'manual',signal:controller.signal});
    try {
    const value=await readBoundedBody(response.body,16384,'facilitator_response_limit');
    return {status:response.status,body:new TextDecoder('utf-8',{fatal:true}).decode(value)};
    } catch(error){throw new FacilitatorResponseError(response.status,error);}
  } finally {clearTimeout(timer);}
}

export async function executePaymentEffect(ledger,decision,{clock=clockSeconds,send=sendFacilitator}={}) {
  const now=clock();
  const inspect=packet({kind:'control',operation:decision.operation,event:'inspect',now,release:noRelease}).command;
  const payment=await ledger.authorization(inspect);
  const effect=packet({kind:'effect',decision,payment,now});
  if(effect.effect==='none'||effect.effect==='rejected')return {decision};
  const dispatch=await ledger.claimDispatch(inspect,JSON.stringify(decision));
  if(!dispatch.claimed)return {decision:await ledger.transition(inspect),dispatched:false};
  let response;
  try{response=await send(effect.request);}catch(error){console.warn('facilitator transport unavailable');response={status:error instanceof FacilitatorResponseError?error.httpStatus:0,body:JSON.stringify({transportError:String(error?.name||'Error').slice(0,64)})};}
  const callback=packet({kind:'callback',operation:decision.operation,phase:effect.effect,response:response.body,status:response.status,now:clock()});
  if(callback.kind==='unavailable')return {decision,unavailable:callback.code};
  return {decision:await ledger.transition(callback.command)};
}

export async function admitPayment(namespace,quote,payment,options={}) {
  const prepared=packet({kind:'prepare',quote,payment,now:(options.clock||clockSeconds)()});
  if(prepared.kind==='challenge')return prepared;
  const ledger=paymentLedger(namespace,prepared.command);
  const decision=await ledger.transition(prepared.command);
  return executePaymentEffect(ledger,decision,options);
}
