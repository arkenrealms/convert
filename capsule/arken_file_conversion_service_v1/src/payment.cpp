#include "arken_file_conversion_service_v1/payment.hpp"
#include "arken_file_conversion_v1/api.hpp"
namespace arken::file_conversion::service::v1::payment {
namespace {
bool OutputValid(const Output &output, const Quote &quote) noexcept {
  namespace C = arken::file_conversion::v1;
  return output.bytes > 0 && output.bytes <= C::maximumOutputBytes &&
         output.width > 0 && output.height > 0 &&
         output.width <= C::maximumDimension &&
         output.height <= C::maximumDimension &&
         std::uint64_t(output.width) * output.height <= C::maximumPixels &&
         output.release == quote.release;
}
} // namespace
bool ValidOperationRecord(const Operation &op) noexcept {
  return policy::ValidOperationRecord(op, OutputValid);
}
Decision Advance(const Operation &op, const Quote &quote,
                 const std::optional<NonceClaim> &claim,
                 const Event &event) noexcept {
  return policy::Advance(op, quote, claim, event, OutputValid);
}
std::uint64_t AttemptDeadline(const Operation &op, std::uint64_t now) noexcept {
  return policy::AttemptDeadline(op, now);
}
Recovery Recover(const Operation &op, const std::optional<NonceClaim> &claim,
                 const Event &event, const Attempt &attempt) noexcept {
  return policy::Recover(op, claim, event, attempt, OutputValid);
}
} // namespace arken::file_conversion::service::v1::payment
