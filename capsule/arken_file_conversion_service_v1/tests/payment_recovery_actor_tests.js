import fixtureText from './payment-actor-fixtures.json';
import {paymentLedger} from './payment_ledger.js';
const scenarios=JSON.parse(fixtureText).scenarios;
const check=(value,label)=>{if(!value)throw Error(label);};
const at=(text,now,revision)=>{const command=JSON.parse(text);command.event.now=String(now);if(revision!==undefined)command.event.expectedRevision=String(revision);command.event.executingRelease=command.quote.release;return JSON.stringify(command);};
export const recoveryPayments={async test(_controller,env){
  for(const [name,stage,inspectName,deadline] of [['recoverVerify',1,'inspectStart',135],['recoverCompute',3,'inspectComputing',130]]){
    const scenario=scenarios[name],commands=scenario.commands,ledger=paymentLedger(env.LEDGER,commands.start),inspect=commands[inspectName];
    if(env.PHASE==='write'){
      let decision=await ledger.transition(commands.start);
      if(stage===3){await ledger.transition(commands.verified);decision=await ledger.transition(commands.compute);}
      const claims=await Promise.all(Array.from({length:16},()=>ledger.claimDispatch(inspect,JSON.stringify(decision))));
      check(claims.filter(x=>x.claimed).length===1,'attempt claim duplicated');
      const waiting=await ledger.recover(at(inspect,deadline-1));check(waiting.action===4&&!waiting.changed&&waiting.operation.revision===decision.operation.revision,'in-flight effect retried');
    }else{
      const early=await ledger.recover(at(inspect,deadline-1));check(early.action===4&&!early.changed,'attempt deadline not durable');
      const tries=await Promise.all(Array.from({length:16},()=>ledger.recover(at(inspect,deadline))));
      const winner=tries.find(x=>x.changed);check(winner&&tries.filter(x=>x.changed).length===1,'recovery revision raced');
      check(tries.filter(x=>x.error===3&&!x.changed).length===15,'recovery ignored expected revision');
      check(winner.operation.stage===stage&&winner.action===(stage===1?1:2),'wrong recovery action');
      check((await ledger.transition(stage===1?commands.verified:commands.output)).error===3,'late callback changed current attempt');
      const current=at(inspect,deadline,winner.operation.revision);
      const claims=await Promise.all(Array.from({length:16},()=>ledger.claimDispatch(current,JSON.stringify(winner))));
      check(claims.filter(x=>x.claimed).length===1,'recovered effect claimed twice');
      const expires=await ledger.recover(at(current,150));check(expires.action===7&&expires.operation.stage===8&&expires.operation.failure===2,'expired recovery not no-charge');
    }
  }
  const absent=scenarios.recoverAbsent,ledger=paymentLedger(env.LEDGER,absent.commands.start);
  if(env.PHASE==='write'){
    const decision=await ledger.transition(absent.commands.start);
    let threw=false;try{await ledger.rollbackDispatch(absent.commands.inspectStart,JSON.stringify(decision));}catch(error){threw=true;check(error.message.includes('injected dispatch rollback'),'unexpected rollback failure');}
    check(threw,'dispatch failpoint did not run');
    const recovered=await ledger.recover(at(absent.commands.inspectStart,100));
    check(recovered.changed&&recovered.action===1,'rolled-back marker or attempt survived');
    check(!(await ledger.claimDispatch(absent.commands.inspectStart,JSON.stringify(decision))).claimed,'undispatched old revision reclaimed');
    const legacy=scenarios.recoverLegacy,old=paymentLedger(env.LEDGER,legacy.commands.start),started=await old.transition(legacy.commands.start);
    await old.claimDispatch(legacy.commands.inspectStart,JSON.stringify(started));await old.removeAttempt(legacy.commands.inspectStart);
    const observed=await old.recover(at(legacy.commands.inspectStart,105));check(!observed.changed&&observed.action===4,'old marker retried immediately');
  }else{
    const legacy=scenarios.recoverLegacy,old=paymentLedger(env.LEDGER,legacy.commands.start);
    check(!(await old.recover(at(legacy.commands.inspectStart,139))).changed,'old marker observation did not persist');
    check((await old.recover(at(legacy.commands.inspectStart,140))).changed,'old marker observation extended itself');
  }
  console.log('Recovery '+env.PHASE+': durable deadlines, 16-way revision and dispatch races, stale callbacks, expiry, rollback and old-marker adoption passed; zero settlement recovery.');
}};
