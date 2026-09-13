#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
// Private read-only RPC planner/verifier, not a public evidence-submission API.
// No request returned here can broadcast or settle a transaction.
std::string SettlementReconciliationPacket(std::string_view) noexcept;
// Direct receipt inspection for a known transaction, with no ledger commands.
std::string PaymentTransferEvidencePacket(std::string_view) noexcept;
}
