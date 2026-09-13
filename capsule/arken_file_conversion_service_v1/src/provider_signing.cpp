#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include <algorithm>
namespace arken::file_conversion::service::v1::payment {
namespace {
std::string Base64Url(std::span<const std::uint8_t> bytes){
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;out.reserve((bytes.size()*4+2)/3);
    for(std::size_t i=0;i<bytes.size();i+=3){const auto remaining=bytes.size()-i;
        const std::uint32_t value=(std::uint32_t(bytes[i])<<16)|(remaining>1?std::uint32_t(bytes[i+1])<<8:0)|(remaining>2?bytes[i+2]:0);
        out+=alphabet[(value>>18)&63];out+=alphabet[(value>>12)&63];if(remaining>1)out+=alphabet[(value>>6)&63];if(remaining>2)out+=alphabet[value&63];
    }return out;
}
std::string Encoded(std::string_view s){return Base64Url(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size()));}
std::string SigningInput(const CdpRequestAuthorization& r){return Encoded(r.headerJson)+"."+Encoded(r.claimsJson);}
}
CdpSigningMaterial::~CdpSigningMaterial(){volatile auto* data=privateKeyPkcs8.data();for(std::size_t i=0;i<privateKeyPkcs8.size();++i)data[i]=0;}
std::optional<CdpSigningMaterial> PrepareCdpSigning(std::string_view id,std::string_view secret,CdpOperation operation,std::uint64_t now,std::string_view nonce) noexcept {
    try{
        const auto request=PrepareCdpAuthorization(id,operation,now,nonce);
        if(!request||secret.size()!=88||secret.substr(86)!="==")return std::nullopt;
        CdpSigningMaterial result;result.request=*request;result.signingInput=SigningInput(*request);
        // RFC8410 Ed25519 PrivateKeyInfo with the nested 32-byte OCTET STRING.
        constexpr std::uint8_t prefix[]={0x30,0x2e,0x02,0x01,0x00,0x30,0x05,0x06,0x03,0x2b,0x65,0x70,0x04,0x22,0x04,0x20};
        std::copy(std::begin(prefix),std::end(prefix),result.privateKeyPkcs8.begin());
        constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned bits=0;std::uint32_t accumulator=0;std::size_t count=0;
        for(std::size_t i=0;i<86;++i){const auto digit=alphabet.find(secret[i]);if(digit==std::string_view::npos)return std::nullopt;
            accumulator=(accumulator<<6)|static_cast<std::uint32_t>(digit);bits+=6;
            if(bits>=8){bits-=8;const auto byte=static_cast<std::uint8_t>(accumulator>>bits);if(count<32)result.privateKeyPkcs8[16+count]=byte;else if(count<64)result.publicKey[count-32]=byte;else return std::nullopt;++count;}
        }
        if(count!=64||bits!=4||(accumulator&15)!=0)return std::nullopt;
        return result;
    }catch(...){return std::nullopt;}
}
std::optional<std::string> CompleteCdpSigning(const CdpRequestAuthorization& request,std::span<const std::uint8_t> signature) noexcept {
    try{if(signature.size()!=64)return std::nullopt;return SigningInput(request)+"."+Base64Url(signature);}catch(...){return std::nullopt;}
}
}
