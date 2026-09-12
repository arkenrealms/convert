#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
enum class CdpOperation {supported,verify,settle};
struct CdpEndpoint {std::string method,url;};
std::optional<CdpEndpoint> CdpEndpointFor(CdpOperation) noexcept;
std::optional<CdpOperation> CdpOperationFor(std::string_view method,std::string_view url) noexcept;
struct CdpRequestAuthorization {
    std::string method,url,headerJson,claimsJson;
};
// Pure request scope; no secret, clock, network, signing or payment dispatch.
std::optional<CdpRequestAuthorization> PrepareCdpAuthorization(std::string_view keyId,CdpOperation,std::uint64_t now,std::string_view nonce) noexcept;
struct ProviderCapabilities {
    bool baseMainnet=false,baseSepolia=false;
    std::uint32_t kindCount=0,extensionCount=0,signerGroupCount=0;
};
// A capability declaration is not credential, token or settlement acceptance.
std::optional<ProviderCapabilities> ReadProviderCapabilities(std::string_view) noexcept;
}
