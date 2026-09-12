import fixtureText from './payment-actor-fixtures.json';
export {x402Integration} from './x402_actor_tests.js';
export {paidHttp} from './paid_http_tests.js';
export {recoveryPayments} from './payment_recovery_actor_tests.js';
import {ConversionPaymentLedger,commitPaymentTransition,commitPaymentDispatch,paymentLedger} from './payment_ledger.js';
import {paymentStorageLookup} from './runtime.js';

const fixtures=JSON.parse(fixtureText),s=fixtures.scenarios,c=fixtures.constants;
const check=(value,label)=>{if(!value)throw Error(label);};
const equal=(a,b,label)=>check(JSON.stringify(a)===JSON.stringify(b),label);
const rejected=async(promise,label,reason)=>{let failed=false;try{await promise;}catch(error){failed=true;check(error instanceof Error,label+' error shape');if(reason)check(error.message.includes(reason),label+' wrong failure');}check(failed,label);};

// Fault injection exists only in this isolated fixture subclass. The production
// actor exposes neither fault flags nor storage inspection/mutation methods.
export class Ledger extends ConversionPaymentLedger {
  constructor(ctx,env){super(ctx,env);this.instanceStamp=crypto.randomUUID();}
  async instance(){
    const previous=await this.ctx.storage.get('fixture-instance')??null;
    await this.ctx.storage.put('fixture-instance',this.instanceStamp);await this.ctx.storage.sync();
    return {previous,current:this.instanceStamp};
  }
  async rollback(command){return commitPaymentTransition(this.ctx.storage,command,plan=>{
    check(plan.decision.error===0&&plan.decision.changed&&plan.writes.length===2,'rollback point was not after both admitted writes');
    throw Error('injected precommit failure');});}
  async loseReply(command){await super.transition(command);throw Error('injected reply loss after commit');}
  async rollbackDispatch(command,decision){return commitPaymentDispatch(this.ctx.storage,command,decision,plan=>{
    check(plan.claimed&&plan.attemptKey&&plan.attemptValue,'dispatch rollback did not cover both writes');throw Error('injected dispatch rollback');});}
  async removeAttempt(command){
    const initial=paymentStorageLookup(JSON.stringify({command,record:null}));
    const record=await this.ctx.storage.get(initial.operationKey);
    const keys=paymentStorageLookup(JSON.stringify({command,record}));await this.ctx.storage.delete(keys.attemptKey);await this.ctx.storage.sync();
  }
  async loseSyncObservation(command){
    const storage=this.ctx.storage;
    return commitPaymentTransition({transaction:fn=>storage.transaction(fn),sync:async()=>{
      await storage.sync();throw Error('injected persistence acknowledgement loss');}},command);
  }
}

