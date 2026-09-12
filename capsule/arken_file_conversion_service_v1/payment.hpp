#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace arken::file_conversion::service::v1::payment {
using Digest=std::array<std::uint8_t,32>;
using Address=std::array<std::uint8_t,20>;
struct Scope {
  std::uint32_t chain{};
  Address asset{},payer{};
  bool operator==(const Scope&) const = default;
};
struct Quote {
  Digest operation{},tenant{},input{},options{},release{},priceVersion{};
  Scope scope{};
  Address recipient{};
  std::uint64_t amount{},expiresAt{}; // Atomic units and injected Unix seconds.
  bool operator==(const Quote&) const = default;
};
struct Authorization {
  Digest payload{},nonce{};
  std::uint64_t validAfter{},validBefore{};
  bool operator==(const Authorization&) const = default;
};
struct Output {
  Digest digest{},release{};
  std::uint32_t bytes{},width{},height{};
  bool operator==(const Output&) const = default;
};
struct Transfer {
  Digest transaction{},nonce{};
  Scope scope{};
  Address recipient{};
  std::uint64_t amount{};
  bool operator==(const Transfer&) const = default;
};
struct NonceClaim {
  Scope scope{};
  Digest nonce{},operation{},payload{};
  bool operator==(const NonceClaim&) const = default;
};
enum class Stage : std::uint8_t { quoted,verifying,authorized,computing,outputReady,settling,settlementUnknown,settled,failedNoCharge,settlementFailed };
enum class Failure : std::uint8_t { none,verificationRejected,expired,conversionRejected,storageFailed,releaseUnavailable,settlementRejected };
struct Operation {
  Quote quote{};
  std::uint64_t revision{};
  Stage stage=Stage::quoted;
  std::optional<Authorization> authorization{};
  std::optional<Output> output{};
  std::optional<Transfer> transfer{};
  Failure failure=Failure::none;
  bool operator==(const Operation&) const = default;
};
enum class EventKind : std::uint8_t {
  request,retryVerification,verificationPassed,verificationRejected,
  beginConversion,outputCommitted,failBeforeSettlement,beginSettlement,
  settlementSucceeded,settlementRejected,settlementUncertain,
  reconciledSucceeded,reconciledFailed,inspect
};
// Events reporting verification or transfer success are trusted adapter inputs,
// never public client assertions. This policy does not verify signatures.
struct Event {
  EventKind kind=EventKind::request;
  std::uint64_t expectedRevision{},now{};
  std::optional<Authorization> authorization{};
  std::optional<Output> output{};
  std::optional<Transfer> transfer{};
  Address verifiedPayer{};
  Digest executingRelease{};
  Failure failure=Failure::none;
};
enum class Error : std::uint8_t { none,invalidRecord,quoteConflict,staleRevision,invalidEvent,expired,authorizationConflict,nonceConflict,nonceClaimMissing,payerMismatch,outputInvalid,releaseMismatch,transferMismatch,revisionOverflow };
enum class Action : std::uint8_t { none,verify,convert,settle,wait,reconcile,receipt,failure };
struct Decision {
  Error error=Error::none;
  Operation operation;
  std::optional<NonceClaim> nonceClaim;
  Action action=Action::none;
  bool changed=false;
};
// The storage owner atomically compare-and-swaps revision, operation and nonce
// claim, and confirms persistence BEFORE dispatching the returned action.
[[nodiscard]] Decision Advance(const Operation&,const Quote& observedQuote,
  const std::optional<NonceClaim>& existingClaim,const Event&) noexcept;
}
