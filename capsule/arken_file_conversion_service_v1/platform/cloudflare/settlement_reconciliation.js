import {settlementReconciliationPacket} from './runtime.js';
import {readBoundedBody} from './bounded_body.js';
export async function readSettlementChain(request){
  const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
  try{
    const response=await fetch(request.url,{method:'POST',headers:{'content-type':'application/json'},body:request.body,redirect:'manual',signal:abort.signal});
    const bytes=await readBoundedBody(response.body,262144,'chain_response_limit');
    return {status:response.status,body:new TextDecoder('utf-8',{fatal:true}).decode(bytes)};
  }finally{clearTimeout(timer);}
}
// Opaque C++ evidence/request plumbing only. No signature, write-RPC or second
// facilitator settlement is sent from this adapter.
export async function reconcileSettlement(ledger,operation,{clock,readChain=readSettlementChain}){
  const evidence={operation,anchor:null,logs:null,receipt:null,now:clock()};
  for(let remaining=4;remaining>0;--remaining){
    evidence.now=clock();const step=settlementReconciliationPacket(JSON.stringify(evidence));
    if(step.kind==='pending')return {pending:true,code:step.code};
    if(step.kind==='complete'){
      let decision;
      for(const command of step.commands){decision=await ledger.transition(command);if(decision.error!==0)break;}
      return {decision,evidence:step.evidence};
    }
    if(step.kind!=='rpc')throw Error('unknown_reconciliation_step');
    try{const response=await readChain(step.request);if(response.status!==200)return {pending:true,code:'chain_http_unavailable'};evidence[step.slot]=response.body;}
    catch(error){console.warn('settlement chain read unavailable');return {pending:true,code:'chain_transport_unavailable'};}
  }
  throw Error('reconciliation_step_limit');
}
