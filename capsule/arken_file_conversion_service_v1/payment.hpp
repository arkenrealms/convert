#pragma once
#include "arken_paid_operation_v1/api.hpp"
namespace arken::file_conversion::service::v1::payment {
namespace policy = arken::paid_operation::v1;
using policy::Action;
using policy::Address;
using policy::Attempt;
using policy::Authorization;
using policy::Digest;
using policy::Error;
using policy::EventKind;
using policy::Failure;
using policy::NonceClaim;
using policy::Quote;
using policy::Scope;
using policy::Stage;
using policy::Transfer;
// Image commitment belongs to the conversion domain, not payment policy.
struct Output {
  Digest digest{}, release{};
  std::uint32_t bytes{}, width{}, height{};
  bool operator==(const Output &) const = default;
};
using Operation = policy::OperationRecord<Output>;
using Event = policy::EventRecord<Output>;
using Decision = policy::DecisionRecord<Output>;
using Recovery = policy::RecoveryRecord<Output>;
[[nodiscard]] Decision Advance(const Operation &, const Quote &,
                               const std::optional<NonceClaim> &,
                               const Event &) noexcept;
[[nodiscard]] bool ValidOperationRecord(const Operation &) noexcept;
[[nodiscard]] std::uint64_t AttemptDeadline(const Operation &,
                                            std::uint64_t) noexcept;
[[nodiscard]] Recovery Recover(const Operation &,
                               const std::optional<NonceClaim> &, const Event &,
                               const Attempt &) noexcept;
} // namespace arken::file_conversion::service::v1::payment
