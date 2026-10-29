#include "arken_mcp_economics_v1/protocol.hpp"
#include "arken_mcp_economics_v1/api.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "arken/digest/v1/digest.hpp"
#include <array>
#include <span>

namespace arken::mcp::economics::v1 {
namespace {
namespace J=arken::schema::cpp::v1::tool_json;
using Json=J::Json;
void fields(const Json& j,std::initializer_list<const char*> keys) {
 J::require(j.is_object()&&j.size()==keys.size(),"budget_fields");
 for(const auto* key:keys)J::require(j.contains(key),"budget_field");
}
std::uint64_t number(const Json& j){return J::readScalar<std::uint64_t>(j);}
bool hex(std::string_view s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==s.npos;}
bool serviceName(std::string_view s){return !s.empty()&&s.size()<=48&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==s.npos;}
std::string digest(std::string_view input) {
 const auto result=arken::digest::v1::sha256(std::as_bytes(std::span(input.data(),input.size())));
 J::require(bool(result),"budget_digest");std::array<char,71> text{};
 J::require(arken::digest::v1::format_sha256(*result,text),"budget_digest_text");return std::string(text.data()+7,64);
}
std::string dayText(std::uint64_t day){auto text=std::to_string(day);text.insert(0,8-text.size(),'0');return text;}
BudgetPolicy policy(const Json& value,const std::string& scope) {
 fields(value,{"maximumAttempts","maximumReservedNanoUsd","attemptReserveNanoUsd"});
 BudgetPolicy result{scope,number(value.at("maximumAttempts")),number(value.at("maximumReservedNanoUsd")),number(value.at("attemptReserveNanoUsd"))};
 J::require(result.maximumAttempts<=1000000&&bool(reserve(result,std::nullopt,1)),"budget_policy");return result;
}
std::optional<BudgetState> state(const Json& value) {
 if(value.is_null())return {};
 const auto s=J::parse(value.get<std::string>(),2048);
 fields(s,{"schema","scope","day","attempts","revision","updatedAt","reservedNanoUsd"});
 J::require(s.at("schema")=="arken.mcp.budget-state.v1","budget_state_schema");
 return BudgetState{s.at("scope"),number(s.at("day")),number(s.at("attempts")),number(s.at("revision")),number(s.at("updatedAt")),number(s.at("reservedNanoUsd"))};
}
std::string encoded(const BudgetState& s) {
 return Json{{"schema","arken.mcp.budget-state.v1"},{"scope",s.scope},{"day",std::to_string(s.day)},
  {"attempts",std::to_string(s.attempts)},{"revision",std::to_string(s.revision)},
  {"updatedAt",std::to_string(s.updatedAt)},{"reservedNanoUsd",std::to_string(s.reservedNanoUsd)}}.dump();
}
Json response(int status,std::string_view code,std::uint64_t retry=0) {
 return {{"status",status},{"body",Json{{"error",code}}.dump()},{"retryAfterSeconds",std::to_string(retry)}};
}
}
std::string budgetPacket(std::uint32_t mode,std::string_view input) noexcept {
 try {
  const auto p=J::parse(input,budgetPacketLimit);
  if(mode==0)fields(p,{"configuration","service","caller","now","enabled"});
  else {J::require(mode==1,"budget_mode");fields(p,{"configuration","service","caller","now","states","enabled"});}
  J::require(p.at("enabled").is_boolean()&&p.at("enabled").get<bool>(),"budget_disabled");
  const auto& config=p.at("configuration");fields(config,{"scope","callerSalt","global","caller","services"});
  const std::string scope=config.at("scope"),salt=config.at("callerSalt"),service=p.at("service"),caller=p.at("caller");
  const auto now=number(p.at("now"));
  J::require(hex(scope)&&hex(salt)&&serviceName(service)&&now>0&&now<=4102444800,"budget_context");
  // Cloudflare supplies this header at the public edge. Never accept a payload
  // address, X-Forwarded-For, a wallet claim, or a fresh random caller fallback.
  J::require(!caller.empty()&&caller.size()<=64&&caller.find_first_not_of("0123456789abcdefABCDEF:.")==caller.npos,"budget_caller");
  const auto& services=config.at("services");
  J::require(services.is_object()&&!services.empty()&&services.size()<=64&&services.contains(service),"budget_service");
  for(auto it=services.begin();it!=services.end();++it){J::require(serviceName(it.key()),"budget_service_name");policy(it.value(),"service/"+it.key());}
  const auto root="budget/"+scope+"/";
  const std::array keys{root+"global",root+"service/"+service,
   root+"caller/"+dayText(now/86400)+"/"+digest(salt+":"+scope+":"+service+":"+dayText(now/86400)+":"+caller)};
  const std::array policies{policy(config.at("global"),"global"),policy(services.at(service),"service/"+service),policy(config.at("caller"),"caller/"+service)};
  if(mode==0)return Json{{"actorKey","arken-mcp-attempt-budget-v1"},{"keys",keys},
   {"prune",{{"prefix",root+"caller/"},{"end",root+"caller/"+dayText(now/86400>2?now/86400-2:0)+"/"},{"limit",4}}}}.dump();
  const auto& states=p.at("states");J::require(states.is_array()&&states.size()==3,"budget_states");
  std::array<Reservation,3> reservations;
  for(std::size_t i=0;i<3;++i) {
   const auto reserved=reserve(policies[i],state(states.at(i)),now);
   if(!reserved)return Json{{"writes",Json::array()},{"response",response(reserved.error==Error::exhausted?429:503,errorCode(reserved.error),reserved.value.retryAfterSeconds)}}.dump();
   reservations[i]=reserved.value;
  }
  auto writes=Json::array();
  for(std::size_t i=0;i<3;++i)writes.push_back({{"key",keys[i]},{"value",encoded(reservations[i].state)}});
  return Json{{"writes",writes},{"response",{{"status",204},{"body",""},{"retryAfterSeconds","0"}}}}.dump();
 }catch(const std::exception&){return R"({"packetError":"attempt_budget_unavailable"})";}
}
}
