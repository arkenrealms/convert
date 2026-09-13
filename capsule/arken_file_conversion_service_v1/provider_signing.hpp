#pragma once
#include "arken_file_conversion_service_v1/provider_authorization.hpp"
#include <array>
#include <span>
namespace arken::file_conversion::service::v1::payment {
struct CdpSigningMaterial {
    CdpRequestAuthorization request;
    std::array<std::uint8_t,48> privateKeyPkcs8{};
    std::array<std::uint8_t,32> publicKey{};
    std::string signingInput;
    ~CdpSigningMaterial();
};
// Checks encoding and request scope. The cryptographic edge must additionally
// prove the public half matches the seed before using the resulting signature.
std::optional<CdpSigningMaterial> PrepareCdpSigning(std::string_view keyId,std::string_view secret,CdpOperation,std::uint64_t now,std::string_view nonce) noexcept;
std::optional<std::string> CompleteCdpSigning(const CdpRequestAuthorization&,std::span<const std::uint8_t> signature) noexcept;
// Private adapter ABI only, never a public MCP command or generic signing tool.
std::string CdpProviderPacket(std::string_view) noexcept;
}
