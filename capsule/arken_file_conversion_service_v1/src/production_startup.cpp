#include "arken_file_conversion_service_v1/production_startup.hpp"
#include "arken_file_conversion_service_v1/provider_readiness.hpp"
#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include "arken_file_conversion_service_v1/reconciliation_configuration.hpp"
#include "rpc_evidence.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
namespace arken::file_conversion::service::v1 {
namespace {
namespace P=payment;namespace J=arken::schema::cpp::v1::tool_json;using P::detail::Json;
Json Unavailable(std::string_view code){return {{"kind","unavailable"},{"ready",false},{"code",code}};}
std::string Fingerprint(const Json& value){const auto digest=P::detail::PayloadDigest(value.dump());arken::schema::cpp::v1::fixed_id<32,1> id;
  for(unsigned i=0;i<32;++i)id.value[i]=std::byte(digest[i]);return J::write(id).get<std::string>();}
Json Startup(const Json& j,bool qualification=false){
  P::detail::Fields(j,{"configuration","cached","anonymous","supported","rpc","now"});const auto& c=j.at("configuration");
  P::detail::Fields(c,{"enabled","keyId","keySecret","rpcClientKey","releaseId","downloadSecret","hasFiles","hasLedger","bindingGeneration"});
  if(qualification){if(c.at("enabled")!="false")return Unavailable("production_qualification_requires_disabled");}
  else{
    if(c.at("enabled").is_null()||c.at("enabled")=="false")return Unavailable("production_admission_disabled");
    if(c.at("enabled")!="true")return Unavailable("production_enable_setting_invalid");
  }
  for(const auto name:{"keyId","keySecret","rpcClientKey","releaseId","downloadSecret"})if(!c.at(name).is_string()||c.at(name).get_ref<const std::string&>().empty())return Unavailable("production_configuration_missing");
  if(!J::readScalar<bool>(c.at("hasFiles"))||!J::readScalar<bool>(c.at("hasLedger")))return Unavailable("production_bindings_missing");
  J::require(J::readScalar<std::uint64_t>(c.at("bindingGeneration"))>0,"binding generation missing");
  const auto now=J::readScalar<std::uint64_t>(j.at("now"));J::require(now>0&&now<=9007199254740000ULL,"startup clock invalid");
  const auto release=J::read<arken::schema::cpp::v1::fixed_id<32,1>>(c.at("releaseId"));
  bool nonzero=false;for(const auto b:release.value)nonzero=nonzero||b!=std::byte{};J::require(nonzero,"release missing");
  const auto& download=c.at("downloadSecret").get_ref<const std::string&>();J::require(download.size()>=32&&download.size()<=256,"download secret bounds");
  const auto profile=P::PaymentProfileById("base-mainnet-v1");
  const auto endpoint=P::ResolveReconciliationEndpoint(*profile,c.at("rpcClientKey").get_ref<const std::string&>());if(!endpoint.error.empty())return Unavailable(endpoint.error);
  // Representation validation only; this nonce is never used to sign a request.
  if(!P::PrepareCdpSigning(c.at("keyId").get_ref<const std::string&>(),c.at("keySecret").get_ref<const std::string&>(),P::CdpOperation::supported,now,std::string(32,'0')))return Unavailable("production_cdp_key_invalid");
  const auto identity=Fingerprint(c);
  const auto ready=[&](const Json& record,bool reused){return Json{{"kind","ready"},{"ready",true},{"code","ready"},{"record",record},{"reused",reused}};};
  if(!j.at("cached").is_null()){
    const auto& record=j.at("cached");P::detail::Fields(record,{"fingerprint","checkedAt","expiresAt"});
    const auto checked=J::readScalar<std::uint64_t>(record.at("checkedAt")),expires=J::readScalar<std::uint64_t>(record.at("expiresAt"));
    J::require(checked<=9007199254740000ULL&&expires==checked+300,"cache lifetime invalid");
    if(record.at("fingerprint")==identity&&checked<=now&&now<expires)return ready(record,true);
  }
  const Json providerInput={{"configuration",{{"keyId",c.at("keyId")},{"keySecret",c.at("keySecret")},{"rpcClientKey",c.at("rpcClientKey")}}},
    {"anonymous",j.at("anonymous")},{"supported",j.at("supported")},{"rpc",j.at("rpc")},{"now",j.at("now")}};
  auto decision=Json::parse(ProviderReadinessPacket(providerInput.dump()));
  if(decision.at("kind")=="probe"){decision["fingerprint"]=identity;return decision;}
  if(decision.at("kind")!="ready")return decision;
  return ready(Json{{"fingerprint",identity},{"checkedAt",std::to_string(now)},{"expiresAt",std::to_string(now+300)}},false);
}
}
std::string ProductionStartupPacket(std::string_view text) noexcept {try{return Startup(payment::detail::Parse(text,1048576)).dump();}
  catch(...){return Unavailable("production_configuration_or_probe_invalid").dump();}}
std::string ProductionQualificationPacket(std::string_view text) noexcept {
  try{const auto j=payment::detail::Parse(text,1048576);const auto kind=J::readScalar<std::string>(j.at("kind"));
    if(kind=="startup"){P::detail::Fields(j,{"kind","input"});return Startup(j.at("input"),true).dump();}
    P::detail::Fields(j,{"kind","administrator","method","hasBody","hasQuery"});J::require(kind=="request","qualification kind invalid");
    if(!J::readScalar<bool>(j.at("administrator")))return Json{{"allowed",false},{"status",401},{"code","authorization_required"}}.dump();
    if(J::readScalar<std::string>(j.at("method"))!="POST")return Json{{"allowed",false},{"status",405},{"code","method_not_allowed"}}.dump();
    if(J::readScalar<bool>(j.at("hasBody"))||J::readScalar<bool>(j.at("hasQuery")))return Json{{"allowed",false},{"status",400},{"code","qualification_input_forbidden"}}.dump();
    return Json{{"allowed",true},{"status",200},{"code","qualified_request"}}.dump();
  }catch(...){return Json{{"allowed",false},{"status",400},{"code","qualification_request_invalid"}}.dump();}
}
}
