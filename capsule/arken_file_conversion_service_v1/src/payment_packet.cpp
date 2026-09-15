#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "payment_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "arken/digest/v1/digest.hpp"
#include <set>
#include <vector>
namespace arken::file_conversion::service::v1::payment {
namespace J=arken::schema::cpp::v1::tool_json;
using Json=J::Json;
namespace detail {
void Fields(const Json& j,std::initializer_list<std::string_view> fields){J::require(j.is_object()&&j.size()==fields.size(),"packet fields mismatch");for(auto field:fields)J::require(j.contains(field),"packet field missing");}
template<std::size_t N> Json Put(const std::array<std::uint8_t,N>& bytes){arken::schema::cpp::v1::fixed_id<N,1> id;for(std::size_t i=0;i<N;++i)id.value[i]=std::byte(bytes[i]);return J::write(id);}
template<std::size_t N> std::array<std::uint8_t,N> Bytes(const Json& j){const auto id=J::read<arken::schema::cpp::v1::fixed_id<N,1>>(j);std::array<std::uint8_t,N> bytes{};for(std::size_t i=0;i<N;++i)bytes[i]=std::to_integer<std::uint8_t>(id.value[i]);return bytes;}
template<class E> E Enum(const Json& j,E last){const auto n=J::readScalar<std::uint8_t>(j);J::require(n<=static_cast<std::uint8_t>(last),"packet enum invalid");return static_cast<E>(n);}
template<class T> T Get(const Json&);
Json Put(const Scope& s){return {{"chain",s.chain},{"asset",Put(s.asset)},{"payer",Put(s.payer)}};}
template<> Scope Get<Scope>(const Json& j){Fields(j,{"chain","asset","payer"});return {J::readScalar<std::uint32_t>(j.at("chain")),Bytes<20>(j.at("asset")),Bytes<20>(j.at("payer"))};}
Json Put(const Quote& q){return {{"operation",Put(q.operation)},{"tenant",Put(q.tenant)},{"input",Put(q.input)},{"options",Put(q.options)},{"release",Put(q.release)},
  {"priceVersion",Put(q.priceVersion)},{"scope",Put(q.scope)},{"recipient",Put(q.recipient)},{"amount",J::writeScalar(q.amount)},{"expiresAt",J::writeScalar(q.expiresAt)}};}
template<> Quote Get<Quote>(const Json& j){Fields(j,{"operation","tenant","input","options","release","priceVersion","scope","recipient","amount","expiresAt"});
  return {Bytes<32>(j.at("operation")),Bytes<32>(j.at("tenant")),Bytes<32>(j.at("input")),Bytes<32>(j.at("options")),Bytes<32>(j.at("release")),Bytes<32>(j.at("priceVersion")),
    Get<Scope>(j.at("scope")),Bytes<20>(j.at("recipient")),J::readScalar<std::uint64_t>(j.at("amount")),J::readScalar<std::uint64_t>(j.at("expiresAt"))};}
Json Put(const Authorization& a){return {{"payload",Put(a.payload)},{"nonce",Put(a.nonce)},{"validAfter",J::writeScalar(a.validAfter)},{"validBefore",J::writeScalar(a.validBefore)}};}
template<> Authorization Get<Authorization>(const Json& j){Fields(j,{"payload","nonce","validAfter","validBefore"});return {Bytes<32>(j.at("payload")),Bytes<32>(j.at("nonce")),J::readScalar<std::uint64_t>(j.at("validAfter")),J::readScalar<std::uint64_t>(j.at("validBefore"))};}
Json Put(const Output& o){return {{"digest",Put(o.digest)},{"release",Put(o.release)},{"bytes",o.bytes},{"width",o.width},{"height",o.height}};}
template<> Output Get<Output>(const Json& j){Fields(j,{"digest","release","bytes","width","height"});return {Bytes<32>(j.at("digest")),Bytes<32>(j.at("release")),J::readScalar<std::uint32_t>(j.at("bytes")),J::readScalar<std::uint32_t>(j.at("width")),J::readScalar<std::uint32_t>(j.at("height"))};}
Json Put(const Transfer& t){return {{"transaction",Put(t.transaction)},{"nonce",Put(t.nonce)},{"scope",Put(t.scope)},{"recipient",Put(t.recipient)},{"amount",J::writeScalar(t.amount)}};}
template<> Transfer Get<Transfer>(const Json& j){Fields(j,{"transaction","nonce","scope","recipient","amount"});return {Bytes<32>(j.at("transaction")),Bytes<32>(j.at("nonce")),Get<Scope>(j.at("scope")),Bytes<20>(j.at("recipient")),J::readScalar<std::uint64_t>(j.at("amount"))};}
Json Put(const NonceClaim& c){return {{"scope",Put(c.scope)},{"nonce",Put(c.nonce)},{"operation",Put(c.operation)},{"payload",Put(c.payload)}};}
template<> NonceClaim Get<NonceClaim>(const Json& j){Fields(j,{"scope","nonce","operation","payload"});return {Get<Scope>(j.at("scope")),Bytes<32>(j.at("nonce")),Bytes<32>(j.at("operation")),Bytes<32>(j.at("payload"))};}
template<class T> Json Put(const std::optional<T>& value){return value?Put(*value):Json(nullptr);}
template<class T> std::optional<T> Optional(const Json& j){return j.is_null()?std::nullopt:std::optional<T>(Get<T>(j));}
Json Put(const Operation& op){return {{"quote",Put(op.quote)},{"revision",J::writeScalar(op.revision)},{"stage",unsigned(op.stage)},{"authorization",Put(op.authorization)},{"output",Put(op.output)},{"transfer",Put(op.transfer)},{"failure",unsigned(op.failure)}};}
template<> Operation Get<Operation>(const Json& j){Fields(j,{"quote","revision","stage","authorization","output","transfer","failure"});
  return {Get<Quote>(j.at("quote")),J::readScalar<std::uint64_t>(j.at("revision")),Enum(j.at("stage"),Stage::settlementFailed),Optional<Authorization>(j.at("authorization")),Optional<Output>(j.at("output")),Optional<Transfer>(j.at("transfer")),Enum(j.at("failure"),Failure::settlementRejected)};}
Json Put(const Event& e){return {{"kind",unsigned(e.kind)},{"expectedRevision",J::writeScalar(e.expectedRevision)},{"now",J::writeScalar(e.now)},
  {"authorization",Put(e.authorization)},{"output",Put(e.output)},{"transfer",Put(e.transfer)},{"verifiedPayer",Put(e.verifiedPayer)},{"executingRelease",Put(e.executingRelease)},{"failure",unsigned(e.failure)}};}
template<> Event Get<Event>(const Json& j){Fields(j,{"kind","expectedRevision","now","authorization","output","transfer","verifiedPayer","executingRelease","failure"});
  return {Enum(j.at("kind"),EventKind::retryConversion),J::readScalar<std::uint64_t>(j.at("expectedRevision")),J::readScalar<std::uint64_t>(j.at("now")),Optional<Authorization>(j.at("authorization")),Optional<Output>(j.at("output")),Optional<Transfer>(j.at("transfer")),Bytes<20>(j.at("verifiedPayer")),Bytes<32>(j.at("executingRelease")),Enum(j.at("failure"),Failure::settlementRejected)};}
Json Parse(std::string_view text,std::size_t maximumBytes){
  J::require(maximumBytes<=1048576&&text.size()<=maximumBytes,"packet byte limit");std::vector<std::set<std::string>> keys;
  return Json::parse(text.begin(),text.end(),[&](int depth,Json::parse_event_t event,Json& value){
    J::require(depth<=32,"packet depth limit");
    if(event==Json::parse_event_t::object_start)keys.emplace_back();
    if(event==Json::parse_event_t::key)J::require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"duplicate packet key");
    if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
}
void Version(const Json& j){J::require(J::readScalar<std::uint32_t>(j.at("version"))==1,"packet version mismatch");}
struct StoredInput {Quote quote;Event event;Operation operation;std::optional<std::string> payment;};
StoredInput Stored(const Json& outer){
  const auto command=Parse(J::readScalar<std::string>(outer.at("command")));
  if(command.contains("payment"))Fields(command,{"version","quote","event","payment"});else Fields(command,{"version","quote","event"});Version(command);
  StoredInput input{Get<Quote>(command.at("quote")),Get<Event>(command.at("event")),{},std::nullopt};
  input.operation.quote=input.quote;
  if(!outer.at("record").is_null()){
    const auto record=Parse(J::readScalar<std::string>(outer.at("record")));Fields(record,{"version","operation"});Version(record);
    input.operation=Get<Operation>(record.at("operation"));J::require(input.operation.quote==input.quote,"stored quote changed");
  }
  if(command.contains("payment")){
    input.payment=J::readScalar<std::string>(command.at("payment"));
    const auto auth=input.operation.authorization?input.operation.authorization:input.event.authorization;
    J::require(auth&&PayloadDigest(*input.payment)==auth->payload,"authorization body digest changed");
  }
  return input;
}
Json Keys(const StoredInput& input){
  const auto& q=input.quote;const auto auth=input.operation.authorization?input.operation.authorization:input.event.authorization;
  const auto actor=std::to_string(q.scope.chain)+"/"+Put(q.scope.asset).get<std::string>()+"/"+Put(q.scope.payer).get<std::string>();
  return {{"scopeKey","scope"},{"actorKey",actor},{"operationKey","operation/"+Put(q.operation).get<std::string>()},
    {"dispatchKey","dispatch/"+Put(q.operation).get<std::string>()+"/"+std::to_string(input.operation.revision)},
    {"attemptKey","attempt/"+Put(q.operation).get<std::string>()+"/"+std::to_string(input.operation.revision)},
    {"nonceKey",auth?Json("nonce/"+Put(auth->nonce).get<std::string>()):Json(nullptr)},
    {"authorizationKey","authorization/"+Put(q.operation).get<std::string>()}};
}
Quote ReadQuote(const Json& j){return Get<Quote>(j);}
Operation ReadOperation(const Json& j){return Get<Operation>(j);}
Output ReadOutput(const Json& j){return Get<Output>(j);}
Json WriteQuote(const Quote& q){return Put(q);}
Json WriteEvent(const Event& e){return Put(e);}
Digest PayloadDigest(std::string_view text){auto hash=arken::digest::v1::sha256(std::as_bytes(std::span(text.data(),text.size())));J::require(hash.has_value(),"payload digest failed");return hash->bytes;}
}
using namespace detail;
std::string InputPacket(const Operation& op,const Quote& q,const std::optional<NonceClaim>& claim,const Event& event){return Json{{"version",1},{"operation",Put(op)},{"quote",Put(q)},{"claim",Put(claim)},{"event",Put(event)}}.dump();}
std::string DecisionPacket(const Decision& d){return Json{{"version",1},{"error",unsigned(d.error)},{"action",unsigned(d.action)},{"changed",d.changed},{"operation",Put(d.operation)},{"claim",Put(d.nonceClaim)}}.dump();}
std::optional<Operation> ReadOperationRecord(std::string_view text) noexcept {try{auto value=Get<Operation>(Parse(text));J::require(ValidOperationRecord(value),"invalid payment record");return value;}catch(...){return std::nullopt;}}
std::string WriteOperationRecord(const Operation& value) noexcept {try{J::require(ValidOperationRecord(value),"invalid payment record");return Put(value).dump();}catch(...){return {};}}
std::optional<Event> ReadEventRecord(std::string_view text) noexcept {try{return Get<Event>(Parse(text));}catch(...){return std::nullopt;}}
std::string AdvancePacket(std::string_view text) noexcept {
  try {
    const auto j=Parse(text);
    Fields(j,{"version","operation","quote","claim","event"});Version(j);
    return DecisionPacket(Advance(Get<Operation>(j.at("operation")),Get<Quote>(j.at("quote")),Optional<NonceClaim>(j.at("claim")),Get<Event>(j.at("event"))));
  } catch(const std::exception&){return "{\"packetError\":\"invalid_payment_packet\"}";}
  catch(...){return "{\"packetError\":\"payment_packet_unavailable\"}";}
}
std::string StorageLookupPacket(std::string_view text) noexcept {
  try{const auto outer=Parse(text);Fields(outer,{"command","record"});return Keys(Stored(outer)).dump();}
  catch(const std::exception&){return "{\"packetError\":\"invalid_payment_storage_lookup\"}";}
  catch(...){return "{\"packetError\":\"payment_storage_unavailable\"}";}
}
std::string StorageAdvancePacket(std::string_view text) noexcept {
  try {
    const auto outer=Parse(text);Fields(outer,{"command","record","scope","claim"});auto input=Stored(outer);
    // A read observes the current durable revision. Effect callbacks still
    // require the exact revision frozen by the dispatching controller.
    if(input.event.kind==EventKind::inspect)input.event.expectedRevision=input.operation.revision;
    if(!outer.at("scope").is_null())J::require(Get<Scope>(Parse(J::readScalar<std::string>(outer.at("scope"))))==input.quote.scope,"actor scope changed");
    // A record without its atomically committed scope denotes corrupt/partial storage.
    J::require((outer.at("record").is_null()&&outer.at("claim").is_null())||!outer.at("scope").is_null(),"actor scope missing");
    std::optional<NonceClaim> claim;
    if(!outer.at("claim").is_null()){
      const auto stored=Parse(J::readScalar<std::string>(outer.at("claim")));Fields(stored,{"version","claim"});Version(stored);claim=Get<NonceClaim>(stored.at("claim"));}
    const auto decision=Advance(input.operation,input.quote,claim,input.event);auto writes=Json::array();
    const auto keys=Keys(input);
    if(decision.changed){
      writes.push_back({{"key",keys.at("operationKey")},{"value",Json{{"version",1},{"operation",Put(decision.operation)}}.dump()}});
      if(decision.nonceClaim&&decision.nonceClaim!=claim){J::require(!keys.at("nonceKey").is_null(),"nonce key absent");
        writes.push_back({{"key",keys.at("nonceKey")},{"value",Json{{"version",1},{"claim",Put(*decision.nonceClaim)}}.dump()}});}
      if(outer.at("scope").is_null())writes.push_back({{"key","scope"},{"value",Put(input.quote.scope).dump()}});
      if(input.payment&&!input.operation.authorization)writes.push_back({{"key",keys.at("authorizationKey")},{"value",*input.payment}});
    }
    return Json{{"decision",Json::parse(DecisionPacket(decision))},{"writes",writes}}.dump();
  } catch(const std::exception&){return "{\"packetError\":\"invalid_payment_storage_packet\"}";}
  catch(...){return "{\"packetError\":\"payment_storage_unavailable\"}";}
}
std::string StorageDispatchPacket(std::string_view text) noexcept {
  try {
    const auto outer=Parse(text);Fields(outer,{"command","record","scope","claim","decision","dispatch"});
    const auto input=Stored(outer);const auto decision=Parse(J::readScalar<std::string>(outer.at("decision")));
    Fields(decision,{"version","error","action","changed","operation","claim"});Version(decision);
    const auto requested=Get<Operation>(decision.at("operation"));
    J::require(!outer.at("record").is_null()&&!outer.at("scope").is_null(),"dispatch record missing");
    J::require(Get<Scope>(Parse(J::readScalar<std::string>(outer.at("scope"))))==input.quote.scope,"dispatch scope mismatch");
    std::optional<NonceClaim> claim;
    if(!outer.at("claim").is_null()){const auto value=Parse(J::readScalar<std::string>(outer.at("claim")));Fields(value,{"version","claim"});Version(value);claim=Get<NonceClaim>(value.at("claim"));}
    Event inspect;inspect.kind=EventKind::inspect;inspect.expectedRevision=input.operation.revision;
    J::require(Advance(input.operation,input.quote,claim,inspect).error==Error::none,"invalid dispatch state");
    const auto action=Enum(decision.at("action"),Action::failure);
    const bool stage=(action==Action::verify&&requested.stage==Stage::verifying)||(action==Action::convert&&requested.stage==Stage::computing)||(action==Action::settle&&requested.stage==Stage::settling);
    J::require(stage&&J::readScalar<std::uint8_t>(decision.at("error"))==0&&J::readScalar<bool>(decision.at("changed")),"effect not admitted");
    const auto key="dispatch/"+Put(requested.quote.operation).get<std::string>()+"/"+std::to_string(requested.revision);
    const auto value=Json{{"version",1},{"action",unsigned(action)},{"revision",J::writeScalar(requested.revision)}}.dump();
    if(requested!=input.operation)return Json{{"key",key},{"claimed",false}}.dump();
    if(!outer.at("dispatch").is_null()){J::require(J::readScalar<std::string>(outer.at("dispatch"))==value,"dispatch marker mismatch");return Json{{"key",key},{"claimed",false}}.dump();}
    Json result={{"key",key},{"value",value},{"claimed",true}};
    if(action==Action::verify||action==Action::convert){
      result["attemptKey"]=Keys(input).at("attemptKey");
      result["attemptValue"]=Json{{"version",1},{"action",unsigned(action)},{"revision",J::writeScalar(requested.revision)},
        {"deadline",J::writeScalar(AttemptDeadline(requested,input.event.now))}}.dump();
    }
    return result.dump();
  }catch(const std::exception&){return "{\"packetError\":\"invalid_payment_dispatch\"}";}
  catch(...){return "{\"packetError\":\"payment_dispatch_unavailable\"}";}
}
std::string StorageRecoveryPacket(std::string_view text) noexcept {
  try{
    const auto outer=Parse(text);Fields(outer,{"command","record","scope","claim","dispatch","attempt"});const auto input=Stored(outer);
    J::require(input.event.kind==EventKind::inspect&&!outer.at("record").is_null()&&!outer.at("scope").is_null(),"recovery record missing");
    J::require(Get<Scope>(Parse(J::readScalar<std::string>(outer.at("scope"))))==input.quote.scope,"recovery scope changed");
    std::optional<NonceClaim> claim;
    if(!outer.at("claim").is_null()){const auto value=Parse(J::readScalar<std::string>(outer.at("claim")));Fields(value,{"version","claim"});Version(value);claim=Get<NonceClaim>(value.at("claim"));}
    Attempt attempt;attempt.dispatched=!outer.at("dispatch").is_null();
    const auto stage=input.operation.stage;
    const auto action=stage==Stage::verifying?Action::verify:stage==Stage::computing?Action::convert:Action::settle;
    if(attempt.dispatched){const auto marker=Parse(J::readScalar<std::string>(outer.at("dispatch")));Fields(marker,{"version","action","revision"});Version(marker);
      J::require((stage==Stage::verifying||stage==Stage::computing||stage==Stage::settling)&&Enum(marker.at("action"),Action::failure)==action
        &&J::readScalar<std::uint64_t>(marker.at("revision"))==input.operation.revision,"recovery marker changed");}
    if(!outer.at("attempt").is_null()){
      const auto value=Parse(J::readScalar<std::string>(outer.at("attempt")));Fields(value,{"version","action","revision","deadline"});Version(value);
      J::require((stage==Stage::verifying||stage==Stage::computing)&&Enum(value.at("action"),Action::failure)==action
        &&J::readScalar<std::uint64_t>(value.at("revision"))==input.operation.revision,"recovery attempt changed");
      attempt.deadline=J::readScalar<std::uint64_t>(value.at("deadline"));
    }
    const auto recovered=Recover(input.operation,claim,input.event,attempt);const auto keys=Keys(input);auto writes=Json::array();
    if(recovered.decision.changed)writes.push_back({{"key",keys.at("operationKey")},{"value",Json{{"version",1},{"operation",Put(recovered.decision.operation)}}.dump()}});
    if(recovered.observationDeadline)writes.push_back({{"key",keys.at("attemptKey")},{"value",Json{{"version",1},{"action",unsigned(action)},
      {"revision",J::writeScalar(input.operation.revision)},{"deadline",J::writeScalar(*recovered.observationDeadline)}}.dump()}});
    return Json{{"decision",Json::parse(DecisionPacket(recovered.decision))},{"writes",writes}}.dump();
  }catch(const std::exception&){return "{\"packetError\":\"invalid_payment_recovery\"}";}
  catch(...){return "{\"packetError\":\"payment_recovery_unavailable\"}";}
}
}
