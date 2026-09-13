#pragma once
#include "arken_file_conversion_service_v1/payment.hpp"
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
enum class FacilitatorAuthentication {none,cdp};
enum class ReconciliationProvider {publicBaseSepolia,cdpBaseMainnet};
struct PaymentProfile {
    std::string_view id,network,assetText,facilitator,priceIdentity;
    std::uint32_t chain;
    Address asset;
    std::uint64_t imagePrice;
    FacilitatorAuthentication authentication;
    ReconciliationProvider reconciliationProvider;
};
std::optional<PaymentProfile> PaymentProfileById(std::string_view) noexcept;
// Always resolve a retained operation from its own chain/token, never a new default.
std::optional<PaymentProfile> PaymentProfileForQuote(const Quote&) noexcept;
Address ImagePaymentRecipient() noexcept;
}
