import text from './x402-fixtures.json';
import {x402Packet,paidConversionPacket} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {admitPayment,executePaymentEffect,retryPaymentVerification,sendFacilitator} from './x402_facilitator.js';
const data=JSON.parse(text),f=data.fixture;
const check=(value,label)=>{if(!value)throw Error(label);};
const packet=value=>{const result=x402Packet(JSON.stringify(value));check(!result.packetError,'C++ protocol rejected fixture');return result;};
const control=(operation,event)=>packet({kind:'control',operation,event,now:'101',release:f.release}).command;
const prepared=(quote,payment)=>packet({kind:'prepare',quote,payment,now:'100'});
function variant(id){const quote=structuredClone(f.quote);quote.operation=id.repeat(32);const challenge=prepared(quote,null);
  const payment=JSON.parse(f.payment);payment.resource=challenge.result.structuredContent.resource;payment.accepted=challenge.result.structuredContent.accepts[0];
  payment.payload.authorization.nonce='0x'+id.repeat(32);return {quote,payment:JSON.stringify(payment)};}
export const x402Integration={async test(_controller,env){
  for(const vector of data.vectors)check(JSON.stringify(x402Packet(vector.input))===JSON.stringify(JSON.parse(vector.expected)),'native/Wasm x402 mismatch');
  for(const vector of data.paidVectors)check(JSON.stringify(paidConversionPacket(vector.input))===JSON.stringify(JSON.parse(vector.expected)),'native/Wasm paid-controller mismatch');
  const admission=prepared(f.quote,f.payment),ledger=paymentLedger(env.LEDGER,admission.command);
  const options={clock:()=> '101'};
  if(env.PHASE==='write'){
    const io=await sendFacilitator({url:'https://x402.org/facilitator/verify',body:'{}'},{fetcher:async(url,options)=>{
      check(url.endsWith('/verify')&&options.method==='POST'&&options.redirect==='error'&&options.signal,'facilitator HTTP options');
      return new Response('{"isValid":false}',{status:200});}});
    check(io.status===200&&io.body==='{"isValid":false}','facilitator stream decoding');
    const denied=await sendFacilitator({url:'https://x402.org/facilitator/verify',body:'{}'},{fetcher:async()=>new Response('denied',{status:403})});
    check(denied.status===403&&denied.body==='denied','HTTP status lost in transport');
    for(const bytes of [new Uint8Array(16385),new Uint8Array([195,40])]){
      let rejected=false;try{await sendFacilitator({url:'https://x402.org/facilitator/verify',body:'{}'},{fetcher:async()=>new Response(bytes)});}catch(error){rejected=true;}
      check(rejected,'unbounded or invalid UTF8 provider reply accepted');
    }
    let verifies=0,settles=0;
    options.send=async request=>{
      const body=JSON.parse(request.body);
      check(body.x402Version===2&&body.paymentRequirements.network==='eip155:84532','wrong facilitator protocol');
      check(body.paymentRequirements.amount==='10000','amount changed');
      check(await ledger.authorization(admission.inspectCommand)===admission.payment,'authorization not durable before request');
      if(request.url.endsWith('/verify')){verifies++;return {status:200,body:f.verifiedResponse};}
      if(request.url.endsWith('/settle')){settles++;return {status:200,body:f.settledResponse};}
      throw Error('unapproved facilitator URL');
    };
    const challenge=await admitPayment(env.LEDGER,f.quote,null,options);check(challenge.kind==='challenge'&&verifies===0,'unpaid request performed I/O');
    const verified=await admitPayment(env.LEDGER,f.quote,f.payment,options);
    check(verified.decision.operation.stage===f.verified.operation.stage&&verifies===1,'verification did not reach durable policy');
    await admitPayment(env.LEDGER,f.quote,f.payment,options);check(verifies===1,'duplicate verification');
    const computing=await ledger.transition(control(verified.decision.operation,'resume'));
    const output=JSON.parse(f.outputCommand).event.output;
    const outputCommand=packet({kind:'output',operation:computing.operation,output,now:'101'}).command;
    const ready=await ledger.transition(outputCommand),settling=await ledger.transition(control(ready.operation,'resume'));
    const replies=await Promise.all(Array.from({length:8},()=>executePaymentEffect(ledger,settling,options)));
    check(settles===1,'same admitted decision dispatched settlement twice');
    check(replies.some(x=>x.decision.operation.transfer!==null),'settlement callback not persisted');
    await executePaymentEffect(ledger,settling,options);check(settles===1,'old decision charged after receipt');

    const lost=variant('43');let lostCalls=0;
    const lostOptions={clock:()=> '101',send:async request=>{if(request.url.endsWith('/verify'))return {status:200,body:f.verifiedResponse};lostCalls++;throw Error('injected settlement response loss');}};
    const lossVerified=await admitPayment(env.LEDGER,lost.quote,lost.payment,lostOptions),lostLedger=paymentLedger(env.LEDGER,prepared(lost.quote,lost.payment).command);
    const lossComputing=await lostLedger.transition(control(lossVerified.decision.operation,'resume'));
    const lossReady=await lostLedger.transition(packet({kind:'output',operation:lossComputing.operation,output,now:'101'}).command);
    const lossSettling=await lostLedger.transition(control(lossReady.operation,'resume'));
    const unknown=await executePaymentEffect(lostLedger,lossSettling,lostOptions);
    check(unknown.decision.operation.stage!==lossSettling.operation.stage&&unknown.decision.operation.transfer===null,'lost settlement not unresolved');
    await executePaymentEffect(lostLedger,lossSettling,lostOptions);check(lostCalls===1,'lost settlement retried');

    const rejected=variant('44');let attempts=0;
    const invalid=await admitPayment(env.LEDGER,rejected.quote,rejected.payment,{clock:()=> '101',send:async()=>{attempts++;return {status:200,body:'{"isValid":false,"invalidReason":"invalid_signature"}'};}});
    check(invalid.decision.claim===null&&invalid.decision.operation.failure!==0&&attempts===1,'invalid signature claimed nonce');
    const unavailable=variant('45');let verificationCalls=0;
    const retryOptions={clock:()=> '101',send:async()=>{verificationCalls++;return verificationCalls===1?{status:503,body:''}:{status:200,body:f.verifiedResponse};}};
    const pending=await admitPayment(env.LEDGER,unavailable.quote,unavailable.payment,retryOptions);check(pending.unavailable,'verify outage was called invalid signature');
    const retryLedger=paymentLedger(env.LEDGER,prepared(unavailable.quote,unavailable.payment).command);
    const retry=await retryPaymentVerification(retryLedger,pending.decision.operation,retryOptions);
    check(verificationCalls===2&&retry.decision.operation.stage===f.verified.operation.stage,'explicit verification recovery');
    const expired=variant('46');let now='101';
    const late=await admitPayment(env.LEDGER,expired.quote,expired.payment,{clock:()=>now,send:async()=>{now='200';return {status:200,body:f.verifiedResponse};}});
    check(late.decision.error!==0&&late.decision.claim===null,'callback reused old clock');
    console.log('x402 mock-facilitator flow passed: private authorization, dispatch once, callbacks and failures');
  }else{
    const observed=await ledger.transition(admission.inspectCommand);
    check(observed.operation.transfer!==null,'receipt missing after restart');
    check(await ledger.authorization(admission.inspectCommand)===admission.payment,'authorization lost after restart');
    let requests=0;await executePaymentEffect(ledger,f.settling,{clock:()=> '101',send:async()=>{requests++;throw Error('unexpected repeated dispatch');}});
    check(requests===0,'old decision dispatched after process restart');
    console.log('x402 retained authorization and dispatch marker survived process restart');
  }
}};
