import text from './reconciliation-fixtures.json';
import {settlementReconciliationPacket} from './runtime.js';
import {paymentLedger} from './payment_ledger.js';
import {reconcileSettlement} from './settlement_reconciliation.js';
const data=JSON.parse(text);
const check=(value,label)=>{if(!value)throw Error(label);};
export const reconciliationVectors={async test(_controller,env){
  for(const vector of data.vectors)check(JSON.stringify(settlementReconciliationPacket(vector.input))===JSON.stringify(JSON.parse(vector.expected)),'native/Wasm reconciliation mismatch');
  const ledger=paymentLedger(env.LEDGER,data.scenario.inspect);
  if(env.PHASE==='write'){
    let decision;for(const command of data.scenario.commands)decision=await ledger.transition(command);
    check(decision.operation.stage===5,'fixture did not strand settling');
    const readChain=async request=>{
      const first=JSON.parse(request.body)[0];return {status:200,body:first.id===1?data.fixture.anchor:first.id===3?data.fixture.logs:data.fixture.receipt};
    };
    const result=await reconcileSettlement(ledger,decision.operation,{clock:()=> '140',readChain});
    check(result.decision.operation.stage===7&&result.decision.operation.revision==='7','stranded settling not reconciled');
  }else{
    const observed=await ledger.transition(data.scenario.inspect);
    check(observed.operation.stage===7&&observed.operation.revision==='7'&&observed.operation.transfer.transaction==='a'.repeat(64),'reconciled receipt did not survive process restart');
  }
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
