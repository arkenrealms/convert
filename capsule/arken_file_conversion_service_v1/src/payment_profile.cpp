#include "arken_file_conversion_service_v1/payment_profile.hpp"
namespace arken::file_conversion::service::v1::payment {
namespace {
constexpr Address AddressBytes(std::string_view text){Address out{};
    const auto digit=[](char c){return static_cast<unsigned>(c<='9'?c-'0':c-'a'+10);};
    for(std::size_t i=0;i<20;++i)out[i]=static_cast<std::uint8_t>((digit(text[2*i])<<4)|digit(text[2*i+1]));return out;
}
constexpr PaymentProfile profiles[]={
    {"base-sepolia-v1","eip155:84532","0x036cbd53842c5426634e7929541ec2318f3dcf7e","https://x402.org/facilitator", "arken-convert:base-sepolia:image:v1:10000",
      84532,AddressBytes("036cbd53842c5426634e7929541ec2318f3dcf7e"),10000,FacilitatorAuthentication::none,ReconciliationProvider::publicBaseSepolia,"USDC","2"},
    {"base-mainnet-v1","eip155:8453","0x833589fcd6edb6e08f4c7c32d4f71b54bda02913","https://api.cdp.coinbase.com/platform/v2/x402", "arken-convert:base-mainnet:image:v1:10000",
      8453,AddressBytes("833589fcd6edb6e08f4c7c32d4f71b54bda02913"),10000,FacilitatorAuthentication::cdp,ReconciliationProvider::cdpBaseMainnet,"USD Coin","2"}
};
}
std::optional<PaymentProfile> PaymentProfileById(std::string_view id) noexcept {for(const auto& p:profiles)if(p.id==id)return p;return std::nullopt;}
std::optional<PaymentProfile> PaymentProfileForQuote(const Quote& q) noexcept {for(const auto& p:profiles)if(q.scope.chain==p.chain&&q.scope.asset==p.asset)return p;return std::nullopt;}
Address ImagePaymentRecipient() noexcept {return AddressBytes("554933cd7c5275ff3692bf97e0bc90b69c85d3dc");}
}
