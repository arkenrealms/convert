import {readBoundedText} from './bounded_body.js';
const headers={'content-type':'application/json','cache-control':'no-store','x-content-type-options':'nosniff'};
async function deadline(promise,ms){let timer;try{return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('report_timeout')),ms);})]);}finally{clearTimeout(timer);}}
// Optional private HTTP adapter. Native code owns grants, request semantics and
// record projection. It cannot call the payment runtime or a provider transport.
export function createPaidJournalReport({reportPacket,configurationBinding,reportControlBinding,ledgerBinding,
 selectActor=(namespace,plan)=>namespace.get(namespace.idFromName(plan.actorKey)),verifyPage=()=>true},{clock=()=>Math.floor(Date.now()/1000)}={}){
 let busy=false;
 return async function report(request,env){
  let acquired=false;
  try{
   const url=new URL(request.url),keys=[configurationBinding,reportControlBinding,ledgerBinding];
   const captured=keys.map(key=>env[key]),current=()=>keys.every((key,i)=>env[key]===captured[i]);
   const input=()=>({reportConfiguration:env[reportControlBinding]??'',serviceConfiguration:env[configurationBinding]??'',now:String(clock()),
    request:{method:request.method,path:url.pathname+url.search,origin:url.origin,hasOrigin:request.headers.has('origin'),
     authorization:request.headers.get('authorization')||'',contentType:request.headers.get('content-type')||''}});
   const denied=decision=>Response.json({error:decision.code},{status:decision.status,headers});
   const admission=reportPacket(11,input());
   if(admission.kind==='public')return null;
   if(admission.kind==='response')return denied(admission);
   if(admission.kind!=='report')throw Error('report_admission_invalid');
   if(busy)return Response.json({error:'journal_busy'},{status:429,headers});
   busy=true;acquired=true;
   const body=await readBoundedText(request.body,admission.bodyLimit,'report_body_limit',{timeoutMs:admission.deadlineMs});
   const plan=reportPacket(12,{...input(),body});
   if(plan.kind==='response')return denied(plan);
   if(!current()||plan.kind!=='read'||plan.grantId!==admission.grantId||plan.configurationSha256!==admission.configurationSha256)throw Error('report_context_changed');
   const namespace=env[ledgerBinding],actor=selectActor(namespace,plan);
   const page=await deadline(actor.report(plan.after,plan.limit,plan.configurationSha256),plan.deadlineMs);
   const confirmed=reportPacket(12,{...input(),body});
   if(confirmed.kind==='response')return denied(confirmed);
   if(!current()||JSON.stringify(confirmed)!==JSON.stringify(plan)||page.configurationSha256!==plan.configurationSha256||!verifyPage(page,plan))throw Error('report_context_changed');
   return Response.json(page,{status:200,headers});
  }catch(error){
   console.warn('Paid journal read unavailable',{code:'journal_report_unavailable'});
   return Response.json({error:'journal_report_unavailable'},{status:503,headers});
  }finally{if(acquired)busy=false;}
 };
}
