#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <set>
#include <vector>
namespace arken::file_conversion::service::payment {
namespace J=arken::schema::cpp::v1::tool_json;
using Json=J::Json;
namespace {
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
  return {Enum(j.at("kind"),EventKind::reconciledFailed),J::readScalar<std::uint64_t>(j.at("expectedRevision")),J::readScalar<std::uint64_t>(j.at("now")),Optional<Authorization>(j.at("authorization")),Optional<Output>(j.at("output")),Optional<Transfer>(j.at("transfer")),Bytes<20>(j.at("verifiedPayer")),Bytes<32>(j.at("executingRelease")),Enum(j.at("failure"),Failure::settlementRejected)};}
}
std::string InputPacket(const Operation& op,const Quote& q,const std::optional<NonceClaim>& claim,const Event& event){return Json{{"version",1},{"operation",Put(op)},{"quote",Put(q)},{"claim",Put(claim)},{"event",Put(event)}}.dump();}
std::string DecisionPacket(const Decision& d){return Json{{"version",1},{"error",unsigned(d.error)},{"action",unsigned(d.action)},{"changed",d.changed},{"operation",Put(d.operation)},{"claim",Put(d.nonceClaim)}}.dump();}
std::string AdvancePacket(std::string_view text) noexcept {
  try {
    J::require(text.size()<=16384,"packet byte limit");std::vector<std::set<std::string>> keys;
    const auto j=Json::parse(text.begin(),text.end(),[&](int depth,Json::parse_event_t event,Json& value){
      J::require(depth<=32,"packet depth limit");
      if(event==Json::parse_event_t::object_start)keys.emplace_back();
      if(event==Json::parse_event_t::key)J::require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"duplicate packet key");
      if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
    Fields(j,{"version","operation","quote","claim","event"});J::require(J::readScalar<std::uint32_t>(j.at("version"))==1,"packet version mismatch");
    return DecisionPacket(Advance(Get<Operation>(j.at("operation")),Get<Quote>(j.at("quote")),Optional<NonceClaim>(j.at("claim")),Get<Event>(j.at("event"))));
  } catch(const std::exception&){return "{\"packetError\":\"invalid_payment_packet\"}";}
  catch(...){return "{\"packetError\":\"payment_packet_unavailable\"}";}
}
}
