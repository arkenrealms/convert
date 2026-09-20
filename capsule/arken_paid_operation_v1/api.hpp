#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
namespace arken::paid_operation::v1 {
using Digest = std::array<std::uint8_t, 32>;
using Address = std::array<std::uint8_t, 20>;
struct Scope {
  std::uint32_t chain{};
  Address asset{}, payer{};
  bool operator==(const Scope &) const = default;
};
struct Quote {
  Digest operation{}, tenant{}, input{}, options{}, release{}, priceVersion{};
  Scope scope{};
  Address recipient{};
  std::uint64_t amount{},
      expiresAt{}; // Atomic units and injected Unix seconds.
  bool operator==(const Quote &) const = default;
};
struct Authorization {
  Digest payload{}, nonce{};
  std::uint64_t validAfter{}, validBefore{};
  bool operator==(const Authorization &) const = default;
};
// Generic content commitment; domain adapters may supply richer result types.
struct ResultCommitment {
  Digest digest{}, release{};
  std::uint32_t bytes{};
  bool operator==(const ResultCommitment &) const = default;
};
struct Transfer {
  Digest transaction{}, nonce{};
  Scope scope{};
  Address recipient{};
  std::uint64_t amount{};
  bool operator==(const Transfer &) const = default;
};
struct NonceClaim {
  Scope scope{};
  Digest nonce{}, operation{}, payload{};
  bool operator==(const NonceClaim &) const = default;
};
// Values are persisted by existing product journals. Names are domain-neutral.
enum class Stage : std::uint8_t {
  quoted = 0,
  verifying = 1,
  authorized = 2,
  computing = 3,
  outputReady = 4,
  settling = 5,
  settlementUnknown = 6,
  settled = 7,
  failedNoCharge = 8,
  settlementFailed = 9
};
enum class Failure : std::uint8_t {
  none = 0,
  verificationRejected = 1,
  expired = 2,
  executionRejected = 3,
  storageFailed = 4,
  releaseUnavailable = 5,
  settlementRejected = 6
};
template <class Result> struct OperationRecord {
  // Persist large domain results separately; this is a bounded commitment.
  static_assert(std::is_nothrow_copy_constructible_v<Result> &&
                std::is_nothrow_copy_assignable_v<Result>);
  Quote quote{};
  std::uint64_t revision{};
  Stage stage = Stage::quoted;
  std::optional<Authorization> authorization{};
  std::optional<Result> output{};
  std::optional<Transfer> transfer{};
  Failure failure = Failure::none;
  bool operator==(const OperationRecord &) const = default;
};
enum class EventKind : std::uint8_t {
  request = 0,
  retryVerification = 1,
  verificationPassed = 2,
  verificationRejected = 3,
  beginExecution = 4,
  outputCommitted = 5,
  failBeforeSettlement = 6,
  beginSettlement = 7,
  settlementSucceeded = 8,
  settlementRejected = 9,
  settlementUncertain = 10,
  reconciledSucceeded = 11,
  reconciledFailed = 12,
  inspect = 13,
  retryExecution = 14
};
// Events reporting verification or transfer success are trusted adapter inputs,
// never public client assertions. This policy does not verify signatures.
template <class Result> struct EventRecord {
  EventKind kind = EventKind::request;
  std::uint64_t expectedRevision{}, now{};
  std::optional<Authorization> authorization{};
  std::optional<Result> output{};
  std::optional<Transfer> transfer{};
  Address verifiedPayer{};
  Digest executingRelease{};
  Failure failure = Failure::none;
};
enum class Error : std::uint8_t {
  none = 0,
  invalidRecord = 1,
  quoteConflict = 2,
  staleRevision = 3,
  invalidEvent = 4,
  expired = 5,
  authorizationConflict = 6,
  nonceConflict = 7,
  nonceClaimMissing = 8,
  payerMismatch = 9,
  outputInvalid = 10,
  releaseMismatch = 11,
  transferMismatch = 12,
  revisionOverflow = 13
};
enum class Action : std::uint8_t {
  none = 0,
  verify = 1,
  execute = 2,
  settle = 3,
  wait = 4,
  reconcile = 5,
  receipt = 6,
  failure = 7
};
template <class Result> struct DecisionRecord {
  Error error = Error::none;
  OperationRecord<Result> operation;
  std::optional<NonceClaim> nonceClaim;
  Action action = Action::none;
  bool changed = false;
};
// The storage owner atomically compare-and-swaps revision, operation and nonce
// claim, and confirms persistence BEFORE dispatching the returned action.
struct Attempt {
  bool dispatched = false;
  std::optional<std::uint64_t> deadline;
};
template <class Result> struct RecoveryRecord {
  DecisionRecord<Result> decision;
  std::optional<std::uint64_t> observationDeadline;
};
} // namespace arken::paid_operation::v1
#include "state.hpp"
