#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
// Private read-only RPC planner/verifier, not a public evidence-submission API.
// No request returned here can broadcast or settle a transaction.
std::string SettlementReconciliationPacket(std::string_view) noexcept;
// Direct receipt inspection for a known transaction, with no ledger commands.
std::string PaymentTransferEvidencePacket(std::string_view) noexcept;
// Domain-neutral bounded nonce scan and receipt verification. Expected fields
// come from a trusted retained authorization, not a public caller assertion.
// Returns read-only RPC requests, pending, or verified evidence; no journal
// command, output shape, signature, settlement or transaction submission.
std::string PaymentAuthorizationEvidencePacket(std::string_view) noexcept;
}
