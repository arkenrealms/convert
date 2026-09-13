import text from './reconciliation-fixtures.json';
import {settlementReconciliationPacket} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {reconcileSettlement} from './settlement_reconciliation.js';
const data=JSON.parse(text);
const check=(value,label)=>{if(!value)throw Error(label);};
export const reconciliationVectors={async test(_controller,env){
  for(const vector of data.vectors)check(JSON.stringify(settlementReconciliationPacket(vector.input))===JSON.stringify(JSON.parse(vector.expected)),'native/Wasm reconciliation mismatch');
  for(const mainnet of [false,true]){
    const fixture=mainnet?data.mainnetFixture:data.fixture,scenario=mainnet?data.mainnetScenario:data.scenario;
    const ledger=paymentLedger(env.LEDGER,scenario.inspect);
    if(env.PHASE==='write'){
      let decision;for(const command of scenario.commands)decision=await ledger.transition(command);
      check(decision.operation.stage===5,'fixture did not strand settling');
      let reads=0;
      const readChain=async request=>{
        reads++;const expected=mainnet?'https://api.developer.coinbase.com/rpc/v1/base/'+fixture.rpcClientKey:'https://sepolia.base.org';
        check(request.url===expected,'wrong recovery provider or changed captured key');
        const calls=JSON.parse(request.body),first=calls[0];
        check(calls.every(c=>['eth_chainId','eth_getBlockByNumber','eth_getLogs','eth_getTransactionReceipt'].includes(c.method)),'recovery emitted a transaction');
        if(first.id===3)check(first.params[0].address.slice(2)===fixture.operation.quote.scope.asset,'wrong profile token filter');
        return {status:200,body:first.id===1?fixture.anchor:first.id===3?fixture.logs:fixture.receipt};
      };
      if(mainnet){
        const missing=await reconcileSettlement(ledger,decision.operation,{clock:()=> '140',readChain});
        check(missing.pending&&missing.code==='mainnet_reconciliation_not_configured'&&reads===0,'missing key issued RPC');
        let deniedReads=0;
        const denied=await reconcileSettlement(ledger,decision.operation,{clock:()=> '140',rpcClientKey:fixture.rpcClientKey,readChain:async()=>{deniedReads++;return {status:403,body:'denied'};}});
        check(denied.pending&&deniedReads===1,'provider denial retried or fell back');
        let wrongReads=0;const wrong=JSON.parse(fixture.anchor);wrong.find(r=>r.id===1).result='0x14a34';
        const mismatch=await reconcileSettlement(ledger,decision.operation,{clock:()=> '140',rpcClientKey:fixture.rpcClientKey,readChain:async()=>{wrongReads++;return {status:200,body:JSON.stringify(wrong)};}});
        check(mismatch.pending&&wrongReads===1&&(await ledger.transition(scenario.inspect)).operation.stage===5,'wrong chain mutated ledger');
      }
      const options={clock:()=> '140',readChain,rpcClientKey:fixture.rpcClientKey};
      const first=reconcileSettlement(ledger,decision.operation,options);options.rpcClientKey='changed_while_request_awaits';
      const results=await Promise.all([first,reconcileSettlement(ledger,decision.operation,{...options,rpcClientKey:fixture.rpcClientKey})]);
      const current=await ledger.transition(scenario.inspect);
      check(current.operation.stage===7&&current.operation.revision==='7'&&reads===6,'concurrent stranded settling not reconciled once');
      check(results.some(r=>r.evidence?.chain===(mainnet?8453:84532)),'wrong recovered chain');
      check(!JSON.stringify(results).includes('fixture_cdp_client_1234'),'client key leaked in recovery result');
    }else{
      const observed=await ledger.transition(scenario.inspect);
      check(observed.operation.stage===7&&observed.operation.revision==='7'&&observed.operation.transfer.transaction==='a'.repeat(64),'reconciled receipt did not survive process restart');
    }
  }
  console.log('Sepolia and configured CDP-mainnet recovery passed: scoped reads, concurrent positive completion, missing-key/denied/wrong-chain no-fallback, retained restart receipt; RPC data is fixture-only');
}};
// RPC fixture construction only; the real C++ verifier interprets this evidence.
export function chainFixture(payment,now){
  const anchor=JSON.parse(data.fixture.anchor),logs=JSON.parse(data.fixture.logs),receipt=JSON.parse(data.fixture.receipt);
  const authorization=payment.payload.authorization;
  const topic=address=>'0x'+'0'.repeat(24)+address.slice(2).toLowerCase();
  const stamp='0x'+now.toString(16);
  anchor[0].result.timestamp=stamp;receipt[0].result.timestamp=stamp;receipt[2].result.timestamp=stamp;
  logs[0].result[0].topics[1]=topic(authorization.from);logs[0].result[0].topics[2]=authorization.nonce.toLowerCase();
  receipt[1].result.logs[0]=structuredClone(logs[0].result[0]);
  receipt[1].result.logs[1].topics[1]=topic(authorization.from);receipt[1].result.logs[1].topics[2]=topic(authorization.to);
  return request=>{
    check(request.url==='https://sepolia.base.org','unapproved reconciliation endpoint');
    const body=JSON.parse(request.body);
    check(body.every(r=>['eth_chainId','eth_getBlockByNumber','eth_getLogs','eth_getTransactionReceipt'].includes(r.method)),'reconciliation sent write RPC');
    if(body[0].id===1)return {status:200,body:JSON.stringify(anchor)};
    if(body[0].id===3){check(body[0].params[0].topics[1]===topic(authorization.from)&&body[0].params[0].topics[2]===authorization.nonce.toLowerCase(),'wrong nonce search');return {status:200,body:JSON.stringify(logs)};}
    check(body[0].id===4,'unknown RPC phase');return {status:200,body:JSON.stringify(receipt)};
  };
}
