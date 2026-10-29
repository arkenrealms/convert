#include "arken_file_conversion_service_v1/provider_native.hpp"
#include <stdexcept>
#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <array>
#include <memory>
#include <span>
namespace arken::file_conversion::service::v1::payment {
namespace {
void Require(bool value, std::string_view code) { if (!value) throw std::runtime_error(std::string(code)); }
template<class T,auto Free> using Ptr=std::unique_ptr<T,decltype(Free)>;
}
std::string SignCdpRequest(std::string_view id,std::string_view secret,CdpOperation operation,std::uint64_t now,std::string_view nonce){
    namespace A=arken::file_conversion::service::v1::payment;
    const auto material=A::PrepareCdpSigning(id,secret,operation,now,nonce);Require(material.has_value(),"cdp_request_or_key_format_invalid");
    const unsigned char* cursor=material->privateKeyPkcs8.data();
    Ptr<EVP_PKEY,EVP_PKEY_free> privateKey(d2i_AutoPrivateKey(nullptr,&cursor,static_cast<long>(material->privateKeyPkcs8.size())),EVP_PKEY_free);
    Require(bool(privateKey),"cdp_key_import_failed");std::array<unsigned char,32> publicKey{};std::size_t n=publicKey.size();
    Require(cursor==material->privateKeyPkcs8.data()+material->privateKeyPkcs8.size()&&EVP_PKEY_is_a(privateKey.get(),"ED25519"),"cdp_pkcs8_shape_invalid");
    Require(EVP_PKEY_get_raw_public_key(privateKey.get(),publicKey.data(),&n)==1&&n==32&&CRYPTO_memcmp(publicKey.data(),material->publicKey.data(),32)==0,"cdp_key_public_half_mismatch");
    const auto& input=material->signingInput;
    Ptr<EVP_MD_CTX,EVP_MD_CTX_free> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);Require(bool(context),"cdp_signer_context_failed");
    std::array<unsigned char,64> signature{};n=signature.size();
    Require(EVP_DigestSignInit(context.get(),nullptr,nullptr,nullptr,privateKey.get())==1&&EVP_DigestSign(context.get(),signature.data(),&n,reinterpret_cast<const unsigned char*>(input.data()),input.size())==1&&n==64,"cdp_signing_failed");
    Ptr<EVP_PKEY,EVP_PKEY_free> verifier(EVP_PKEY_new_raw_public_key_ex(nullptr,"ED25519",nullptr,publicKey.data(),publicKey.size()),EVP_PKEY_free);
    Require(bool(verifier)&&EVP_DigestVerifyInit(context.get(),nullptr,nullptr,nullptr,verifier.get())==1&&EVP_DigestVerify(context.get(),signature.data(),signature.size(),reinterpret_cast<const unsigned char*>(input.data()),input.size())==1,"cdp_signature_self_check_failed");
    const auto token=A::CompleteCdpSigning(material->request,signature);Require(token.has_value(),"cdp_token_format_failed");return *token;
}
}
