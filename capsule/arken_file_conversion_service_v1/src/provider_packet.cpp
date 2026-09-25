#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include "provider_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
namespace arken::file_conversion::service::v1::payment {
namespace {
namespace J=arken::schema::cpp::v1::tool_json;
CdpOperation RequestOperation(const provider_json::Json& request){
    provider_json::Fields(request,{"method","url"});const auto method=J::readScalar<std::string>(request.at("method")),url=J::readScalar<std::string>(request.at("url"));
    const auto operation=CdpOperationFor(method,url);J::require(operation.has_value(),"unapproved CDP request");return *operation;
}
provider_json::Json Transport(const provider_json::Json& packet){
    provider_json::Fields(packet,{"kind","request"});const auto& request=packet.at("request");
    J::require(request.is_object()&&request.contains("method")&&request.contains("url"),"provider request");
    for(auto it=request.begin();it!=request.end();++it)J::require(it.key()=="method"||it.key()=="url"||it.key()=="authentication"||it.key()=="body","unknown provider field");
    const auto operation=RequestOperation({{"method",request.at("method")},{"url",request.at("url")}});
    if(request.contains("authentication"))J::require(request.at("authentication")=="cdp","provider authentication");
    const auto endpoint=*CdpEndpointFor(operation);provider_json::Json body=nullptr;
    if(operation==CdpOperation::supported){
        J::require(!request.contains("body")||request.at("body").is_null()||request.at("body")=="","GET body");
    }else{
        J::require(request.contains("body")&&request.at("body").is_string(),"POST body");
        const auto& text=request.at("body").get_ref<const std::string&>();
        J::require(!text.empty()&&text.size()<=8192,"provider body limit");body=text;
    }
    return {{"request",{{"method",endpoint.method},{"url",endpoint.url},{"authentication","cdp"},{"body",body}}},{"responseLimit",16384},{"deadlineMs",30000}};
}
struct SecretText {std::string& value;~SecretText(){volatile char* p=value.data();for(std::size_t i=0;i<value.size();++i)p[i]=0;}};
}
std::string CdpProviderPacket(std::string_view text) noexcept {
    try{
        auto j=provider_json::Parse(text,16384);const auto kind=J::readScalar<std::string>(j.at("kind"));
        if(kind=="transport")return Transport(j).dump();
        J::require(text.size()<=4096,"CDP signing packet limit");
        if(kind=="prepare")provider_json::Fields(j,{"kind","request","keyId","keySecret","now","nonce"});
        else {J::require(kind=="complete","unknown CDP packet");provider_json::Fields(j,{"kind","request","keyId","now","nonce","signature"});}
        const auto op=RequestOperation(j.at("request"));const auto id=J::readScalar<std::string>(j.at("keyId")),nonce=J::readScalar<std::string>(j.at("nonce"));
        const auto now=J::readScalar<std::uint64_t>(j.at("now"));
        if(kind=="prepare"){
            J::require(j.at("keySecret").is_string(),"key secret shape");auto& secret=j.at("keySecret").get_ref<std::string&>();SecretText cleanup{secret};
            const auto material=PrepareCdpSigning(id,secret,op,now,nonce);J::require(material.has_value(),"invalid CDP signing input");
            return provider_json::Json{{"request",j.at("request")},{"privateKeyPkcs8",material->privateKeyPkcs8},{"publicKey",material->publicKey},{"signingInput",material->signingInput}}.dump();
        }
        const auto request=PrepareCdpAuthorization(id,op,now,nonce);J::require(request.has_value(),"invalid CDP completion");
        J::require(j.at("signature").is_array()&&j.at("signature").size()==64,"signature size");std::array<std::uint8_t,64> signature{};
        for(std::size_t i=0;i<64;++i)signature[i]=J::readScalar<std::uint8_t>(j.at("signature")[i]);
        const auto token=CompleteCdpSigning(*request,signature);J::require(token.has_value(),"CDP token formatting unavailable");
        return provider_json::Json{{"request",j.at("request")},{"authorization","Bearer "+*token}}.dump();
    }catch(...){return "{\"packetError\":\"invalid_cdp_provider_packet\"}";}
}
}
