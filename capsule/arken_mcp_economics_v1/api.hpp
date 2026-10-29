#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arken::mcp::economics::v1 {
using Amount = std::uint64_t; // USD nanounits, not USDC atomic units.
inline constexpr Amount usd = 1'000'000'000;
inline constexpr Amount usdcAtomic = 1'000;
inline constexpr std::uint64_t minimumPriceAtomic = 10'000;
inline constexpr std::string_view rateCard = "cloudflare-paid-overage-2026-10-16";
inline constexpr std::uint64_t rateValidThrough = 1796083200; // 2026-12-01 UTC; review changed log pricing.
enum class Error { none, invalid, overflow, staleHistory, insufficientHistory, clockReversed, exhausted, rateReview };
std::string_view errorCode(Error);
template<class T> struct Result { Error error=Error::none; T value{}; explicit operator bool() const { return error==Error::none; } };
struct Usage {
 std::optional<std::uint64_t> workerRequests, workerCpuMicroseconds;
 std::optional<std::uint64_t> durableRequests, durableDurationNanoGbSeconds;
 std::optional<std::uint64_t> sqliteRowsRead, sqliteRowsWritten, logEvents;
 std::optional<std::uint64_t> facilitatorTransactions;
};
struct CostLine { std::string meter; Amount nanoUsd{}; };
struct Estimate {
 Amount observedNanoUsd{};
 std::vector<CostLine> lines;
 std::vector<std::string> missing;
 // Storage, R2, fixed subscriptions and external providers are not in Usage.
 // Even complete meter coverage is not a final invoice or fully loaded cost.
};
Result<Estimate> estimate(const Usage&);
// One completed day's observed SQL storage peak, at $0.20 / decimal GB-month
// with 30 daily peaks per month, before allowances. Not an invoice or bound.
Result<Amount> sqliteStorageDay(std::uint64_t peakBytes);
// Conditional marginal operation tariff only: class, bill rounding, allowances,
// failed requests, storage and retrieval remain the observation owner's duties.
enum class R2StorageClass { standard, infrequentAccess };
inline constexpr std::string_view r2RateCard = "cloudflare-r2-operations-2026-10-17";
Result<Amount> r2OperationScenario(std::uint64_t classA,std::uint64_t classB,R2StorageClass);
inline constexpr std::string_view r2StorageRateCard = "cloudflare-r2-standard-storage-2026-10-26";
inline constexpr std::uint64_t r2StandardStorageAllowanceGbMonths = 10;
struct R2StorageScenario { std::uint64_t billableGbMonths{}; Amount nanoUsd{}; };
// Conditional account-period Standard tariff, not historical class or invoice
// proof. Input is ceil(exact cumulative consumed GB-months), allowance once.
Result<R2StorageScenario> r2StandardStorageScenario(std::uint64_t roundedConsumedGbMonths);
enum class WorkersMeter { requests, cpuMilliseconds };
inline constexpr std::string_view workersRateCard = "cloudflare-workers-standard-2026-10-26";
inline constexpr std::uint64_t workersRequestAllowance = 10'000'000;
inline constexpr std::uint64_t workersCpuMillisecondAllowance = 30'000'000;
struct WorkersScenario { std::uint64_t billableQuantity{}; Amount nanoUsd{}; };
// Conditional account-period tariff, allowance applied once. The caller owns
// complete integral consumption, meter selection and historical applicability.
Result<WorkersScenario> workersStandardScenario(WorkersMeter,std::uint64_t consumed);
enum class DurableComputeMeter { requests, durationGbSeconds };
inline constexpr std::string_view durableComputeRateCard = "cloudflare-durable-compute-2026-10-26";
inline constexpr std::uint64_t durableRequestAllowance = 1'000'000;
inline constexpr std::uint64_t durableDurationAllowanceGbSeconds = 400'000;
struct DurableComputeScenario { std::uint64_t billableQuantity{}; Amount nanoUsd{}; };
// Conditional account-period Paid compute tariff. Input is ceil(exact consumed
// base units); excess rounds UP to million-unit blocks after one allowance.
// Not a marginal estimate, invoice, historical tariff or per-purchase bound.
Result<DurableComputeScenario> durableComputeScenario(DurableComputeMeter,std::uint64_t roundedConsumed);
// SQL allowance checks only. Positive-overage billing/rounding is not inferred
// from the compute tariff or from marginal per-row operational estimates.
inline constexpr std::string_view durableSqlAllowanceCard = "cloudflare-durable-sql-allowances-2026-10-26";
inline constexpr std::uint64_t durableSqlReadAllowance = 25'000'000'000;
inline constexpr std::uint64_t durableSqlWriteAllowance = 50'000'000;
inline constexpr std::uint64_t durableSqlStorageAllowanceGbMonths = 5;
// Uniform-period scenario: difference of cumulative rounded nanodollars.
// Adjacent slices conserve the period amount; not a per-slice upper bound.
Result<Amount> periodExpense(Amount,std::uint64_t periodSeconds,std::uint64_t beginSeconds,std::uint64_t endSeconds);
Result<Amount> add(Amount,Amount);
Result<Amount> multiply(Amount,Amount);
std::string usdText(Amount);
std::string differenceText(Amount income,Amount cost);

