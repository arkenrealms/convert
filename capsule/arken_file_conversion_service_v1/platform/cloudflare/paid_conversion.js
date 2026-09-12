import {paidConversionPacket,planPaidRequest} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {executePaymentEffect} from './x402_facilitator.js';
import {reconcileSettlement} from './settlement_reconciliation.js';
const packet=value=>{const result=paidConversionPacket(JSON.stringify(value));if(result.packetError)throw Error(result.packetError);return result;};
const failure=code=>({isError:true,content:[{type:'text',text:JSON.stringify({code})}],structuredContent:{code}});
const clockSeconds=()=>String(Math.floor(Date.now()/1000));
async function binding(bucket,key){const object=await bucket.get(key+'/payment');return object?await object.text():null;}
async function outputReceipt(bucket,key){
  const output=await bucket.head(key+'/output');
  return output?.customMetadata?.receipt?JSON.parse(output.customMetadata.receipt):null;
}
// Thin platform effect interpreter. Quotes, transitions, retry choices and paid
// disclosure come exclusively from the private C++ controller.
export function createPaidConversion({clock=clockSeconds,send,readChain}={}){
  return {
    planRequest:planPaidRequest,
    async convert(plan,env,{found,owner,id,execute}){
      if(!env.PAYMENT_LEDGER||!env.RELEASE_ID)return failure('sandbox_payment_not_configured');
      const source=await env.FILES.head(found.key+'/input');
      if(!source)return failure('upload_required');
      if(source.size!==found.record.args.source_bytes)return failure('source_size_mismatch');
      const qualified=packet({kind:'quote',fileId:id,owner,inputDigest:source.customMetadata?.sha256,
        arguments:found.record.args,release:env.RELEASE_ID,expiresAt:String(Math.floor(found.record.expiresAt/1000))});
      const quote=qualified.quote;
      let bound=await binding(env.FILES,found.key);
      let prepared=packet({kind:'bind',quote,payment:plan.payment,binding:bound,now:clock()});
      if(prepared.kind!=='admit')return prepared.result;
      if(bound===null){
        // R2's immutable file binding chooses one payer/authorization per file;
        // the Durable Object remains the sole payment/nonce/dispatch authority.
        await env.FILES.put(found.key+'/payment',prepared.binding,{onlyIf:{etagDoesNotMatch:'*'}});
        bound=await binding(env.FILES,found.key);
        if(bound===null)return failure('payment_binding_unavailable');
        prepared=packet({kind:'bind',quote,payment:plan.payment,binding:bound,now:clock()});
        if(prepared.kind!=='admit')return prepared.result;
      }
      const ledger=paymentLedger(env.PAYMENT_LEDGER,prepared.command);
      let decision=await ledger.transition(prepared.command),receipt=await outputReceipt(env.FILES,found.key);
      for(let remaining=12;remaining>0;--remaining){
        const step=packet({kind:'next',decision,receipt,now:clock(),release:env.RELEASE_ID});
        if(step.kind==='result')return step.result;
        if(step.kind==='inspect'){decision=await ledger.transition(prepared.inspectCommand);receipt=await outputReceipt(env.FILES,found.key);continue;}
        if(step.kind==='transition'){decision=await ledger.transition(step.command);continue;}
        if(step.kind==='recover'){decision=await ledger.recover(step.command);receipt=await outputReceipt(env.FILES,found.key);continue;}
        if(step.kind==='reconcile'){
          const result=await reconcileSettlement(ledger,decision.operation,{clock,readChain});
          if(result.pending)return failure('settlement_pending_reconciliation');
          decision=result.decision;receipt=await outputReceipt(env.FILES,found.key);continue;
        }
        if(step.kind==='facilitate'){
          const effect=await executePaymentEffect(ledger,decision,{clock,send});
          if(effect.unavailable)return failure(effect.unavailable);decision=effect.decision;continue;
        }
        if(step.kind==='convert'){
          const dispatch=await ledger.claimDispatch(step.command,JSON.stringify(decision));
          if(!dispatch.claimed){decision=await ledger.transition(prepared.inspectCommand);receipt=await outputReceipt(env.FILES,found.key);continue;}
          const result=await execute(step.inputDigest,qualified.codec);
          if(result.code){decision=await ledger.transition(packet({kind:'fail',operation:decision.operation,now:clock()}).command);continue;}
          receipt=await outputReceipt(env.FILES,found.key);
          if(receipt===null)return failure('output_commit_pending');continue;
        }
        throw Error('unknown_paid_conversion_effect');
      }
      return failure('operation_step_limit');
    },
    async downloadAllowed(env,found){
      if(!env.PAYMENT_LEDGER||!env.RELEASE_ID)return false;
      const bound=await binding(env.FILES,found.key);if(bound===null)return false;
      const quote=JSON.parse(bound).quote;
      const prepared=packet({kind:'bind',quote,payment:null,binding:bound,now:clock()});
      const ledger=paymentLedger(env.PAYMENT_LEDGER,prepared.command);
      const decision=await ledger.transition(prepared.inspectCommand),receipt=await outputReceipt(env.FILES,found.key);
      const step=packet({kind:'next',decision,receipt,now:clock(),release:env.RELEASE_ID});
      return step.kind==='result'&&step.result._meta?.['x402/payment-response']?.success===true;
    }
  };
}
