import {publicAdmissionPacket} from './runtime.js';
const packet=value=>{const result=publicAdmissionPacket(JSON.stringify(value));if(result.packetError)throw Error(result.packetError);return result;};
export function accessPlan(context,intent,fileId=null,fileToken=null){return packet({kind:'access',...context,intent,fileId,fileToken});}
export function fileOwner(fileId,fileToken){const result=packet({kind:'capability',fileId,fileToken});if(!result.allowed)throw Error(result.code);return result.owner;}
export function uploadAdmissionActor(namespace){const keys=packet({kind:'lookup'});return namespace.get(namespace.idFromName(keys.actorKey));}
export async function commitPublicReservation(storage,command,beforeCommit){
  const keys=packet({kind:'lookup'});
  const result=await storage.transaction(async tx=>{
    const record=await tx.get(keys.recordKey)??null,scope=await tx.get(keys.scopeKey)??null;
    const plan=packet({...JSON.parse(command),record,scope});
    for(const write of plan.writes)await tx.put(write.key,write.value);
    if(beforeCommit){const value=beforeCommit(plan);if(value!==undefined)throw Error('precommit_invariant_must_be_synchronous');}
    return {allowed:plan.allowed,replayed:plan.replayed,code:plan.code,retryAfterSeconds:plan.retryAfterSeconds};
  });
  await storage.sync();return result;
}
export async function reservePublicUpload(namespace,operation,bytes,now){
  if(!namespace)return {allowed:false,code:'public_admission_unavailable'};
  return uploadAdmissionActor(namespace).reservePublicUpload(JSON.stringify({kind:'reserve',operation,bytes:String(bytes),now:String(now)}));
}
