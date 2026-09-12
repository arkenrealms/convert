#include "arken_file_conversion_service_v1/payment.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace P=arken::file_conversion::service::payment;
namespace {
unsigned checks=0;
auto packets=nlohmann::json::array();
void Check(bool condition,std::string_view label){++checks;if(!condition)throw std::runtime_error(std::string(label));}
void Packet(const P::Operation& op,const P::Quote& quote,const std::optional<P::NonceClaim>& claim,const P::Event& event,const P::Decision& result){
  const auto input=P::InputPacket(op,quote,claim,event),expected=P::DecisionPacket(result);
  Check(P::AdvancePacket(input)==expected,"packet differs from typed policy");packets.push_back({{"input",input},{"expected",expected}});
}
P::Quote Quote(){P::Quote q;q.operation[0]=1;q.tenant[0]=2;q.input[0]=3;q.options[0]=4;q.release[0]=5;q.priceVersion[0]=6;
  q.scope.chain=84532;q.scope.payer[0]=7;q.scope.asset[0]=8;q.recipient[0]=9;q.amount=10000;q.expiresAt=200;return q;}
P::Authorization Auth(){P::Authorization a;a.payload[0]=10;a.validAfter=90;a.validBefore=150;return a;}
P::Output Output(){P::Output out;out.digest[0]=11;out.release=Quote().release;out.bytes=500;out.width=13;out.height=7;return out;}
P::Transfer Transfer(){P::Transfer t;t.transaction[0]=12;t.scope=Quote().scope;t.recipient=Quote().recipient;t.amount=10000;t.nonce=Auth().nonce;return t;}
struct Machine {
  P::Operation op{.quote=Quote()};std::optional<P::NonceClaim> claim;
  unsigned verifies=0,converts=0,settles=0;
  P::Event event(P::EventKind kind)const{P::Event e;e.kind=kind;e.expectedRevision=op.revision;e.now=100;return e;}
  P::Decision step(P::Event e){auto result=P::Advance(op,op.quote,claim,e);Check(result.error==P::Error::none,"valid transition rejected");Packet(op,op.quote,claim,e,result);
    if(result.action==P::Action::verify)++verifies;if(result.action==P::Action::convert)++converts;if(result.action==P::Action::settle)++settles;
    op=result.operation;claim=result.nonceClaim;return result;}
  void reject(P::Event e,P::Error expected){const auto result=P::Advance(op,op.quote,claim,e);Check(result.error==expected,"wrong rejection");Packet(op,op.quote,claim,e,result);
    Check(result.operation==op&&result.nonceClaim==claim&&!result.changed&&result.action==P::Action::none,"rejection changed state");}
  void start(){auto e=event(P::EventKind::request);e.authorization=Auth();Check(step(e).action==P::Action::verify,"verification action");}
  void verified(){start();auto e=event(P::EventKind::verificationPassed);e.verifiedPayer=op.quote.scope.payer;step(e);}
  void computing(){verified();auto e=event(P::EventKind::beginConversion);e.executingRelease=op.quote.release;Check(step(e).action==P::Action::convert,"conversion action");}
  void output(){computing();auto e=event(P::EventKind::outputCommitted);e.output=Output();step(e);}
  void settling(){output();Check(step(event(P::EventKind::beginSettlement)).action==P::Action::settle,"settle action");}
};
void Tests(){
  Machine m;m.settling();Check(m.verifies==1&&m.converts==1&&m.settles==1,"single effect sequence");
  for(int i=0;i<20;++i)Check(m.step(m.event(P::EventKind::request)).action==P::Action::wait,"pending request restarted effect");
  m.step(m.event(P::EventKind::settlementUncertain));Check(m.op.stage==P::Stage::settlementUnknown,"unknown settlement stage");
  for(int i=0;i<20;++i){Check(m.step(m.event(P::EventKind::request)).action==P::Action::reconcile,"unknown settlement replayed");m.reject(m.event(P::EventKind::beginSettlement),P::Error::invalidEvent);}
  auto e=m.event(P::EventKind::reconciledSucceeded);e.transfer=Transfer();e.now=1000;Check(m.step(e).action==P::Action::receipt,"late settlement reconciliation");
  for(int i=0;i<20;++i){e=m.event(P::EventKind::request);e.now=1000;Check(m.step(e).action==P::Action::receipt,"expired payment lost receipt");}
  Check(m.settles==1,"second settlement dispatched");
  for(int i=1;i<=int(P::EventKind::reconciledFailed);++i)m.reject(m.event(static_cast<P::EventKind>(i)),P::Error::invalidEvent);
  e=m.event(P::EventKind::request);e.expectedRevision=0;m.reject(e,P::Error::staleRevision);
  auto changed=m.op.quote;changed.amount++;const auto conflict=P::Advance(m.op,changed,m.claim,m.event(P::EventKind::request));Check(conflict.error==P::Error::quoteConflict&&conflict.operation==m.op,"changed quote accepted");
  for(unsigned field=0;field<6;++field){changed=m.op.quote;
    if(field==0)changed.input[0]++;if(field==1)changed.options[0]++;if(field==2)changed.release[0]++;
    if(field==3)changed.tenant[0]++;if(field==4)changed.priceVersion[0]++;if(field==5)changed.recipient[0]++;
    const auto decision=P::Advance(m.op,changed,m.claim,m.event(P::EventKind::request));Check(decision.error==P::Error::quoteConflict,"quote identity not frozen");Packet(m.op,changed,m.claim,m.event(P::EventKind::request),decision);}
  e=m.event(P::EventKind::request);e.authorization=Auth();e.authorization->payload[0]++;m.reject(e,P::Error::authorizationConflict);
  Machine second;second.op.quote.operation[0]=42;second.start();second.claim=m.claim;e=second.event(P::EventKind::verificationPassed);e.verifiedPayer=second.op.quote.scope.payer;second.reject(e,P::Error::nonceConflict);
  second.step(second.event(P::EventKind::verificationRejected));Check(second.claim==m.claim&&second.settles==0,"bad verification claimed nonce");
  Machine invalid;invalid.start();invalid.step(invalid.event(P::EventKind::verificationRejected));Check(!invalid.claim&&invalid.op.stage==P::Stage::failedNoCharge,"invalid proof acquired claim");
  Machine early;early.start();e=early.event(P::EventKind::verificationPassed);e.verifiedPayer=early.op.quote.scope.payer;e.now=150;early.reject(e,P::Error::expired);
  e.now=100;e.verifiedPayer[0]++;early.reject(e,P::Error::payerMismatch);
  e=early.event(P::EventKind::request);e.authorization=Auth();e.authorization->nonce[0]=1;early.reject(e,P::Error::authorizationConflict);
  e=early.event(P::EventKind::retryVerification);const auto old=e.expectedRevision;early.step(e);e=early.event(P::EventKind::verificationPassed);e.expectedRevision=old;e.verifiedPayer=early.op.quote.scope.payer;early.reject(e,P::Error::staleRevision);
  for(auto now:{90u,150u,200u}){Machine expired;e=expired.event(P::EventKind::request);e.authorization=Auth();e.now=now;expired.reject(e,P::Error::expired);Check(!expired.claim,"expired proof acquired claim");}
  Machine released;released.verified();e=released.event(P::EventKind::beginConversion);released.reject(e,P::Error::releaseMismatch);
  e.executingRelease=released.op.quote.release;e.now=151;released.reject(e,P::Error::expired);
  released.claim.reset();e.now=100;released.reject(e,P::Error::nonceClaimMissing);
  for(int field=0;field<4;++field){Machine bad;bad.computing();e=bad.event(P::EventKind::outputCommitted);e.output=Output();
    if(field==0)e.output->bytes=0;if(field==1)e.output->bytes=8388609;if(field==2)e.output->width=4097;if(field==3)e.output->release[0]++;bad.reject(e,P::Error::outputInvalid);}
  for(int field=0;field<7;++field){Machine bad;bad.settling();e=bad.event(P::EventKind::settlementSucceeded);e.transfer=Transfer();
    if(field==0)e.transfer->transaction={};if(field==1)e.transfer->amount++;if(field==2)e.transfer->scope.chain++;
    if(field==3)e.transfer->scope.asset[0]++;if(field==4)e.transfer->recipient[0]++;if(field==5)e.transfer->nonce[0]++;if(field==6)e.transfer->scope.payer[0]++;bad.reject(e,P::Error::transferMismatch);}
  Machine expiredSettlement;expiredSettlement.output();e=expiredSettlement.event(P::EventKind::beginSettlement);e.now=151;expiredSettlement.reject(e,P::Error::expired);Check(expiredSettlement.settles==0,"expired authorization settled");
  for(int stage=0;stage<5;++stage){Machine failed;if(stage==1)failed.start();if(stage==2)failed.verified();if(stage==3)failed.computing();if(stage==4)failed.output();
    e=failed.event(P::EventKind::failBeforeSettlement);e.failure=P::Failure::storageFailed;failed.step(e);Check(failed.settles==0&&failed.op.stage==P::Stage::failedNoCharge,"pre-settlement failure charged");
    failed.reject(failed.event(P::EventKind::beginSettlement),P::Error::invalidEvent);}
  Machine rejected;rejected.settling();rejected.step(rejected.event(P::EventKind::settlementRejected));rejected.reject(rejected.event(P::EventKind::beginSettlement),P::Error::invalidEvent);
  Check(rejected.step(rejected.event(P::EventKind::request)).action==P::Action::failure,"settlement failure became new purchase");
  Machine resumed;resumed.verified();e=resumed.event(P::EventKind::request);e.executingRelease=resumed.op.quote.release;
  Check(resumed.step(e).action==P::Action::convert,"authorized request did not resume conversion");
  Check(resumed.step(resumed.event(P::EventKind::request)).action==P::Action::wait&&resumed.converts==1,"in-flight conversion replayed");
  e=resumed.event(P::EventKind::outputCommitted);e.output=Output();resumed.step(e);
  Check(resumed.step(resumed.event(P::EventKind::request)).action==P::Action::settle,"committed output did not resume settlement");
  Check(resumed.step(resumed.event(P::EventKind::request)).action==P::Action::wait&&resumed.settles==1,"in-flight settlement replayed");
  Machine success;success.settling();e=success.event(P::EventKind::settlementSucceeded);e.transfer=Transfer();success.step(e);success.reject(e,P::Error::staleRevision);
  Machine badRecord;badRecord.op.stage=P::Stage::settled;badRecord.reject(badRecord.event(P::EventKind::request),P::Error::invalidRecord);
  badRecord.op.stage=P::Stage::failedNoCharge;badRecord.op.failure=P::Failure::settlementRejected;badRecord.reject(badRecord.event(P::EventKind::request),P::Error::invalidRecord);
  for(unsigned field=0;field<3;++field){Machine empty;if(field==0)empty.op.quote.scope.asset={};if(field==1)empty.op.quote.scope.payer={};if(field==2)empty.op.quote.recipient={};empty.reject(empty.event(P::EventKind::request),P::Error::invalidRecord);}
  badRecord.op={.quote=Quote()};badRecord.op.revision=std::numeric_limits<std::uint64_t>::max();e=badRecord.event(P::EventKind::request);e.authorization=Auth();badRecord.reject(e,P::Error::revisionOverflow);
  Machine exact;exact.op.quote.amount=9007199254740993ULL;exact.start();
  auto packet=nlohmann::json::parse(P::InputPacket(exact.op,exact.op.quote,exact.claim,exact.event(P::EventKind::request)));
  Check(packet.at("quote").at("amount")=="9007199254740993","amount narrowed");
  for(const auto& bad:{nlohmann::json(10000),nlohmann::json("010000"),nlohmann::json("18446744073709551616")}){
    auto changedPacket=packet;changedPacket["quote"]["amount"]=bad;const auto input=changedPacket.dump();const auto out=P::AdvancePacket(input);
    Check(nlohmann::json::parse(out).contains("packetError"),"noncanonical amount admitted");packets.push_back({{"input",input},{"expected",out}});}
  const auto duplicate="{\"version\":1,"+packet.dump().substr(1);
  for(const auto& input:{std::string("{"),duplicate,std::string(16385,' ')}){
    const auto out=P::AdvancePacket(input);Check(nlohmann::json::parse(out).contains("packetError"),"invalid packet admitted");
    if(input.size()<=16384)packets.push_back({{"input",input},{"expected",out}});}
  for(unsigned shape=0;shape<4;++shape){auto invalidPacket=packet;
    if(shape==0)invalidPacket["version"]=1.0;if(shape==1)invalidPacket["event"]["kind"]=255;
    if(shape==2)invalidPacket["extra"]=true;if(shape==3)invalidPacket.erase("event");
    const auto input=invalidPacket.dump(),out=P::AdvancePacket(input);Check(nlohmann::json::parse(out).contains("packetError"),"invalid packet shape admitted");
    packets.push_back({{"input",input},{"expected",out}});}
}
}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-payment-tests r4332\n";return 0;}
  try{Check(argc==1||(argc==3&&std::string_view(argv[1])=="--write-packets"),"invalid arguments");Tests();
    if(argc==3){std::ofstream out(argv[2],std::ios::binary);out<<packets.dump();Check(bool(out),"packet fixture write failed");}
    std::cout<<checks<<" payment policy checks passed; "<<packets.size()<<" native/Wasm packets\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