struct PricePolicy {
 std::uint64_t currentPriceAtomic=minimumPriceAtomic;
 std::uint32_t targetMarginBasisPoints=8000;
 Amount overheadNanoUsd{};
 std::uint64_t minimumSamples=30, maximumHistoryAgeSeconds=7*86400;
};
enum class Coverage { unavailable, partial, complete };
enum class CostBasis { unavailable, observed, upperBound };
enum class CostComponent {
 execution, durableObjects, storage, routingAndAdmission, payment, logs, fixedAndExternal, nonRevenueTraffic, count
};
inline constexpr std::size_t costComponentCount=static_cast<std::size_t>(CostComponent::count);
std::string_view costComponentName(CostComponent);
struct CostEvidence {
 CostBasis basis=CostBasis::unavailable;
 Amount nanoUsd{};
 std::string evidenceSha256; // Reviewed source/limit/retention evidence, not proof by itself.
};
struct History {
 std::string service, command, release, workerVersion, inputClass;
 std::uint64_t windowStart{}, windowEnd{}, settledPurchases{};
 std::string rateCardIdentity, evidenceSha256;
 Coverage sampling=Coverage::unavailable, retention=Coverage::unavailable, purchaseAttribution=Coverage::unavailable;
 // Each direct component bounds ONE purchase. nonRevenueTraffic instead bounds
 // the entire same-window cost pool, disjoint from those direct components.
 // The evidence bundle owns scope, deduplication and operator-test exclusion.
 std::array<CostEvidence,costComponentCount> costs{};
 std::optional<Amount> observedMaximumWorkerNanoUsd;
};
struct PriceRecommendation {
 std::optional<std::uint64_t> priceAtomic;
 std::optional<Amount> costBasisNanoUsd, nonRevenueAllocationNanoUsd;
 std::vector<std::string> missingCoverage;
};
// A recommendation only. Quote owners freeze the chosen amount in new offers.
// Never lowers current price or bypasses the one-cent minimum. Complete history
// is necessary, not a guarantee against future changes or hostile unpaid traffic.
Result<PriceRecommendation> recommendPrice(const PricePolicy&,const History&,std::uint64_t now);

struct BudgetPolicy {
 std::string scope; // Stable across deployments; policy changes cannot reset consumption.
 std::uint64_t maximumAttempts{};
 Amount maximumReservedNanoUsd{}, attemptReserveNanoUsd{};
};
struct BudgetState {
 std::string scope;
 std::uint64_t day{}, attempts{}, revision{}, updatedAt{};
 Amount reservedNanoUsd{};
 bool operator==(const BudgetState&) const = default;
};
struct Reservation {
 BudgetState state;
 std::uint64_t remainingAttempts{}, retryAfterSeconds{};
 Amount remainingNanoUsd{};
};
// Storage owner must commit returned state atomically before any expensive effect.
// Each transport admission consumes an attempt regardless of its later outcome.
// There is intentionally no success/refund/reset API. Invalid prior state fails closed.
Result<Reservation> reserve(const BudgetPolicy&,const std::optional<BudgetState>&,std::uint64_t now);
}
