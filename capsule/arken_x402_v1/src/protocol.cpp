#include "arken_x402_v1/api.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
#include "arken/digest/v1/digest.hpp"
#include <algorithm>
namespace arken::x402::v1 {
namespace {
namespace J=arken::schema::cpp::v1::tool_json;using Json=J::Json;
void fields(const Json& j,std::initializer_list<const char*> names){J::require(j.is_object()&&j.size()==names.size(),"x402 fields");for(auto name:names)J::require(j.contains(name),"x402 field");}
template<std::size_t N> std::array<std::uint8_t,N> hex(const Json& j){auto text=J::readScalar<std::string>(j);J::require(text.size()==2+N*2&&text.starts_with("0x"),"x402 hex width");
    text.erase(0,2);for(auto& c:text)if(c>='A'&&c<='F')c=char(c-'A'+'a');const auto value=J::read<arken::schema::cpp::v1::fixed_id<N,1>>(text);std::array<std::uint8_t,N> out{};
    for(std::size_t i=0;i<N;++i)out[i]=std::to_integer<std::uint8_t>(value.value[i]);return out;}
template<std::size_t N> std::string hex(const std::array<std::uint8_t,N>& bytes){arken::schema::cpp::v1::fixed_id<N,1> value;for(std::size_t i=0;i<N;++i)value.value[i]=std::byte(bytes[i]);return "0x"+J::write(value).template get<std::string>();}
bool any(const Address& a){return std::ranges::any_of(a,[](auto b){return b!=0;});}
Json terms(const ExactEvmTerms& t){J::require(t.network.starts_with("eip155:")&&t.network.size()>7&&t.network.size()<=27&&t.network[7]!='0'
    &&std::all_of(t.network.begin()+7,t.network.end(),[](char c){return c>='0'&&c<='9';}),"x402 network");
    J::require(any(t.asset)&&any(t.recipient)&&t.amount>0&&t.maximumTimeoutSeconds>0&&t.maximumTimeoutSeconds<=600
        &&!t.domainName.empty()&&t.domainName.size()<=128&&!t.domainVersion.empty()&&t.domainVersion.size()<=32,"x402 terms");
    return {{"scheme","exact"},{"network",t.network},{"asset",hex(t.asset)},{"amount",std::to_string(t.amount)},{"payTo",hex(t.recipient)},
        {"maxTimeoutSeconds",t.maximumTimeoutSeconds},{"extra",{{"name",t.domainName},{"version",t.domainVersion}}}};}
Json resource(const Resource& r){J::require(!r.url.empty()&&r.url.size()<=1024&&(r.url.starts_with("mcp://tool/")||r.url.starts_with("https://"))
    &&r.url.find_first_of("\r\n\t ")==std::string::npos&&r.description.size()<=512&&r.mimeType=="application/json","x402 resource");
    return {{"url",r.url},{"description",r.description},{"mimeType",r.mimeType}};}
}
std::string addressText(const Address& a){return hex(a);}
std::string requirements(const ExactEvmTerms& t) noexcept {try{return terms(t).dump();}catch(...){return {};}}
std::string paymentRequired(const ExactEvmTerms& t,const Resource& r) noexcept {
    try{const Json required{{"x402Version",2},{"error","PAYMENT_REQUIRED"},{"resource",resource(r)},{"accepts",Json::array({terms(t)})}};
        return Json{{"isError",true},{"structuredContent",required},{"content",Json::array({{{"type","text"},{"text",required.dump()}}})}}.dump();
    }catch(...){return {};}
}
std::optional<InspectedPayment> inspectSubmission(const ExactEvmTerms& t,const Resource& r,std::string_view text,std::uint64_t expiry,const Address& bound) noexcept {
    try{auto p=arken::mcp::v1::parseRequestJson(text);fields(p,{"x402Version","resource","accepted","payload"});J::require(J::readScalar<std::uint32_t>(p.at("x402Version"))==2,"x402 version");
        auto accepted=p.at("accepted");accepted["asset"]=hex(hex<20>(accepted.at("asset")));accepted["payTo"]=hex(hex<20>(accepted.at("payTo")));
        J::require(accepted==terms(t)&&p.at("resource")==resource(r),"x402 offer binding");auto& body=p.at("payload");fields(body,{"signature","authorization"});
        auto signature=J::readScalar<std::string>(body.at("signature"));J::require(signature.starts_with("0x")&&signature.size()>=4&&signature.size()<=4098&&signature.size()%2==0,"signature shape");
        for(std::size_t i=2;i<signature.size();++i){auto& c=signature[i];if(c>='A'&&c<='F')c=char(c-'A'+'a');J::require((c>='0'&&c<='9')||(c>='a'&&c<='f'),"signature hex");}body["signature"]=signature;
        auto& auth=body.at("authorization");fields(auth,{"from","to","value","validAfter","validBefore","nonce"});InspectedPayment value;value.payer=hex<20>(auth.at("from"));
        J::require(any(value.payer)&&(!any(bound)||bound==value.payer)&&hex<20>(auth.at("to"))==t.recipient&&J::readScalar<std::uint64_t>(auth.at("value"))==t.amount,"authorization binding");
        value.nonce=hex<32>(auth.at("nonce"));value.validAfter=J::readScalar<std::uint64_t>(auth.at("validAfter"));value.validBefore=J::readScalar<std::uint64_t>(auth.at("validBefore"));
        J::require(value.validAfter<value.validBefore&&value.validBefore<=expiry,"authorization lifetime");
        auth["from"]=hex(value.payer);auth["to"]=hex(t.recipient);auth["nonce"]=hex(value.nonce);p["accepted"]=accepted;value.normalizedPayload=p.dump();
        const auto digest=arken::digest::v1::sha256(std::as_bytes(std::span(value.normalizedPayload.data(),value.normalizedPayload.size())));J::require(digest.has_value(),"payload digest");value.payloadDigest=digest->bytes;return value;
    }catch(...){return std::nullopt;}
}
std::string verificationBody(const ExactEvmTerms& t,const InspectedPayment& p) noexcept {
    try{const auto payload=arken::mcp::v1::parseRequestJson(p.normalizedPayload);const auto& r=payload.at("resource");
        const Resource offered{J::readScalar<std::string>(r.at("url")),J::readScalar<std::string>(r.at("description")),J::readScalar<std::string>(r.at("mimeType"))};
        const auto checked=inspectSubmission(t,offered,p.normalizedPayload,p.validBefore,p.payer);
        J::require(checked&&checked->payer==p.payer&&checked->nonce==p.nonce&&checked->payloadDigest==p.payloadDigest
            &&checked->validAfter==p.validAfter&&checked->validBefore==p.validBefore&&checked->normalizedPayload==p.normalizedPayload,"inspection identity");
        return Json{{"x402Version",2},{"paymentPayload",payload},{"paymentRequirements",terms(t)}}.dump();}catch(...){return {};}
}
bool matchesVerifiedPayer(std::string_view text,const Address& expected) noexcept {
    try{const auto value=arken::mcp::v1::parseRequestJson(text);return any(expected)&&J::readScalar<bool>(value.at("isValid"))&&hex<20>(value.at("payer"))==expected;}catch(...){return false;}
}
}
