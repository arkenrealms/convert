#include "arken_file_conversion_service_v1/provider_authorization.hpp"
#include "payment_json.hpp"
#include <algorithm>
#include <set>
namespace arken::file_conversion::service::v1::payment {
namespace {
bool Hex(std::string_view s){return !s.empty()&&s.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool Uuid(std::string_view s){
    if(s.size()!=36)return false;
    for(std::size_t i=0;i<s.size();++i){if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!Hex(s.substr(i,1)))return false;}return true;
}
bool KeyId(std::string_view s){
    if(Uuid(s))return true;
    return s.size()==95&&s.starts_with("organizations/")&&s.substr(50,9)=="/apiKeys/"&&Uuid(s.substr(14,36))&&Uuid(s.substr(59));
}
void Text(const detail::Json& j,std::size_t maximum){
    if(!j.is_string())throw std::invalid_argument("provider string required");
    const auto& s=j.get_ref<const std::string&>();
    if(s.empty()||s.size()>maximum||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c>126;}))throw std::invalid_argument("provider text bounds");
}
}
std::optional<CdpRequestAuthorization> PrepareCdpAuthorization(std::string_view id,CdpOperation operation,std::uint64_t now,std::string_view nonce) noexcept {
    try{
        // Match pinned CDP SDK a1195adcfa5a93627bd3cb79831b188cc13073a1:
        // uris array, iat/nbf/exp, nonce; no singular-uri or audience alias.
        if(!KeyId(id)||nonce.size()!=32||!Hex(nonce)||now==0||now>9007199254740871ULL)return std::nullopt;
        const auto endpoint=CdpEndpointFor(operation);if(!endpoint)return std::nullopt;
        const detail::Json header={{"alg","EdDSA"},{"typ","JWT"},{"kid",id},{"nonce",nonce}};
        const detail::Json claims={{"sub",id},{"iss","cdp"},{"iat",now},{"nbf",now},{"exp",now+120},
            {"uris",detail::Json::array({endpoint->method+" "+endpoint->url.substr(8)})}};
        return CdpRequestAuthorization{endpoint->method,endpoint->url,header.dump(),claims.dump()};
    }catch(...){return std::nullopt;}
}
std::optional<ProviderCapabilities> ReadProviderCapabilities(std::string_view text) noexcept {
    try{
        const auto j=detail::Parse(text,65536);detail::Fields(j,{"kinds","extensions","signers"});
        const auto& kinds=j.at("kinds");const auto& extensions=j.at("extensions");const auto& signers=j.at("signers");
        if(!kinds.is_array()||kinds.size()>128||!extensions.is_array()||extensions.size()>64||!signers.is_object()||signers.size()>64)return std::nullopt;
        ProviderCapabilities result;result.kindCount=static_cast<std::uint32_t>(kinds.size());result.extensionCount=static_cast<std::uint32_t>(extensions.size());result.signerGroupCount=static_cast<std::uint32_t>(signers.size());
        for(const auto& k:kinds){
            if(!k.is_object()||!k.contains("x402Version")||!k.at("x402Version").is_number_unsigned())return std::nullopt;
            Text(k.at("scheme"),64);Text(k.at("network"),128);
            if(k.at("x402Version")==2&&k.at("scheme")=="exact"){
                if(k.at("network")=="eip155:8453")result.baseMainnet=true;
                if(k.at("network")=="eip155:84532")result.baseSepolia=true;
            }
        }
        std::set<std::string> seen;
        for(const auto& e:extensions){Text(e,128);if(!seen.insert(e.get<std::string>()).second)return std::nullopt;}
        for(auto it=signers.begin();it!=signers.end();++it){Text(detail::Json(it.key()),128);if(!it.value().is_array()||it.value().size()>128)return std::nullopt;
            for(const auto& address:it.value())Text(address,128);}
        return result;
    }catch(...){return std::nullopt;}
}
}
