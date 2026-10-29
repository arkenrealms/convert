import {guarded,original} from './guard_entry.js';
import configurationText from './guard_configuration.json';
const configuration=JSON.parse(configurationText);
const check=(value,message)=>{if(!value)throw Error(message);};
const request=(method,body)=>new Request(configuration.endpoint,{
 method,headers:{'content-type':'application/json','accept':'application/json, text/event-stream','MCP-Protocol-Version':'2025-11-25','CF-Connecting-IP':'192.0.2.71',
  ...(configuration.fixtureAuthorization?{'Authorization':configuration.fixtureAuthorization}:{})},
 ...(body?{body:JSON.stringify(body)}:{})});
const shape=async response=>({status:response.status,headers:[...response.headers],body:await response.text()});
export async function qualify(){
 let effects=0;
 const environment=()=>{
  const env={...configuration.bindings};
  // Explicit test I/O boundaries: discovery may check their presence but must
  // not touch storage, providers or private service methods. Never a fallback.
  for(const name of configuration.unavailableBindings)env[name]=new Proxy(Object.freeze({}),{get(){++effects;throw Error('Discovery attempted downstream I/O');}});
  return env;
 };
 const names=Object.keys(original).filter(name=>name!=='default');
 check(JSON.stringify(Object.keys(guarded).filter(name=>name!=='default'))===JSON.stringify(names),'Named export closure');
 for(const name of names)check(original[name]===guarded[name],'Named export identity');
 const body={jsonrpc:'2.0',id:1,method:'tools/list',params:{}};
 let deniedInvocations=0;
 for(const mode of ['missing','unavailable','denied'])for(const payload of [body,{jsonrpc:'2.0',id:2,method:'tools/call',params:{name:'__qualification_unknown_tool__',arguments:{}}}]){
  let calls=0;
  const env=environment();
  if(mode!=='missing')env.ARKEN_MCP_BUDGET={reserve(service,caller){
   ++calls;check(service===configuration.service&&caller==='192.0.2.71','Fixed service and edge caller');
   if(mode==='unavailable')throw Error('Qualification dependency unavailable');
   return {status:429,body:'{"error":"attempt_budget_exhausted"}',retryAfterSeconds:'60'};
  }};
  const input=request('POST',payload),response=await guarded.default.fetch(input,env,{});
  check(response.status===(mode==='denied'?429:503)&&!input.bodyUsed,'Deny before body or downstream dispatch');
  check(calls===(mode==='missing'?0:1),'Exactly one admission attempt');
  await response.arrayBuffer();
  if(payload.method==='tools/call')++deniedInvocations;
 }
 let admitted=0;
 const binding={reserve(service,caller){check(service===configuration.service&&caller==='192.0.2.71','Admission identity');++admitted;return {status:204};}};
 const requests=[
  ['POST',{jsonrpc:'2.0',id:7,method:'initialize',params:{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'Arken qualification',version:'1'}}}],
  ['POST',body],['POST',{jsonrpc:'2.0',id:9,method:'not-a-method',params:{}}],['GET',null],
  ['POST',{jsonrpc:'2.0',id:10,method:'tools/call',params:{name:'__qualification_unknown_tool__',arguments:{}}}]
 ];
 const statuses=[],liveCases=[];
 for(const [method,payload] of requests){
  const env=environment();
  const a=await shape(await original.default.fetch(request(method,payload),env,{}));
  const b=await shape(await guarded.default.fetch(request(method,payload),{...env,ARKEN_MCP_BUDGET:binding},{}));
  check(b.headers.some(([key,value])=>key==='x-arken-attempt-budget'&&value==='admitted'),'Admission response provenance');
  b.headers=b.headers.filter(([key])=>key!=='x-arken-attempt-budget');
  check(JSON.stringify(a)===JSON.stringify(b),'Production response, status and header parity');
  if(payload?.method==='initialize'||payload?.method==='tools/list'){
   check(b.status===200,'Discovery positive control must succeed');
   const message=JSON.parse(b.body);
   check(message.id===payload.id&&message.result&&!message.error,'MCP discovery result required');
   if(payload.method==='tools/list')check(Array.isArray(message.result.tools),'Actual tool catalog required');
  }
  statuses.push(b.status);
  const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(b.body)));
  liveCases.push({method,body:payload,status:b.status,bodySha256:[...digest].map(n=>n.toString(16).padStart(2,'0')).join('')});
 }
 check(admitted===requests.length,'Every forwarded attempt charged once');
 check(effects===0,'No downstream storage or private service effects');
 return {status:'passed',service:configuration.service,
  admissionBeforeEffects:true,originalResponseParity:true,namedExportIdentity:true,
  admittedAttempts:admitted,deniedInvocations,responseStatuses:statuses,liveCases,discoveryPositiveControl:true,downstreamEffects:effects,paymentSubmissions:0};
}
export default {async test(){console.log('ARKEN_GUARD_PROOF='+JSON.stringify(await qualify()));}};
