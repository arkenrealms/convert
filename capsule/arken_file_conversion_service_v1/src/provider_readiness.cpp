#include "arken_file_conversion_service_v1/provider_readiness.hpp"
#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include "arken_file_conversion_service_v1/reconciliation_configuration.hpp"
#include "rpc_evidence.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
namespace arken::file_conversion::service::v1 {
namespace {
namespace P=payment; namespace J=arken::schema::cpp::v1::tool_json; using P::detail::Json;
Json Unavailable(std::string_view code){return {{"kind","unavailable"},{"ready",false},{"code",code}};}
Json Response(const Json& value){P::detail::Fields(value,{"status","body"});J::require(value.at("body").is_string()&&value.at("body").get_ref<const std::string&>().size()<=262144,"startup response limit");return value;}

Json Probe(std::string_view slot,std::string_view method,std::string_view url,std::string_view authentication,std::string body=""){
  return {{"kind","probe"},{"ready",false},{"code","provider_checks_required"},{"slot",slot},
    {"request",{{"method",method},{"url",url},{"authentication",authentication},{"body",body}}}};
}
Json Readiness(const Json& j){
  P::detail::Fields(j,{"configuration","anonymous","supported","rpc","now"});
  const auto& c=j.at("configuration");P::detail::Fields(c,{"keyId","keySecret","rpcClientKey"});
  for(const auto name:{"keyId","keySecret","rpcClientKey"})
    if(!c.at(name).is_string()||c.at(name).get_ref<const std::string&>().empty())return Unavailable("production_configuration_missing");
  const auto now=J::readScalar<std::uint64_t>(j.at("now"));J::require(now>0&&now<=9007199254740000ULL,"provider clock invalid");
  const auto profile=P::PaymentProfileById("base-mainnet-v1");
  const auto endpoint=P::ResolveReconciliationEndpoint(*profile,c.at("rpcClientKey").get_ref<const std::string&>());if(!endpoint.error.empty())return Unavailable(endpoint.error);
  // Representation validation only; this nonce is never used to sign a request.
  if(!P::PrepareCdpSigning(c.at("keyId").get_ref<const std::string&>(),c.at("keySecret").get_ref<const std::string&>(),P::CdpOperation::supported,now,std::string(32,'0')))return Unavailable("production_cdp_key_invalid");
  const auto supportedEndpoint=P::CdpEndpointFor(P::CdpOperation::supported);
  if(j.at("anonymous").is_null())return Probe("anonymous",supportedEndpoint->method,supportedEndpoint->url,"none");
  if(Response(j.at("anonymous")).at("status")!=401)return Unavailable("provider_authentication_not_proven");
  if(j.at("supported").is_null())return Probe("supported",supportedEndpoint->method,supportedEndpoint->url,"cdp");
  if(Response(j.at("supported")).at("status")!=200)return Unavailable("provider_authentication_unavailable");
  const auto caps=P::ReadProviderCapabilities(j.at("supported").at("body").get_ref<const std::string&>());
  if(!caps||!caps->baseMainnet)return Unavailable("provider_mainnet_not_supported");
  if(j.at("rpc").is_null()){
    const auto call=[](unsigned id,std::string_view method,Json params){return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};};
    return Probe("rpc","POST",endpoint.url,"none",Json::array({call(1,"eth_chainId",Json::array()),call(2,"eth_getBlockByNumber",Json::array({"safe",false})),
      call(3,"eth_call",Json::array({Json{{"to",profile->assetText},{"data","0x313ce567"}},"safe"}))}).dump());
  }
  if(Response(j.at("rpc")).at("status")!=200)return Unavailable("production_rpc_unavailable");
  const auto replies=P::rpc::Replies(j.at("rpc").at("body"),{1,2,3});
  if(P::rpc::Quantity(replies.at(1))!=profile->chain)return Unavailable("production_rpc_wrong_chain");
  const auto block=P::rpc::Header(replies.at(2));
  if(block.number==0||block.time>now||now-block.time>1800)return Unavailable("production_rpc_header_unavailable");
  if(P::rpc::Data(replies.at(3),32)!="0x"+std::string(63,'0')+"6")return Unavailable("production_token_decimals_invalid");

  return {{"kind","ready"},{"ready",true},{"code","ready"}};
}
}
std::string ProviderReadinessPacket(std::string_view text) noexcept {
  try{return Readiness(payment::detail::Parse(text,1048576)).dump();}
  catch(...){return Unavailable("production_configuration_or_probe_invalid").dump();}
}
}
