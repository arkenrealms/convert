import {ConversionPaymentLedger} from './payment_ledger.js';
import {createConversionJournalReporter} from './journal_report_runtime.js';
const reporter=createConversionJournalReporter();
const packet=(mode,input)=>{const value=reporter.packet(mode,input);if(value.packetError)throw Error('conversion_journal_unavailable');return value;};
// Read-only extension of the unchanged paid actor. The namespace's published
// class name remains ConversionPaymentLedger in the candidate composition.
export class ConversionJournalLedger extends ConversionPaymentLedger {
 async report(after,limit,configurationSha256){
  const configuration=this.env.CONVERT_REPORT_CONFIGURATION,namespace=this.env.PAYMENT_LEDGER;
  const context=packet(13,{serviceConfiguration:configuration});
  if(context.configurationSha256!==configurationSha256)throw Error('conversion_report_context_changed');
  const page=await this.ctx.storage.transaction(async tx=>{
   const scope=await tx.get('scope')??null;
   const plan=packet(9,{scope,after,limit});
   if(plan.actorKey!==null&&!this.ctx.id.equals(namespace.idFromName(plan.actorKey)))throw Error('conversion_report_actor_mismatch');
   const rows=await tx.list({prefix:plan.prefix,startAfter:plan.after,limit:plan.limit});
   return packet(10,{scope,after,limit,now:String(Math.floor(Date.now()/1000)),entries:Array.from(rows,([key,value])=>({key,value}))});
  });
  if(configuration!==this.env.CONVERT_REPORT_CONFIGURATION||namespace!==this.env.PAYMENT_LEDGER)throw Error('conversion_report_context_changed');
  return {...page,configurationSha256,namespaceId:context.namespaceId,actorId:this.ctx.id.toString()};
 }
}
