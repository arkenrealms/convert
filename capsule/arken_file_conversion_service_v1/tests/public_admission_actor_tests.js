import text from './public-admission-fixtures.json';
import {publicAdmissionPacket} from './runtime.js';
const check=(value,label)=>{if(!value)throw Error(label);};
const command=(id,bytes=1,now=86400)=>JSON.stringify({kind:'reserve',operation:String(id).padStart(64,'0'),bytes:String(bytes),now:String(now)});
export const publicAdmission={async test(_controller,env){
  for(const vector of JSON.parse(text))check(JSON.stringify(publicAdmissionPacket(vector.input))===JSON.stringify(JSON.parse(vector.expected)),'native/Wasm admission mismatch');
  const ledger=env.LEDGER.get(env.LEDGER.idFromName('public-budget-fixture'));
  if(env.PHASE==='write'){
    let failed=false,effects=0;
    try{await ledger.rollbackPublic(command(0)).then(()=>{effects++;});}catch(error){failed=true;check(error.message.includes('injected public rollback'),'wrong public failpoint');}
    check(failed&&effects===0,'failed public commit exposed permission');
    check(!(await ledger.reservePublicUpload(command(0))).replayed,'rolled-back admission survived');
    failed=false;try{await ledger.losePublicReply(command(1)).then(()=>{effects++;});}catch(error){failed=true;}
    check(failed&&effects===0&&(await ledger.reservePublicUpload(command(1))).replayed,'lost admission reply double-reserved');
    failed=false;try{await ledger.losePublicSync(command(2)).then(()=>{effects++;});}catch(error){failed=true;}
    check(failed&&effects===0&&(await ledger.reservePublicUpload(command(2))).replayed,'unconfirmed persistence exposed permission or lost identity');
    for(let i=3;i<63;i++)check((await ledger.reservePublicUpload(command(i))).allowed,'budget denied early');
    const race=await Promise.all(Array.from({length:16},(_,i)=>ledger.reservePublicUpload(command(100+i))));
    check(race.filter(x=>x.allowed).length===1&&race.filter(x=>!x.allowed).length===15,'last public slot raced');
  }else{
    check(!(await ledger.reservePublicUpload(command(999))).allowed,'public budget lost across process restart');
    check((await ledger.reservePublicUpload(command(0))).replayed,'public reservation lost across restart');
    check((await ledger.reservePublicUpload(command(999,1,172800))).allowed,'public day rollover unavailable');
  }
}};
