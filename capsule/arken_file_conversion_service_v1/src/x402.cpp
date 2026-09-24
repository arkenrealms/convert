#include "arken_file_conversion_service_v1/x402.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_file_conversion_service_v1/provider_authorization.hpp"
#include "payment_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "arken/digest/v1/digest.hpp"
#include <algorithm>
namespace arken::file_conversion::service::v1::payment {
namespace {
using detail::Json;
namespace J=arken::schema::cpp::v1::tool_json;
PaymentProfile Profile(const Quote& quote){const auto profile=PaymentProfileForQuote(quote);J::require(profile.has_value(),"unsupported payment profile");return *profile;}
CdpEndpoint EffectEndpoint(const PaymentProfile& profile,bool verify){
  if(profile.authentication==FacilitatorAuthentication::cdp){const auto endpoint=CdpEndpointFor(verify?CdpOperation::verify:CdpOperation::settle);J::require(endpoint.has_value(),"CDP endpoint unavailable");return *endpoint;}
  return {"POST",std::string(profile.facilitator)+(verify?"/verify":"/settle")};
}
Json Transport(const Json& j){
  detail::Fields(j,{"kind","request"});const auto& r=j.at("request");detail::Fields(r,{"method","url","authentication","body"});
  const auto method=J::readScalar<std::string>(r.at("method")),url=J::readScalar<std::string>(r.at("url")),authentication=J::readScalar<std::string>(r.at("authentication"));
  J::require(J::readScalar<std::string>(r.at("body")).size()<=8192,"facilitator body limit");
  for(const auto id:{"base-sepolia-v1","base-mainnet-v1"})for(bool verify:{false,true}){
    const auto profile=PaymentProfileById(id);const auto endpoint=EffectEndpoint(*profile,verify);
    if(method==endpoint.method&&url==endpoint.url&&authentication==(profile->authentication==FacilitatorAuthentication::cdp?"cdp":"none"))return {{"request",r}};
  }throw std::invalid_argument("facilitator request scope invalid");
}
template<std::size_t N> std::array<std::uint8_t,N> Hex(const Json& j){
  auto s=J::readScalar<std::string>(j);J::require(s.size()==2+N*2&&s.starts_with("0x"),"hex width");
  s.erase(0,2);for(auto& c:s)if(c>='A'&&c<='F')c=char(c-'A'+'a');
  const auto id=J::read<arken::schema::cpp::v1::fixed_id<N,1>>(s);std::array<std::uint8_t,N> out{};
  for(std::size_t i=0;i<N;++i)out[i]=std::to_integer<std::uint8_t>(id.value[i]);return out;
}
template<std::size_t N> std::string Hex(const std::array<std::uint8_t,N>& bytes){
  arken::schema::cpp::v1::fixed_id<N,1> id;for(std::size_t i=0;i<N;++i)id.value[i]=std::byte(bytes[i]);return "0x"+J::write(id).template get<std::string>();
}
bool Any(const Address& a){return std::ranges::any_of(a,[](auto b){return b!=0;});}
Json Requirements(const Quote& q){
  const auto profile=Profile(q);J::require(Any(q.recipient)&&q.amount>0,"payment recipient or amount missing");
  return {{"scheme","exact"},{"network",profile.network},{"asset",profile.assetText},{"amount",std::to_string(q.amount)},
    {"payTo",Hex(q.recipient)},{"maxTimeoutSeconds",60},{"extra",{{"name",profile.domainName},{"version",profile.domainVersion}}}};
}
Json Resource(const Quote& q){return {{"url","mcp://tool/conversion.v1.convert_image/"+Hex(q.operation).substr(2)},
  {"description","Bounded image conversion"},{"mimeType","application/json"}};}
Json Command(const Quote& q,const Event& e){return {{"version",1},{"quote",detail::WriteQuote(q)},{"event",detail::WriteEvent(e)}};}
struct Submitted {Quote quote;Authorization auth;Json payload;};
Submitted Submission(Quote q,std::string_view text){
  auto p=detail::Parse(text);detail::Fields(p,{"x402Version","resource","accepted","payload"});
  J::require(J::readScalar<std::uint32_t>(p.at("x402Version"))==2,"x402 version");
  auto accepted=p.at("accepted");accepted["asset"]=Hex(Hex<20>(accepted.at("asset")));accepted["payTo"]=Hex(Hex<20>(accepted.at("payTo")));
  J::require(accepted==Requirements(q)&&p.at("resource")==Resource(q),"payment requirements changed");
  auto& body=p.at("payload");detail::Fields(body,{"signature","authorization"});
  auto signature=J::readScalar<std::string>(body.at("signature"));
  J::require(signature.starts_with("0x")&&signature.size()>=4&&signature.size()<=4098&&signature.size()%2==0,"signature shape");
  for(std::size_t i=2;i<signature.size();++i){auto& c=signature[i];if(c>='A'&&c<='F')c=char(c-'A'+'a');J::require((c>='0'&&c<='9')||(c>='a'&&c<='f'),"signature hex");}
  body["signature"]=signature;
  auto& auth=body.at("authorization");detail::Fields(auth,{"from","to","value","validAfter","validBefore","nonce"});
  const auto payer=Hex<20>(auth.at("from"));J::require(Any(payer)&&(!Any(q.scope.payer)||q.scope.payer==payer),"payer changed");q.scope.payer=payer;
  J::require(Hex<20>(auth.at("to"))==q.recipient&&J::readScalar<std::uint64_t>(auth.at("value"))==q.amount,"authorization amount or recipient changed");
  Authorization a;a.nonce=Hex<32>(auth.at("nonce"));a.validAfter=J::readScalar<std::uint64_t>(auth.at("validAfter"));a.validBefore=J::readScalar<std::uint64_t>(auth.at("validBefore"));
  J::require(a.validAfter<a.validBefore&&a.validBefore<=q.expiresAt,"authorization lifetime");
  auth["from"]=Hex(payer);auth["to"]=Hex(q.recipient);auth["nonce"]=Hex(a.nonce);p["accepted"]=accepted;
  a.payload=detail::PayloadDigest(p.dump());return {q,a,p};
}
Json Prepare(const Json& j){
  detail::Fields(j,{"kind","quote","payment","now"});const auto q=detail::ReadQuote(j.at("quote"));const auto now=J::readScalar<std::uint64_t>(j.at("now"));
  if(j.at("payment").is_null()){
    J::require(now<q.expiresAt&&q.expiresAt-now>=60,"file expiry too close");
    const Json required={{"x402Version",2},{"error","PAYMENT_REQUIRED"},{"resource",Resource(q)},{"accepts",Json::array({Requirements(q)})}};
    return {{"kind","challenge"},{"result",{{"isError",true},{"structuredContent",required},{"content",Json::array({{{"type","text"},{"text",required.dump()}}})}}}};
  }
  const auto submitted=Submission(q,J::readScalar<std::string>(j.at("payment")));
  Event event;event.kind=EventKind::request;event.now=now;event.authorization=submitted.auth;
  auto command=Command(submitted.quote,event);command["payment"]=submitted.payload.dump();
  Event inspect=event;inspect.kind=EventKind::inspect;inspect.authorization.reset();
  return {{"kind","admit"},{"command",command.dump()},{"payment",submitted.payload.dump()},
    {"inspectCommand",Command(submitted.quote,inspect).dump()}};
}
Json Effect(const Json& j){
  detail::Fields(j,{"kind","decision","payment","now"});const auto& decision=j.at("decision");
  const auto op=detail::ReadOperation(decision.at("operation"));const auto action=J::readScalar<std::uint8_t>(decision.at("action"));
  J::require(ValidOperationRecord(op),"invalid effect record");
  if(J::readScalar<std::uint8_t>(decision.at("error"))!=0)return {{"effect","rejected"}};
  if(action!=unsigned(Action::verify)&&action!=unsigned(Action::settle))return {{"effect","none"}};
  J::require(J::readScalar<bool>(decision.at("changed"))&&op.authorization.has_value(),"effect was not admitted");
  const bool verify=action==unsigned(Action::verify);J::require(op.stage==(verify?Stage::verifying:Stage::settling),"effect stage mismatch");
  const auto submitted=Submission(op.quote,J::readScalar<std::string>(j.at("payment")));
  J::require(submitted.auth==*op.authorization,"stored payment changed");const auto now=J::readScalar<std::uint64_t>(j.at("now"));
  J::require(submitted.auth.validAfter<now&&now<submitted.auth.validBefore&&submitted.auth.validBefore-now<=60,"payment time window");
  const auto profile=Profile(op.quote);const bool cdp=profile.authentication==FacilitatorAuthentication::cdp;
  const auto endpoint=EffectEndpoint(profile,verify);
  return {{"effect",verify?"verify":"settle"},{"request",{{"method",endpoint.method},{"url",endpoint.url},{"authentication",cdp?"cdp":"none"},
    {"body",Json{{"x402Version",2},{"paymentPayload",submitted.payload},{"paymentRequirements",Requirements(op.quote)}}.dump()}}}};
}
Json Callback(const Json& j){
  detail::Fields(j,{"kind","operation","phase","response","status","now"});const auto op=detail::ReadOperation(j.at("operation"));Requirements(op.quote);
  J::require(ValidOperationRecord(op),"invalid callback record");
  const auto phase=J::readScalar<std::string>(j.at("phase"));const bool verify=phase=="verify";J::require(verify||phase=="settle","callback phase");
  J::require(op.authorization&&op.stage==(verify?Stage::verifying:Stage::settling),"callback not dispatched");
  Event event;event.expectedRevision=op.revision;event.now=J::readScalar<std::uint64_t>(j.at("now"));
  event.kind=EventKind::settlementUncertain;
  const auto status=J::readScalar<std::uint32_t>(j.at("status"));J::require(status<=599,"invalid HTTP status");
  std::string reason=status==0?"transport":"http_"+std::to_string(status);
  if(status==0){
    try{const auto name=detail::Parse(J::readScalar<std::string>(j.at("response"))).value("transportError","");
      if(name=="AbortError"||name=="TimeoutError")reason="transport_timeout";
      else if(name=="TypeError")reason="transport_type_error";
    }catch(const std::exception&){reason="transport";}
  }
  try{
    J::require(status==200,"facilitator HTTP failed");reason="invalid_json";
    const auto response=detail::Parse(J::readScalar<std::string>(j.at("response")));
    if(verify){
      reason="invalid_validity";
      if(!J::readScalar<bool>(response.at("isValid")))event.kind=EventKind::verificationRejected;
      else {reason="invalid_payer";event.verifiedPayer=Hex<20>(response.at("payer"));J::require(event.verifiedPayer==op.quote.scope.payer,"verified payer mismatch");event.kind=EventKind::verificationPassed;}
    }else{
      J::require(J::readScalar<bool>(response.at("success"))&&response.at("network")==Profile(op.quote).network&&Hex<20>(response.at("payer"))==op.quote.scope.payer,"settlement not proven");
      Transfer transfer;transfer.transaction=Hex<32>(response.at("transaction"));J::require(std::ranges::any_of(transfer.transaction,[](auto b){return b!=0;}),"empty transaction");
      transfer.scope=op.quote.scope;transfer.recipient=op.quote.recipient;transfer.amount=op.quote.amount;transfer.nonce=op.authorization->nonce;
      event.transfer=transfer;event.kind=EventKind::settlementSucceeded;
    }
  }catch(const std::exception&){if(verify)return {{"kind","unavailable"},{"code","facilitator_verification_unavailable_"+reason}};}
  return {{"kind","callback"},{"command",Command(op.quote,event).dump()}};
}
Json Control(const Json& j){
  detail::Fields(j,{"kind","operation","event","now","release"});const auto op=detail::ReadOperation(j.at("operation"));
  Event event;event.expectedRevision=op.revision;event.now=J::readScalar<std::uint64_t>(j.at("now"));
  const auto name=J::readScalar<std::string>(j.at("event"));
  if(name=="inspect")event.kind=EventKind::inspect;
  else if(name=="retryVerification")event.kind=EventKind::retryVerification;
  else if(name=="resume"){event.kind=EventKind::request;event.executingRelease=Hex<32>(j.at("release"));}
  else throw std::invalid_argument("unsupported controller event");
  return {{"command",Command(op.quote,event).dump()}};
}
Json OutputCommand(const Json& j){
  detail::Fields(j,{"kind","operation","output","now"});const auto op=detail::ReadOperation(j.at("operation"));
  J::require(ValidOperationRecord(op)&&op.stage==Stage::computing,"output operation not computing");
  Event event;event.kind=EventKind::outputCommitted;event.expectedRevision=op.revision;event.now=J::readScalar<std::uint64_t>(j.at("now"));event.output=detail::ReadOutput(j.at("output"));
  return {{"command",Command(op.quote,event).dump()}};
}
}
std::string X402Packet(std::string_view text) noexcept {
  try{const auto j=detail::Parse(text);const auto kind=J::readScalar<std::string>(j.at("kind"));
    if(kind=="transport")return Transport(j).dump();
    if(kind=="prepare")return Prepare(j).dump();if(kind=="effect")return Effect(j).dump();if(kind=="callback")return Callback(j).dump();
    if(kind=="control")return Control(j).dump();
    if(kind=="output")return OutputCommand(j).dump();
    return "{\"packetError\":\"unknown_x402_packet\"}";
  }catch(const std::exception&){return "{\"packetError\":\"invalid_x402_packet\"}";}
  catch(...){return "{\"packetError\":\"x402_unavailable\"}";}
}
}