export const durablePayments={async test(_controller,env){
  const ledger=paymentLedger(env.LEDGER,s.main.commands.start);
  if(env.PHASE==='write'){
    const instance=await ledger.instance();check(instance.previous===null,'fixture state was not isolated');
    const starts=await Promise.all(Array.from({length:16},()=>ledger.transition(s.main.commands.start)));
    equal(starts.find(x=>x.error===0),s.main.expected.start,'concurrent winner');
    check(starts.filter(x=>x.error===0&&x.action===c.verify).length===1,'verification dispatched more than once');
    check(starts.filter(x=>x.error===c.staleRevision&&!x.changed&&x.action===0).length===15,'stale concurrent requests admitted');
    equal(await ledger.transition(s.main.commands.verified),s.main.expected.verified,'atomic nonce claim');
    const foreign=JSON.parse(s.reuse.commands.start);foreign.quote.scope.payer='11'.repeat(20);
    await rejected(ledger.transition(JSON.stringify(foreign)),'foreign payer in same actor');
    const injected=JSON.parse(s.reuse.commands.start);injected.operation={stage:7};
    await rejected(ledger.transition(JSON.stringify(injected)),'client-authored state');
    const reused=await ledger.transition(s.reuse.commands.start);check(reused.error===0,'second operation start');
    const reuseResult=await ledger.transition(s.reuse.commands.verified);
    check(reuseResult.error===c.nonceConflict&&!reuseResult.changed&&reuseResult.action===0,'nonce shared between purchases');
    for(const name of ['compute','output','settle','unknown'])equal(await ledger.transition(s.main.commands[name]),s.main.expected[name],name);
    const retries=await Promise.all(Array.from({length:16},()=>ledger.transition(s.main.commands.settle)));
    check(retries.every(x=>x.error===c.staleRevision&&x.action===0),'uncertain settlement was replayed');

    const rollback=paymentLedger(env.LEDGER,s.rollback.commands.start);
    await rollback.transition(s.rollback.commands.start);
    let effects=0;
    await rejected(rollback.rollback(s.rollback.commands.verified).then(()=>{effects++;}),'transaction failure','injected precommit failure');
    check(effects===0,'failed persistence exposed an action');
    equal(await rollback.transition(s.rollback.commands.inspectStart),s.rollback.expected.inspectStart,'operation and nonce rollback');
    await rollback.transition(s.rollbackReuse.commands.start);
    check((await rollback.transition(s.rollbackReuse.commands.verified)).error===0,'rolled-back nonce was consumed');

    const output=paymentLedger(env.LEDGER,s.lostOutput.commands.start);
    for(const name of ['start','verified','compute'])await output.transition(s.lostOutput.commands[name]);
    await rejected(output.loseReply(s.lostOutput.commands.output),'output acknowledgement loss');
    equal(await output.transition(s.lostOutput.commands.inspectOutput),s.lostOutput.expected.inspectOutput,'durable output survived lost reply');
    equal(await output.transition(s.lostOutput.commands.settle),s.lostOutput.expected.settle,'resume committed output');

    const sync=paymentLedger(env.LEDGER,s.lostSync.commands.start);
    for(const name of ['start','verified','compute','output'])await sync.transition(s.lostSync.commands[name]);
    await rejected(sync.loseSyncObservation(s.lostSync.commands.settle).then(()=>{effects++;}),'sync acknowledgement loss');
    check(effects===0,'failed sync exposed settlement action');
    equal(await sync.transition(s.lostSync.commands.inspectSettling),s.lostSync.expected.inspectSettling,'uncertain sync durable record');
    check((await sync.transition(s.lostSync.commands.settle)).action===0,'sync loss replayed settlement');
    console.log('Durable payments write phase passed: concurrency, nonce ownership, rollback and lost acknowledgements');
  }else if(env.PHASE==='read'){
    const instance=await ledger.instance();check(instance.previous!==null&&instance.previous!==instance.current,'actor not recreated after process exit');
    equal(await ledger.transition(s.main.commands.inspectStart),s.main.expected.inspectUnknown,'read current revision after process restart');
    equal(await ledger.transition(s.main.commands.reconciled),s.main.expected.reconciled,'trusted reconciliation');
    const receipts=await Promise.all(Array.from({length:16},()=>ledger.transition(s.main.commands.receipt)));
    for(const receipt of receipts)equal(receipt,s.main.expected.receipt,'receipt repeat after restart');
    check(receipts.every(x=>!x.changed&&x.action===c.receipt),'receipt retry changed ledger');
    const sync=paymentLedger(env.LEDGER,s.lostSync.commands.start);
    equal(await sync.transition(s.lostSync.commands.inspectSettling),s.lostSync.expected.inspectSettling,'settling record survives process restart');
    const reused=await ledger.transition(s.reuse.commands.verified);check(reused.error===c.nonceConflict,'nonce claim lost after process restart');
    console.log('Durable payments read phase passed: new actor instance, persisted operations/nonces, receipt replay');
  }else throw Error('invalid actor proof phase');
}};
