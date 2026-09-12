import {DurableObject} from 'cloudflare:workers';
import {paymentStorageLookup,paymentStorageAdvance,paymentStorageDispatch} from './runtime.js';

// Private binding only. Commands come from the trusted payment adapter,
// never directly from public MCP arguments. C++ owns keys, validation and state.
export function paymentLedger(namespace,command) {
  const keys=paymentStorageLookup(JSON.stringify({command,record:null}));
  if(keys.packetError)throw Error(keys.packetError);
  return namespace.get(namespace.idFromName(keys.actorKey));
}

// The optional synchronous precommit invariant is a private fault-injection seam;
// neither the Durable Object RPC nor a public route accepts this callback.
export async function commitPaymentTransition(storage,command,beforeCommit) {
  if(typeof command!=='string')throw Error('payment_command_text_required');
  const initial=paymentStorageLookup(JSON.stringify({command,record:null}));
  if(initial.packetError)throw Error(initial.packetError);
  const result=await storage.transaction(async tx=>{
    const record=await tx.get(initial.operationKey)??null;
    const keys=paymentStorageLookup(JSON.stringify({command,record}));
    if(keys.packetError)throw Error(keys.packetError);
    const scope=await tx.get(keys.scopeKey)??null;
    const claim=keys.nonceKey===null?null:await tx.get(keys.nonceKey)??null;
    const plan=paymentStorageAdvance(JSON.stringify({command,record,scope,claim}));
    if(plan.packetError)throw Error(plan.packetError);
    for(const write of plan.writes)await tx.put(write.key,write.value);
    if(beforeCommit){const value=beforeCommit(plan);if(value!==undefined)throw Error('precommit_invariant_must_be_synchronous');}
    return plan.decision;
  });
  await storage.sync(); // Never dispatch an action from an unconfirmed write.
  return result;
}

export class ConversionPaymentLedger extends DurableObject {
  async transition(command) {return commitPaymentTransition(this.ctx.storage,command);}
  async claimDispatch(command,decision) {
    const initial=paymentStorageLookup(JSON.stringify({command,record:null}));if(initial.packetError)throw Error(initial.packetError);
    const result=await this.ctx.storage.transaction(async tx=>{
      const record=await tx.get(initial.operationKey)??null;
      const keys=paymentStorageLookup(JSON.stringify({command,record}));if(keys.packetError)throw Error(keys.packetError);
      const scope=await tx.get(keys.scopeKey)??null,claim=keys.nonceKey===null?null:await tx.get(keys.nonceKey)??null;
      let dispatch=null;
      const first=paymentStorageDispatch(JSON.stringify({command,record,scope,claim,decision,dispatch}));
      if(first.packetError)throw Error(first.packetError);
      dispatch=await tx.get(first.key)??null;
      const plan=paymentStorageDispatch(JSON.stringify({command,record,scope,claim,decision,dispatch}));
      if(plan.packetError)throw Error(plan.packetError);
      if(plan.claimed)await tx.put(plan.key,plan.value);
      return {claimed:plan.claimed};
    });
    await this.ctx.storage.sync();return result;
  }
  async authorization(command) {
    const initial=paymentStorageLookup(JSON.stringify({command,record:null}));
    if(initial.packetError)throw Error(initial.packetError);
    return this.ctx.storage.transaction(async tx=>{
      const record=await tx.get(initial.operationKey)??null;
      const keys=paymentStorageLookup(JSON.stringify({command,record}));
      if(keys.packetError)throw Error(keys.packetError);
      const scope=await tx.get(keys.scopeKey)??null;
      const claim=keys.nonceKey===null?null:await tx.get(keys.nonceKey)??null;
      const plan=paymentStorageAdvance(JSON.stringify({command,record,scope,claim}));
      if(plan.packetError||plan.decision.error!==0||plan.writes.length!==0)throw Error('authorization_read_rejected');
      return await tx.get(keys.authorizationKey)??null;
    });
  }
}
