import {productionStartupPacket,productionQualificationPacket} from './runtime.js';
import {createCdpTransport} from './cdp_authorization.js';
import {readBoundedBody} from './bounded_body.js';
const clock=()=>String(Math.floor(Date.now()/1000));
const packet=input=>productionStartupPacket(JSON.stringify(input));
const qualifyPacket=input=>productionQualificationPacket(JSON.stringify({kind:'startup',input}));
export function createProductionStartup({fetcher=(url,options)=>fetch(url,options),clock:readClock=clock}={}) {
  const environments=new WeakMap();
  function entry(env){let value=environments.get(env);
    if(!value){value={files:env.FILES,ledger:env.PAYMENT_LEDGER,generation:1,cached:null,pending:null};environments.set(env,value);}
    if(value.files!==env.FILES||value.ledger!==env.PAYMENT_LEDGER){value.files=env.FILES;value.ledger=env.PAYMENT_LEDGER;value.generation++;value.cached=null;}
    return value;
  }
  function input(env,value){return {configuration:{enabled:env.PAYMENTS_ENABLED??null,keyId:env.CDP_API_KEY_ID??null,keySecret:env.CDP_API_KEY_SECRET??null,
    rpcClientKey:env.CDP_RPC_CLIENT_KEY??null,releaseId:env.RELEASE_ID??null,downloadSecret:env.ACCESS_TOKEN??null,
    hasFiles:Boolean(env.FILES),hasLedger:Boolean(env.PAYMENT_LEDGER),bindingGeneration:String(value.generation)},cached:value.cached,anonymous:null,supported:null,rpc:null,now:readClock()};}
  async function run(captured,decide){
    const sendCdp=createCdpTransport({keyId:captured.configuration.keyId,keySecret:captured.configuration.keySecret},{fetcher});
    for(let remaining=4;remaining>0;remaining--){captured.now=readClock();const next=decide(captured);
      if(next.kind!=='probe')return next;
      let response;
      try{
        if(next.request.authentication==='cdp')response=await sendCdp({...next.request,body:next.request.body||undefined});
        else {const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);
          try{const result=await fetcher(next.request.url,{method:next.request.method,body:next.request.body||undefined,headers:{'content-type':'application/json'},redirect:'manual',signal:controller.signal});
            response={status:result.status,body:new TextDecoder('utf-8',{fatal:true}).decode(await readBoundedBody(result.body,262144,'startup_response_limit'))};
          }finally{clearTimeout(timer);}
        }
      }catch(error){console.warn('production provider probe failed',{code:'provider_transport_unavailable'});response={status:0,body:''};}
      captured[next.slot]=response;
    }
    throw Error('production_startup_step_limit');
  }
    async function ensure(env,decide){const value=entry(env),captured=input(env,value),initial=decide(captured);
      if(initial.kind!=='probe')return {ready:initial.ready===true,code:initial.code};
      if(!value.pending){const pending=run(captured,decide).then(result=>{if(result.ready)value.cached=result.record;value.last={fingerprint:initial.fingerprint,result};return result;}).finally(()=>{if(value.pending===pending)value.pending=null;});value.pending=pending;}
      const completed=await value.pending;
      // Reassess fresh values: a late result cannot admit changed credentials,
      // release or binding identities. A new call may qualify the new context.
      const current=entry(env);let result=decide(input(env,current));
      if(result.kind==='probe'&&result.fingerprint===initial.fingerprint&&completed.kind==='unavailable')result=completed;
      return {ready:result.ready===true,code:result.code};
    }
  return {
    status(env){const value=entry(env);let result=packet(input(env,value));
      if(result.kind==='probe'&&value.last?.fingerprint===result.fingerprint&&!value.last.result.ready)result=value.last.result;
      return {ready:result.ready===true,code:result.code};},
    ensure:env=>ensure(env,packet),
    async qualify(env,request){
      const gate=productionQualificationPacket(JSON.stringify(request));
      if(!gate.allowed)return {status:gate.status,body:{code:gate.code}};
      const result=await ensure(env,qualifyPacket);
      return {status:result.ready?200:503,body:{providerQualified:result.ready,code:result.code,charging:false,admissionReady:false,
        releaseId:env.RELEASE_ID||'unpublished',deploymentId:env.VERSION_METADATA?.id||'unpublished'}};
    }
  };
}
