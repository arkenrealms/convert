import {createPaidJournalReport} from './report_http_transport.js';
import {createConversionJournalReporter} from './journal_report_runtime.js';
export function withConversionJournalReport(worker,options){
 const reporter=createConversionJournalReporter();
 const report=createPaidJournalReport({reportPacket:reporter.packet,
  configurationBinding:'CONVERT_REPORT_CONFIGURATION',reportControlBinding:'CONVERT_REPORT_CONTROL',ledgerBinding:'PAYMENT_LEDGER',
  selectActor:(namespace,plan)=>namespace.get(namespace.idFromString(plan.actorId)),
  verifyPage:(page,plan)=>page.actorId===plan.actorId&&page.namespaceId===plan.namespaceId},options);
 return {...worker,journalReportMemoryBytes:reporter.memoryBytes,async fetch(request,env){
  return await report(request,env)??worker.fetch(request,env);
 }};
}
