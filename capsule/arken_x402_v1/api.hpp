#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace arken::x402::v1 {
using Address=std::array<std::uint8_t,20>;
using Digest=std::array<std::uint8_t,32>;
struct ExactEvmTerms {
    std::string network;
    Address asset{},recipient{};
    std::uint64_t amount=0;
    std::uint32_t maximumTimeoutSeconds=60;
    std::string domainName,domainVersion;
};
struct Resource {std::string url,description,mimeType="application/json";};
// Inspection validates protocol shape and offer binding, NOT cryptographic
// signatures, chain state, payer authentication, or payment settlement.
struct InspectedPayment {
    Address payer{};Digest nonce{},payloadDigest{};
    std::uint64_t validAfter=0,validBefore=0;
    std::string normalizedPayload;
};
[[nodiscard]] std::string requirements(const ExactEvmTerms&) noexcept;
[[nodiscard]] std::string paymentRequired(const ExactEvmTerms&,const Resource&) noexcept;
[[nodiscard]] std::optional<InspectedPayment> inspectSubmission(const ExactEvmTerms&,const Resource&,
    std::string_view payload,std::uint64_t maximumExpiry,const Address& boundPayer={}) noexcept;
[[nodiscard]] std::string verificationBody(const ExactEvmTerms&,const InspectedPayment&) noexcept;
[[nodiscard]] bool matchesVerifiedPayer(std::string_view response,const Address&) noexcept;
[[nodiscard]] std::string addressText(const Address&);
}
