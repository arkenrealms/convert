#pragma once
#include "api.hpp"
#include <algorithm>
#include <limits>
// Deterministic policy only. Persist the decision before executing its action.
namespace arken::paid_operation::v1 {
namespace detail {
inline bool HasClaim(Stage stage) {
  return stage == Stage::authorized || stage == Stage::computing ||
         stage == Stage::outputReady || stage == Stage::settling ||
         stage == Stage::settlementUnknown || stage == Stage::settled ||
         stage == Stage::settlementFailed;
}
inline bool HasOutput(Stage stage) {
  return stage == Stage::outputReady || stage == Stage::settling ||
         stage == Stage::settlementUnknown || stage == Stage::settled ||
         stage == Stage::settlementFailed;
}
template <std::size_t N>
bool Nonzero(const std::array<std::uint8_t, N> &value) {
  return std::ranges::any_of(value, [](auto b) { return b != 0; });
}
template <class Result>
bool TransferValid(const Transfer &t, const OperationRecord<Result> &op) {
  return op.authorization && Nonzero(t.transaction) &&
         t.scope == op.quote.scope && t.recipient == op.quote.recipient &&
         t.amount == op.quote.amount && t.nonce == op.authorization->nonce;
}
template <class Result>
bool Window(const OperationRecord<Result> &op, std::uint64_t now) {
  return op.authorization && now < op.quote.expiresAt &&
         op.authorization->validAfter < now &&
         now < op.authorization->validBefore &&
         op.authorization->validBefore <= op.quote.expiresAt;
}
template <class Result, class Validator>
bool RecordValid(const OperationRecord<Result> &op, Validator valid) {
  static_assert(std::is_nothrow_invocable_r_v<bool, Validator, const Result &,
                                              const Quote &>);
  if (op.quote.amount == 0 || op.quote.scope.chain == 0 ||
      op.quote.expiresAt == 0 || op.stage > Stage::settlementFailed ||
      op.failure > Failure::settlementRejected)
    return false;
  if (!Nonzero(op.quote.scope.asset) || !Nonzero(op.quote.scope.payer) ||
      !Nonzero(op.quote.recipient))
    return false;
  if (op.authorization &&
      (op.authorization->validBefore <= op.authorization->validAfter ||
       op.authorization->validBefore > op.quote.expiresAt))
    return false;
  if (op.stage == Stage::quoted &&
      (op.authorization || op.output || op.transfer))
    return false;
  if ((HasClaim(op.stage) || op.stage == Stage::verifying) && !op.authorization)
    return false;
  if (HasOutput(op.stage) && !op.output)
    return false;
  if (!HasOutput(op.stage) && op.stage != Stage::failedNoCharge && op.output)
    return false;
  if (op.output && !valid(*op.output, op.quote))
    return false;
  if (op.stage == Stage::settled &&
      (!op.transfer || !TransferValid(*op.transfer, op)))
    return false;
  if (op.stage != Stage::settled && op.transfer)
    return false;
  if ((op.stage == Stage::failedNoCharge ||
       op.stage == Stage::settlementFailed) != (op.failure != Failure::none))
    return false;
  if (op.stage == Stage::settlementFailed &&
      op.failure != Failure::settlementRejected)
    return false;
  if (op.stage == Stage::failedNoCharge &&
      op.failure == Failure::settlementRejected)
    return false;
  return true;
}
} // namespace detail
template <class Result, class Validator>
bool ValidOperationRecord(const OperationRecord<Result> &op,
                          Validator valid) noexcept {
  return detail::RecordValid(op, valid);
}
template <class Result, class Validator>
DecisionRecord<Result>
Advance(const OperationRecord<Result> &original, const Quote &observed,
        const std::optional<NonceClaim> &claim,
        const EventRecord<Result> &event, Validator valid) noexcept {
  using namespace detail;
  DecisionRecord<Result> result{.operation = original, .nonceClaim = claim};
  const auto reject = [&](Error error) {
    result.error = error;
    result.action = Action::none;
    result.changed = false;
    result.operation = original;
    result.nonceClaim = claim;
    return result;
  };
  if (!RecordValid(original, valid))
    return reject(Error::invalidRecord);
  if (original.quote != observed)
    return reject(Error::quoteConflict);
  if (original.revision != event.expectedRevision)
    return reject(Error::staleRevision);
  if (event.authorization && original.authorization &&
      event.authorization != original.authorization)
    return reject(Error::authorizationConflict);
  const auto expectedClaim = [&] {
    return NonceClaim{original.quote.scope, original.authorization->nonce,
                      original.quote.operation,
                      original.authorization->payload};
  };
  if (HasClaim(original.stage)) {
    if (!claim)
      return reject(Error::nonceClaimMissing);
    if (*claim != expectedClaim())
      return reject(Error::nonceConflict);
  }
  const auto change = [&](Stage stage, Action action) {
    if (original.revision == std::numeric_limits<std::uint64_t>::max())
      return reject(Error::revisionOverflow);
    result.operation.stage = stage;
    result.operation.revision = original.revision + 1;
    result.action = action;
    result.changed = true;
    return result;
  };
  switch (event.kind) {
  case EventKind::inspect:
    return result;
  case EventKind::request:
    if (original.stage == Stage::settled) {
      result.action = Action::receipt;
      return result;
    }
    if (original.stage == Stage::failedNoCharge ||
        original.stage == Stage::settlementFailed) {
      result.action = Action::failure;
      return result;
    }
    if (original.stage == Stage::settlementUnknown) {
      result.action = Action::reconcile;
      return result;
    }
    // Resume safe pre-effect boundaries here, not in a JS state machine.
    if (original.stage == Stage::authorized ||
        original.stage == Stage::outputReady) {
      auto resumed = event;
      resumed.kind = original.stage == Stage::authorized
                         ? EventKind::beginExecution
                         : EventKind::beginSettlement;
      return Advance(original, observed, claim, resumed, valid);
    }
    if (original.stage != Stage::quoted) {
      result.action = Action::wait;
      return result;
    }
    if (!event.authorization)
      return reject(Error::invalidEvent);
    result.operation.authorization = event.authorization;
    if (!Window(result.operation, event.now))
      return reject(Error::expired);
    return change(Stage::verifying, Action::verify);
  case EventKind::retryVerification:
    if (original.stage != Stage::verifying)
      return reject(Error::invalidEvent);
    if (!Window(original, event.now))
      return reject(Error::expired);
    return change(Stage::verifying, Action::verify);
  case EventKind::verificationPassed:
    if (original.stage != Stage::verifying)
      return reject(Error::invalidEvent);
    if (event.verifiedPayer != original.quote.scope.payer)
      return reject(Error::payerMismatch);
    if (!Window(original, event.now))
      return reject(Error::expired);
    if (claim && *claim != expectedClaim())
      return reject(Error::nonceConflict);
    result.nonceClaim = expectedClaim();
    return change(Stage::authorized, Action::none);
  case EventKind::verificationRejected:
    if (original.stage != Stage::verifying)
      return reject(Error::invalidEvent);
    result.operation.failure = Failure::verificationRejected;
    return change(Stage::failedNoCharge, Action::failure);
  case EventKind::beginExecution:
  case EventKind::retryExecution:
    if (original.stage != (event.kind == EventKind::beginExecution
                               ? Stage::authorized
                               : Stage::computing))
      return reject(Error::invalidEvent);
    if (event.executingRelease != original.quote.release)
      return reject(Error::releaseMismatch);
    if (!Window(original, event.now))
      return reject(Error::expired);
    return change(Stage::computing, Action::execute);
  case EventKind::outputCommitted:
    if (original.stage != Stage::computing || !event.output)
      return reject(Error::invalidEvent);
    if (!valid(*event.output, original.quote))
      return reject(Error::outputInvalid);
    result.operation.output = event.output;
    return change(Stage::outputReady, Action::none);
  case EventKind::failBeforeSettlement:
    if (original.stage > Stage::outputReady || event.failure == Failure::none ||
        event.failure >= Failure::settlementRejected)
      return reject(Error::invalidEvent);
    result.operation.failure = event.failure;
    return change(Stage::failedNoCharge, Action::failure);
  case EventKind::beginSettlement:
    if (original.stage != Stage::outputReady)
      return reject(Error::invalidEvent);
    if (!Window(original, event.now))
      return reject(Error::expired);
    return change(Stage::settling, Action::settle);
  case EventKind::settlementUncertain:
    if (original.stage != Stage::settling)
      return reject(Error::invalidEvent);
    return change(Stage::settlementUnknown, Action::reconcile);
  case EventKind::settlementSucceeded:
  case EventKind::reconciledSucceeded:
    if (event.kind == EventKind::settlementSucceeded &&
        original.stage != Stage::settling)
      return reject(Error::invalidEvent);
    if (event.kind == EventKind::reconciledSucceeded &&
        original.stage != Stage::settlementUnknown)
      return reject(Error::invalidEvent);
    if (!event.transfer || !TransferValid(*event.transfer, original))
      return reject(Error::transferMismatch);
    result.operation.transfer = event.transfer;
    return change(Stage::settled, Action::receipt);
  case EventKind::settlementRejected:
  case EventKind::reconciledFailed:
    if (event.kind == EventKind::settlementRejected &&
        original.stage != Stage::settling)
      return reject(Error::invalidEvent);
    if (event.kind == EventKind::reconciledFailed &&
        original.stage != Stage::settlementUnknown)
      return reject(Error::invalidEvent);
    result.operation.failure = Failure::settlementRejected;
    return change(Stage::settlementFailed, Action::failure);
  }
  return reject(Error::invalidEvent);
}
template <class Result>
std::uint64_t AttemptDeadline(const OperationRecord<Result> &op,
                              std::uint64_t now) noexcept {
  if (!op.authorization ||
      (op.stage != Stage::verifying && op.stage != Stage::computing))
    return 0;
  const std::uint64_t budget = op.stage == Stage::verifying ? 35 : 30;
  const auto ceiling = std::numeric_limits<std::uint64_t>::max();
  return std::min(now > ceiling - budget ? ceiling : now + budget,
                  op.authorization->validBefore);
}
template <class Result, class Validator>
RecoveryRecord<Result> Recover(const OperationRecord<Result> &op,
                               const std::optional<NonceClaim> &claim,
                               const EventRecord<Result> &observation,
                               const Attempt &attempt,
                               Validator valid) noexcept {
  auto event = observation;
  event.kind = EventKind::inspect;
  RecoveryRecord<Result> result{Advance(op, op.quote, claim, event, valid),
                                std::nullopt};
  if (result.decision.error != Error::none)
    return result;
  const auto reject = [&] {
    result.decision.error = Error::invalidRecord;
    return result;
  };
  if (attempt.deadline && (!attempt.dispatched || !op.authorization ||
                           *attempt.deadline > op.authorization->validBefore ||
                           *attempt.deadline <= op.authorization->validAfter))
    return reject();
  if (op.stage == Stage::settled) {
    result.decision.action = Action::receipt;
    return result;
  }
  if (op.stage == Stage::failedNoCharge ||
      op.stage == Stage::settlementFailed) {
    result.decision.action = Action::failure;
    return result;
  }
  if (op.stage == Stage::settlementUnknown) {
    result.decision.action = Action::reconcile;
    return result;
  }
  result.decision.action = Action::wait;
  // Includes outputReady: recovery itself cannot admit any monetary effect.
  if (op.stage >= Stage::settling || !op.authorization)
    return result;
  const auto fail = [&](Failure failure) {
    event.kind = EventKind::failBeforeSettlement;
    event.failure = failure;
    result.decision = Advance(op, op.quote, claim, event, valid);
    return result;
  };
  if (event.now >= op.authorization->validBefore ||
      event.now >= op.quote.expiresAt)
    return fail(Failure::expired);
  if (event.now <= op.authorization->validAfter)
    return result;
  if ((op.stage == Stage::authorized || op.stage == Stage::computing) &&
      event.executingRelease != op.quote.release)
    return fail(Failure::releaseUnavailable);
  if (op.stage != Stage::verifying && op.stage != Stage::computing)
    return result;
  if (attempt.dispatched) {
    // Old records have no observed start time. Persist one conservative budget.
    if (!attempt.deadline) {
      result.observationDeadline = AttemptDeadline(op, event.now);
      return result;
    }
    if (event.now < *attempt.deadline)
      return result;
  }
  event.kind = op.stage == Stage::verifying ? EventKind::retryVerification
                                            : EventKind::retryExecution;
  result.decision = Advance(op, op.quote, claim, event, valid);
  return result;
}
} // namespace arken::paid_operation::v1
