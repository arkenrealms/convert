import fixtureText from './x402-fixtures.json';
import providerText from './provider-fixtures.json';
import {x402Packet,paidConversionPacket,settlementReconciliationPacket} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {admitPayment,executePaymentEffect,sendFacilitator} from './x402_facilitator.js';
const f=JSON.parse(fixtureText).mainnetFixture,provider=JSON.parse(providerText);
const check=(value,label)=>{if(!value)throw Error(label);};
const packet=value=>{const result=x402Packet(JSON.stringify(value));check(!result.packetError,'mainnet C++ fixture rejected');return result;};
const control=(operation,event)=>packet({kind:'control',operation,event,now:'101',release:f.release}).command;
const decode=text=>Uint8Array.from(atob(text.replaceAll('-','+').replaceAll('_','/')),c=>c.charCodeAt(0));
export const mainnetProfile={async test(_controller,env){
  const admission=packet({kind:'prepare',quote:f.quote,payment:f.payment,now:'100'}),ledger=paymentLedger(env.LEDGER,admission.command);
  const c=provider.signingCases[0].prepare,credential={keyId:c.keyId,keySecret:c.keySecret};
  const material=provider.vectors[0].expected;
  const publicKey=await crypto.subtle.importKey('raw',new Uint8Array(material.publicKey),'Ed25519',false,['verify']);
  let verifies=0,settles=0;
  const fetcher=async(url,options)=>{
    check(options.method==='POST'&&options.redirect==='manual','mainnet transport method/redirect drift');new Request(url,options);
    check(url==='https://api.cdp.coinbase.com/platform/v2/x402/verify'||url==='https://api.cdp.coinbase.com/platform/v2/x402/settle','mainnet effect retargeted');
    const auth=options.headers.Authorization;check(auth?.startsWith('Bearer '),'CDP JWT missing');
    const jwt=auth.slice(7),parts=jwt.split('.');check(parts.length===3,'JWT encoding');
    check(await crypto.subtle.verify('Ed25519',publicKey,decode(parts[2]),new TextEncoder().encode(parts[0]+'.'+parts[1])),'CDP JWT signature invalid');
    const claims=JSON.parse(new TextDecoder().decode(decode(parts[1])));
    check(JSON.stringify(claims.uris)===JSON.stringify(['POST '+url.slice(8)])&&claims.sub===c.keyId,'JWT request binding mismatch');
    const body=JSON.parse(options.body);
    check(body.paymentRequirements.network==='eip155:8453'&&body.paymentRequirements.asset==='0x833589fcd6edb6e08f4c7c32d4f71b54bda02913','wrong quote profile');
    check(body.paymentRequirements.payTo==='0x554933cd7c5275ff3692bf97e0bc90b69c85d3dc'&&body.paymentRequirements.amount==='10000','recipient or amount changed');
    check(!options.body.includes(c.keySecret)&&await ledger.authorization(admission.inspectCommand)===admission.payment,'credential leak or non-durable authorization');
    if(url.endsWith('/verify')){verifies++;return new Response(f.verifiedResponse);}
    settles++;const current=await ledger.transition(admission.inspectCommand);check(current.operation.output!==null,'mainnet settled before committed output');
    return new Response(f.settledResponse);
  };
  const options={clock:()=> '101',send:request=>sendFacilitator(request,{cdpCredential:credential,fetcher})};
  if(env.PHASE==='write'){
    const verified=await admitPayment(env.LEDGER,f.quote,f.payment,options);check(verifies===1&&verified.decision.operation.authorization!==null,'CDP effect did not verify');
    const computing=await ledger.transition(control(verified.decision.operation,'resume'));
    const ready=await ledger.transition(packet({kind:'output',operation:computing.operation,output:JSON.parse(f.outputCommand).event.output,now:'101'}).command);
    const settling=await ledger.transition(control(ready.operation,'resume'));
    const effects=await Promise.all(Array.from({length:8},()=>executePaymentEffect(ledger,settling,options)));
    check(settles===1&&effects.some(r=>r.decision.operation.transfer!==null),'CDP effect duplicate or missing settlement');
    const noRecovery=settlementReconciliationPacket(JSON.stringify({operation:settling.operation,anchor:null,logs:null,receipt:null,now:'151'}));
    check(noRecovery.kind==='pending'&&noRecovery.code==='mainnet_reconciliation_not_configured'&&!noRecovery.request,'mainnet used unqualified recovery RPC');
    const effect=packet({kind:'effect',decision:f.settling,payment:f.payment,now:'101'});let requests=0,failed=false;
    try{await sendFacilitator(effect.request,{fetcher:async()=>{requests++;return new Response('{}');}});}catch{failed=true;}
    check(failed&&requests===0,'missing CDP key triggered unauthenticated request');
  }
  const observed=await ledger.transition(admission.inspectCommand);check(observed.operation.transfer!==null,'mainnet receipt lost on restart');
  await executePaymentEffect(ledger,f.settling,options);check(env.PHASE==='write'?settles===1:verifies===0&&settles===0,'retained effect replayed across restart');
  const receipt={file_id:f.quote.operation,download_url:'https://converter.example/output',expires_at:'2026-09-13T02:00:00.000Z',
    width:1,height:1,output_bytes:76,sha256:f.quote.input,media_type:'image/png',alpha_disposition:'preserved'};
  const delivered=paidConversionPacket(JSON.stringify({kind:'next',decision:observed,receipt,now:'151',release:'08'.repeat(32)}));
  check(delivered.result?._meta?.['x402/payment-response']?.network==='eip155:8453','mainnet receipt mislabeled testnet');
  console.log('Mainnet profile fixture passed: C++ quote/effect, real WebCrypto CDP auth, once-only settlement, correct retained receipt; network denied, provider/payment/output fixtures, not live revenue');
}};
