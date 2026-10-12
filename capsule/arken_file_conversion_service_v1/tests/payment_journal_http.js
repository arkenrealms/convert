// Assertions around the real native-owned payment transitions and report RPC.
// These credentials and storage mutation methods exist only in isolated proof.
import accessText from './payment-journal-access-fixtures.json';
import actorText from './payment-journal-actors.json';
import {ConversionJournalLedger} from './journal_ledger.js';
import {withConversionJournalReport} from './journal_worker.js';
import {paymentStorageLookup} from './runtime.js';
export class JournalLedger extends ConversionJournalLedger {
 async storedRows(){return Array.from(await this.ctx.storage.list());}
 async seedRows(rows){for(const [key,value] of rows)await this.ctx.storage.put(key,value);await this.ctx.storage.sync();return rows.length;}
 async instance(){
  const previous=await this.ctx.storage.get('fixture-instance')??null,current=crypto.randomUUID();
  await this.ctx.storage.put('fixture-instance',current);await this.ctx.storage.sync();return {previous,current};
 }
}
export async function proveConversionJournalHttp(env){
 let checks=0,actorCalls=0,now=150,fixtureStorageWrites=1;
 const check=(ok,label)=>{++checks;if(!ok)throw Error(label);};
 const equal=(a,b,label)=>check(JSON.stringify(a)===JSON.stringify(b),label);
 const seed=JSON.parse(accessText).seed,scenarios=JSON.parse(actorText);
 const keys=paymentStorageLookup(JSON.stringify({command:scenarios.main[0].command,record:null}));
 const id=env.PAYMENT_LEDGER.idFromName(keys.actorKey),actor=env.PAYMENT_LEDGER.get(id),actorId=id.toString();
 const instance=await actor.instance();
 check(env.PHASE==='write'?instance.previous===null:instance.previous!==null&&instance.previous!==instance.current,'Process restart identity');
 if(env.PHASE==='write'){
  for(const row of [...scenarios.main,...scenarios.second]){
   equal(await actor.transition(row.command),row.expected,'Frozen engine mainnet-profile transition');
   fixtureStorageWrites+=row.storageWrites;
  }
 }
 const before=JSON.stringify(await actor.storedRows());
 const originalNamespace=env.PAYMENT_LEDGER;
 const testEnv={...env,CONVERT_REPORT_CONTROL:seed.admission.reportConfiguration,
  PAYMENT_LEDGER:{idFromString:value=>originalNamespace.idFromString(value),get:value=>{++actorCalls;return originalNamespace.get(value);}}};
 const worker=withConversionJournalReport({fetch:async()=>new Response('public-route-retained',{status:202})},{clock:()=>now});
 const path=seed.admission.request.path,origin=seed.admission.request.origin;
 const request=({headers={},body=JSON.stringify({...seed.body,actorId}),method='POST',route=path}={})=>new Request(origin+route,{method,
  headers:{'content-type':'application/json',authorization:seed.admission.request.authorization,...headers},...(method==='POST'?{body}:{})});
 const call=options=>worker.fetch(request(options),testEnv);
 for(const options of [{headers:{authorization:''}},{headers:{authorization:'Bearer '+'f'.repeat(64)}},
  {headers:{origin}},{headers:{'content-type':'text/plain'}},{method:'GET'},{route:path+'?query=1'}])
  check((await call(options)).status===403,'Unauthorized report admitted');
 for(const body of ['{}','{"actorId":"bad","after":"","limit":1}',JSON.stringify({...seed.body,actorId,limit:17})])
  check((await call({body})).status===400,'Malformed page admitted');
 check((await call({body:' '.repeat(1025)})).status===503,'Oversized body admitted');
 check(actorCalls===0,'Denied request reached storage');
 check((await call({route:'/v1'})).status===202,'Public route intercepted');
 const response=await call({body:JSON.stringify({actorId,after:'',limit:1})}),first=await response.json();
 check(response.status===200&&response.headers.get('cache-control')==='no-store'&&!response.headers.has('access-control-allow-origin'),'Private response headers');
 check(first.schema==='arken.convert.payment-journal-page.v1'&&first.rows.length===1&&!first.reachedEnd&&first.nextCursor,'First page');
 const secondResponse=await call({body:JSON.stringify({actorId,after:first.nextCursor,limit:1})}),second=await secondResponse.json();
 check(secondResponse.status===200&&second.rows.length===1&&second.reachedEnd&&second.nextCursor===null,'Final page');
 const rows=[...first.rows,...second.rows];
 check(new Set(rows.map(row=>row.operation)).size===2&&rows.some(row=>row.stage==='settled')&&rows.some(row=>row.stage==='verifying'),'Retained payment stages');
 check(rows.every(row=>row.tool===null&&row.updatedAt===null&&row.classification==='unknown'&&Object.keys(row).length===13),'Invented metadata or leaked private fields');
 check(first.actorId===actorId&&first.namespaceId===JSON.parse(seed.admission.serviceConfiguration).namespaceId,'Returned actor identity');
 const emptyId=originalNamespace.idFromName('empty-fixture').toString();
 const emptyResponse=await call({body:JSON.stringify({...seed.body,actorId:emptyId})}),empty=await emptyResponse.json();
 check(emptyResponse.status===200&&empty.rows.length===0&&empty.actorKey===null,'Empty actor');
 const wrongId=originalNamespace.idFromName('wrong-scope-fixture');
 fixtureStorageWrites+=await originalNamespace.get(wrongId).seedRows(JSON.parse(before));
 check((await call({body:JSON.stringify({...seed.body,actorId:wrongId.toString()})})).status===503,'Foreign scope actor admitted');
 const currentCalls=actorCalls;
 const revokingBody=new ReadableStream({pull(controller){now=200;controller.enqueue(new TextEncoder().encode(JSON.stringify({...seed.body,actorId})));controller.close();}});
 check((await call({body:revokingBody})).status===403&&actorCalls===currentCalls,'Expiry while awaiting body reached storage');
 now=150;
 const control=testEnv.CONVERT_REPORT_CONTROL,namespace=testEnv.PAYMENT_LEDGER;
 testEnv.PAYMENT_LEDGER={idFromString:value=>originalNamespace.idFromString(value),get:()=>({report:async(...args)=>{
  const result=await actor.report(...args);testEnv.CONVERT_REPORT_CONTROL='';return result;
 }})};
 check((await call()).status===403,'Revocation after actor read leaked page');
 testEnv.CONVERT_REPORT_CONTROL=control;
 for(const changed of [{actorId:emptyId},{namespaceId:'f'.repeat(32)},{configurationSha256:'f'.repeat(64)}]){
  testEnv.PAYMENT_LEDGER={idFromString:value=>originalNamespace.idFromString(value),get:()=>({report:async(...args)=>({...await actor.report(...args),...changed})})};
  check((await call()).status===503,'Returned page identity mismatch admitted');
 }
 let release,entered;const started=new Promise(resolve=>{entered=resolve;});
 testEnv.PAYMENT_LEDGER={idFromString:value=>originalNamespace.idFromString(value),get:()=>({report:async(...args)=>{
  entered();await new Promise(resolve=>{release=resolve;});return actor.report(...args);
 }})};
 const pending=call();await started;
 check((await call()).status===429,'Concurrent report lease missing');release();
 check((await pending).status===200,'Retained report failed');
 testEnv.PAYMENT_LEDGER=namespace;
 check((await call()).status===200,'Report lease not released');
 equal(await actor.storedRows(),JSON.parse(before),'Report changed durable payment bytes');
 return {status:'passed',phase:env.PHASE,checks,rows:rows.length,actorCalls,storageUnchanged:true,
  storage:'workerd-disk-backed-durable-objects',originalPaidEngine:true,networkRequests:0,financialEffects:0,
  reportStorageWrites:0,fixtureStorageWrites,fixtureSeedingOnly:true,processRestartProved:env.PHASE==='read',productionDeployed:false};
}
