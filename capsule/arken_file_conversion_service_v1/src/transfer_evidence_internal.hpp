#pragma once
#include "rpc_evidence.hpp"
namespace arken::file_conversion::service::v1::payment::transfer_evidence {
inline constexpr std::string_view authorizationTopic =
    "0x98de503528ee59b575ef0c0a2576a82497bfc029a5685b209e9ec333479b10a5";
struct Expectation {
  Scope scope;
  Address recipient;
  Digest nonce;
  std::uint64_t amount{}, validAfter{}, validBefore{};
};
void Identity(const detail::Json &log, const detail::Json &event);
void AuthorizationLog(const detail::Json &log, const Expectation &);
rpc::Block Receipt(const Expectation &, const detail::Json &event,
                   const rpc::Block &anchor, const detail::Json &receipt,
                   const detail::Json &block, const detail::Json &anchorNow);
} // namespace arken::file_conversion::service::v1::payment::transfer_evidence
