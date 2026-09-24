#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
namespace P=arken::file_conversion::service::v1::payment;
using Json=nlohmann::json;
unsigned PaymentRecoveryTests(){
  unsigned count=0;const auto check=[&](bool value,const char* label){++count;if(!value)throw std::runtime_error(label);};
  P::Operation op;op.quote.scope.chain=84532;op.quote.scope.asset[0]=1;op.quote.scope.payer[0]=2;op.quote.recipient[0]=3;
  op.quote.amount=10000;op.quote.expiresAt=200;op.quote.release[0]=4;op.stage=P::Stage::verifying;op.revision=1;
  op.authorization=P::Authorization{.validAfter=90,.validBefore=150};
  P::Event e;e.kind=P::EventKind::inspect;e.expectedRevision=1;e.now=100;e.executingRelease=op.quote.release;
  auto r=P::Recover(op,{},e,{});check(r.decision.changed&&r.decision.action==P::Action::verify&&r.decision.operation.revision==2,"undispatched verify recovery");
  r=P::Recover(op,{},e,{true,135});check(!r.decision.changed&&r.decision.action==P::Action::wait&&!r.observationDeadline,"active verification replayed");
  r=P::Recover(op,{},e,{true,{}});check(r.observationDeadline==135&&r.decision.action==P::Action::wait,"old marker did not gain conservative deadline");
  e.now=134;r=P::Recover(op,{},e,{true,135});check(!r.decision.changed,"deadline boundary early");
  e.now=135;r=P::Recover(op,{},e,{true,135});check(r.decision.changed&&r.decision.operation.revision==2,"deadline boundary late");
  e.expectedRevision=0;r=P::Recover(op,{},e,{true,135});check(r.decision.error==P::Error::staleRevision&&!r.decision.changed,"recovery discarded revision fence");e.expectedRevision=1;
  for(auto now:{0u,90u}){e.now=now;r=P::Recover(op,{},e,{true,135});check(r.decision.action==P::Action::wait&&!r.observationDeadline,"clock backwards extended deadline");}
  for(auto now:{150u,200u}){e.now=now;r=P::Recover(op,{},e,{true,135});check(r.decision.operation.failure==P::Failure::expired&&r.decision.action==P::Action::failure,"expiry not no-charge");}
  e.now=100;
  for(const auto attempt:{P::Attempt{false,135},P::Attempt{true,90},P::Attempt{true,151}}){r=P::Recover(op,{},e,attempt);check(r.decision.error==P::Error::invalidRecord&&!r.decision.changed,"invalid attempt accepted");}
  check(P::AttemptDeadline(op,140)==150,"deadline exceeded authorization");
  auto large=op;large.authorization->validBefore=large.quote.expiresAt=std::numeric_limits<std::uint64_t>::max();
  check(P::AttemptDeadline(large,large.quote.expiresAt-2)==large.quote.expiresAt,"deadline overflow");
  op.stage=P::Stage::computing;P::NonceClaim claim{op.quote.scope,op.authorization->nonce,op.quote.operation,op.authorization->payload};
  check(P::AttemptDeadline(op,100)==130,"compute budget");
  e.now=130;r=P::Recover(op,claim,e,{true,130});check(r.decision.action==P::Action::convert&&r.decision.changed,"compute recovery not admitted");
  auto stale=e;stale.kind=P::EventKind::outputCommitted;stale.output=P::Output{.release=op.quote.release,.bytes=70,.width=1,.height=1};
  check(P::Advance(r.decision.operation,op.quote,claim,stale).error==P::Error::staleRevision,"late compute callback accepted");
  r=P::Recover(op,{},e,{});check(r.decision.error==P::Error::nonceClaimMissing,"recovery bypassed nonce");
  e.executingRelease[0]++;r=P::Recover(op,claim,e,{true,140});check(r.decision.operation.failure==P::Failure::releaseUnavailable&&r.decision.action==P::Action::failure,"release mismatch not no-charge");e.executingRelease=op.quote.release;
  auto overflow=op;overflow.revision=e.expectedRevision=std::numeric_limits<std::uint64_t>::max();r=P::Recover(overflow,claim,e,{});check(r.decision.error==P::Error::revisionOverflow,"recovery revision overflow");e.expectedRevision=1;
  for(auto stage:{P::Stage::authorized,P::Stage::outputReady,P::Stage::settling,P::Stage::settlementUnknown,P::Stage::settled,P::Stage::failedNoCharge,P::Stage::settlementFailed}){
    auto value=op;value.stage=stage;
    if(stage>=P::Stage::outputReady&&stage!=P::Stage::failedNoCharge)value.output=P::Output{.release=op.quote.release,.bytes=70,.width=1,.height=1};
    if(stage==P::Stage::settled){value.transfer=P::Transfer{.scope=op.quote.scope,.recipient=op.quote.recipient,.amount=10000};value.transfer->transaction[0]=1;}
    if(stage==P::Stage::failedNoCharge)value.failure=P::Failure::expired;
    if(stage==P::Stage::settlementFailed)value.failure=P::Failure::settlementRejected;
    for(auto now:{100u,1000u}){e.now=now;r=P::Recover(value,claim,e,{});check(r.decision.error==P::Error::none&&r.decision.action!=P::Action::settle,"recovery admitted settlement or invalid fixture");}
  }
  e.now=100;op.stage=P::Stage::verifying;
  const auto encoded=Json::parse(P::InputPacket(op,op.quote,{},e));
  const std::string command=Json{{"version",1},{"quote",encoded.at("quote")},{"event",encoded.at("event")}}.dump();
  Json packet={{"command",command},{"record",Json{{"version",1},{"operation",encoded.at("operation")}}.dump()},
    {"scope",encoded.at("quote").at("scope").dump()},{"claim",nullptr},{"dispatch",nullptr},{"attempt",nullptr}};
  auto plan=Json::parse(P::StorageRecoveryPacket(packet.dump()));check(plan.at("writes").size()==1&&plan.at("decision").at("action")==unsigned(P::Action::verify),"recovery packet writes");
  const Json marker={{"version",1},{"action",1},{"revision","1"}};
  packet["dispatch"]=marker.dump();plan=Json::parse(P::StorageRecoveryPacket(packet.dump()));
  check(plan.at("writes").size()==1&&plan.at("writes")[0].at("key").get<std::string>().starts_with("attempt/"),"observation wrote operation");
  packet["attempt"]=plan.at("writes")[0].at("value");
  plan=Json::parse(P::StorageRecoveryPacket(packet.dump()));check(plan.at("writes").empty()&&plan.at("decision").at("action")==unsigned(P::Action::wait),"repeat observation extended deadline");
  for(int field=0;field<11;++field){auto bad=packet;
    if(field<5){auto attempt=Json::parse(bad.at("attempt").get<std::string>());if(field==0)attempt["deadline"]=135;if(field==1)attempt["deadline"]="0135";if(field==2)attempt["action"]=3;if(field==3)attempt["revision"]="2";if(field==4)attempt["extra"]=true;bad["attempt"]=attempt.dump();}
    if(field==5)bad["scope"]=nullptr;
    if(field==6){auto m=marker;m["action"]=3;bad["dispatch"]=m.dump();}
    if(field==7)bad["record"]=nullptr;
    if(field==8){auto c=Json::parse(command);c["event"]["kind"]=0;bad["command"]=c.dump();}
    if(field==9){auto m=marker;m["action"]=1.0;bad["dispatch"]=m.dump();}
    if(field==10){auto a=Json::parse(bad.at("attempt").get<std::string>());a["action"]=1.0;bad["attempt"]=a.dump();}
    check(Json::parse(P::StorageRecoveryPacket(bad.dump())).contains("packetError"),"malformed recovery accepted");
  }
  packet["dispatch"]=nullptr;plan=Json::parse(P::StorageRecoveryPacket(packet.dump()));check(plan.at("decision").at("error")==unsigned(P::Error::invalidRecord)&&plan.at("writes").empty(),"orphan attempt accepted");
  const auto duplicate="{\"attempt\":null,"+packet.dump().substr(1);check(Json::parse(P::StorageRecoveryPacket(duplicate)).contains("packetError"),"duplicate recovery key");
  return count;
}
