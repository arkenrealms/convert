// Shared HTTP transport boundary, placed before body reads, authentication,
// parsing, readiness, payment effects and computation. No fallback on failure.
export function withAttemptBudget(handler,service){
 return {...handler,async fetch(request,env,context){
  let timer;
  try{
   const binding=env.ARKEN_MCP_BUDGET;
   if(!binding)throw Error('attempt_budget_binding_missing');
   const verdict=await Promise.race([binding.reserve(service,request.headers.get('CF-Connecting-IP')||''),
    new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('attempt_budget_deadline')),2000);})]);
   if(binding!==env.ARKEN_MCP_BUDGET)throw Error('attempt_budget_binding_changed');
   if(verdict.status!==204)return new Response(verdict.body,{status:verdict.status,headers:{
    'content-type':'application/json','cache-control':'no-store',...(verdict.status===429?{'retry-after':verdict.retryAfterSeconds}:{})}});
  }catch(error){
   console.warn('MCP attempt admission unavailable');
   return Response.json({error:'attempt_budget_unavailable'},{status:503,headers:{'cache-control':'no-store'}});
  }finally{clearTimeout(timer);}
  const response=await handler.fetch(request,env,context);
  const headers=new Headers(response.headers);
  headers.set('X-Arken-Attempt-Budget','admitted');
  return new Response(response.body,{status:response.status,statusText:response.statusText,headers});
 }};
}
