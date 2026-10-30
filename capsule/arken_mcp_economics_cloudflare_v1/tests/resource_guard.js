// Fixture-only I/O around the exact production attempt adapter. Domain and
// payment decisions remain in the frozen native family package.
import {withAttemptBudget} from './mcp_attempt_gate.js';
export function resourceSubject(ordinary,guarded,factory,service){
 if(factory==='default'){
  if(typeof ordinary.default?.fetch!=='function'||typeof guarded.default?.fetch!=='function')throw Error('Resource default handler missing');
 }else if(typeof ordinary[factory]!=='function')throw Error('Resource factory missing');
 const exports=Object.keys(ordinary).filter(name=>name!=='default');
 if(exports.some(name=>ordinary[name]!==guarded[name]))throw Error('Resource named exports changed');
 let reservations=0,responses=0,thrown=0;
 const budget=Object.freeze({async reserve(target){
  if(target!==service)throw Error('Resource guard target changed');
  reservations++;return {status:204};
 }}),environments=new WeakMap();
 const environment=env=>{
  if(Object.hasOwn(env,'ARKEN_MCP_BUDGET'))throw Error('Resource fixture already has a budget');
  let value=environments.get(env);
  // A forwarding view preserves late revocation/control reads from the actual
  // fixture environment. Never patch or freeze the production authority.
  if(!value){value=new Proxy(env,{get(target,key,receiver){return key==='ARKEN_MCP_BUDGET'?budget:Reflect.get(target,key,receiver);}});environments.set(env,value);}
  return value;
 };
 const wrap=handler=>({...handler,async fetch(request,env,context){
  let response;
  try{response=await handler.fetch(request,environment(env),context);}catch(error){thrown++;throw error;}
  if(response.headers.get('X-Arken-Attempt-Budget')!=='admitted')throw Error('Resource request bypassed attempt admission');
  responses++;return response;
 }});
 return {
  createWorker:options=>{if(factory==='default')throw Error('Default profile cannot substitute a factory');return wrap(withAttemptBudget(ordinary[factory](options),service));},
  createDefaultWorker:()=>wrap(guarded.default),
  evidence(){if(reservations!==responses+thrown)throw Error('Resource admission count mismatch');
   return {service,namedExportIdentity:true,reservations,responses,thrown};}
 };
}
