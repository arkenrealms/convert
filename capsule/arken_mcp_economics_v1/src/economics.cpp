#include "arken_mcp_economics_v1/api.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace arken::mcp::economics::v1 {
namespace {
constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
constexpr Amount workerRequestNanoUsd=300,workerCpuMillisecondNanoUsd=20;
bool identity(std::string_view text) {
 return !text.empty()&&text.size()<=128&&text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:/-")==text.npos;
}
// Quotient/remainder arithmetic avoids a large intermediate and is portable C++20.
Result<Amount> scaled(Amount value,Amount numerator,Amount denominator) {
 if(!denominator)return {Error::invalid,{}};
 const auto whole=multiply(value/denominator,numerator);
 const auto remainder=multiply(value%denominator,numerator);
 if(!whole||!remainder)return {Error::overflow,{}};
 return add(whole.value,remainder.value/denominator+(remainder.value%denominator!=0));
}
}
std::string_view errorCode(Error error) {
 switch(error) {
 case Error::none:return "ok";case Error::invalid:return "economics_invalid";
 case Error::overflow:return "economics_overflow";case Error::staleHistory:return "history_stale";
 case Error::insufficientHistory:return "history_unqualified";case Error::clockReversed:return "budget_clock_reversed";
 case Error::exhausted:return "attempt_budget_exhausted";
 case Error::rateReview:return "history_rate_review_required";
 }return "economics_invalid";
}
Result<Amount> add(Amount a,Amount b) { return a>maximum-b?Result<Amount>{Error::overflow,{}}:Result<Amount>{Error::none,a+b}; }
Result<Amount> multiply(Amount a,Amount b) { return b&&a>maximum/b?Result<Amount>{Error::overflow,{}}:Result<Amount>{Error::none,a*b}; }
Result<Amount> periodExpense(Amount amount,std::uint64_t duration,std::uint64_t begin,std::uint64_t end) {
 if(!duration||duration>366*86400||begin>=end||end>duration)return {Error::invalid,{}};
 const auto first=scaled(amount,begin,duration),last=scaled(amount,end,duration);
 if(!first||!last)return {Error::overflow,{}};
 return {Error::none,last.value-first.value};
}
Result<Estimate> estimate(const Usage& usage) {
 struct Meter {const char* name;std::optional<std::uint64_t> value;Amount numerator,denominator;};
 const std::array meters{
  Meter{"workerRequests",usage.workerRequests,workerRequestNanoUsd,1},
  Meter{"workerCpuMicroseconds",usage.workerCpuMicroseconds,workerCpuMillisecondNanoUsd,1000},
  Meter{"durableRequests",usage.durableRequests,150,1},
  Meter{"durableDurationNanoGbSeconds",usage.durableDurationNanoGbSeconds,12500,usd},
  Meter{"sqliteRowsRead",usage.sqliteRowsRead,1,1},
  Meter{"sqliteRowsWritten",usage.sqliteRowsWritten,1000,1},
  Meter{"logEvents",usage.logEvents,600,1},
  Meter{"facilitatorTransactions",usage.facilitatorTransactions,1'000'000,1}};
 Estimate result;
 for(const auto& meter:meters) {
  if(!meter.value){result.missing.emplace_back(meter.name);continue;}
  const auto cost=scaled(*meter.value,meter.numerator,meter.denominator);
  if(!cost)return {cost.error,{}};
  const auto sum=add(result.observedNanoUsd,cost.value);if(!sum)return {sum.error,{}};
  result.observedNanoUsd=sum.value;result.lines.push_back({meter.name,cost.value});
 }
 return {Error::none,std::move(result)};
}
std::string usdText(Amount value) {
 auto text=std::to_string(value);if(text.size()<10)text.insert(0,10-text.size(),'0');
 text.insert(text.size()-9,".");return text;
}
Result<Amount> sqliteStorageDay(std::uint64_t peakBytes) {return scaled(peakBytes,1,150);}
Result<Amount> r2OperationScenario(std::uint64_t classA,std::uint64_t classB,R2StorageClass storage) {
 if(storage!=R2StorageClass::standard&&storage!=R2StorageClass::infrequentAccess)return {Error::invalid,{}};
 const auto a=multiply(classA,storage==R2StorageClass::standard?4500:9000);
 const auto b=multiply(classB,storage==R2StorageClass::standard?360:900);
 if(!a||!b)return {Error::overflow,{}};
 return add(a.value,b.value);
}
std::string differenceText(Amount income,Amount cost) {
 return income>=cost?usdText(income-cost):"-"+usdText(cost-income);
}
Result<R2StorageScenario> r2StandardStorageScenario(std::uint64_t roundedConsumedGbMonths) {
 const auto units=roundedConsumedGbMonths>r2StandardStorageAllowanceGbMonths?
  roundedConsumedGbMonths-r2StandardStorageAllowanceGbMonths:0;
 const auto cost=multiply(units,15'000'000);
 if(!cost)return {cost.error,{}};
 return {Error::none,{units,cost.value}};
}
Result<WorkersScenario> workersStandardScenario(WorkersMeter meter,std::uint64_t consumed) {
 std::uint64_t allowance;Amount rate;
 switch(meter) {
 case WorkersMeter::requests:allowance=workersRequestAllowance;rate=workerRequestNanoUsd;break;
 case WorkersMeter::cpuMilliseconds:allowance=workersCpuMillisecondAllowance;rate=workerCpuMillisecondNanoUsd;break;
 default:return {Error::invalid,{}};
 }
 const auto units=consumed>allowance?consumed-allowance:0;
 const auto cost=multiply(units,rate);if(!cost)return {cost.error,{}};
 return {Error::none,{units,cost.value}};
}
Result<DurableComputeScenario> durableComputeScenario(DurableComputeMeter meter,std::uint64_t roundedConsumed) {
 std::uint64_t allowance;Amount blockCost;
 switch(meter) {
 case DurableComputeMeter::requests:allowance=durableRequestAllowance;blockCost=150'000'000;break;
 case DurableComputeMeter::durationGbSeconds:allowance=durableDurationAllowanceGbSeconds;blockCost=12'500'000'000;break;
 default:return {Error::invalid,{}};
 }
 constexpr std::uint64_t block=1'000'000;
 const auto excess=roundedConsumed>allowance?roundedConsumed-allowance:0;
 const auto blocks=excess/block+(excess%block!=0);
 const auto quantity=multiply(blocks,block),cost=multiply(blocks,blockCost);
 if(!quantity||!cost)return {Error::overflow,{}};
 return {Error::none,{quantity.value,cost.value}};
}
Result<Reservation> reserve(const BudgetPolicy& policy,const std::optional<BudgetState>& prior,std::uint64_t now) {
 if(!identity(policy.scope)||!policy.maximumAttempts||!policy.maximumReservedNanoUsd||!policy.attemptReserveNanoUsd||
    policy.attemptReserveNanoUsd>policy.maximumReservedNanoUsd||!now||now>4102444800)
  return {Error::invalid,{}};
 BudgetState state{policy.scope,now/86400,0,0,now,0};
 if(prior) {
  if(prior->scope!=policy.scope||!prior->updatedAt||prior->day!=prior->updatedAt/86400||
     !prior->attempts||!prior->revision||!prior->reservedNanoUsd||prior->revision<prior->attempts)
   return {Error::invalid,{}};
  if(prior->updatedAt>now)return {Error::clockReversed,{}};
  if(prior->day==state.day)state=*prior;
  else state.revision=prior->revision;
 }
 Reservation result{state,0,86400-now%86400,0};
 if(state.attempts>=policy.maximumAttempts||state.reservedNanoUsd>policy.maximumReservedNanoUsd||
    policy.attemptReserveNanoUsd>policy.maximumReservedNanoUsd-state.reservedNanoUsd)
  return {Error::exhausted,result};
 if(state.revision==maximum)return {Error::overflow,{}};
 ++state.attempts;++state.revision;state.updatedAt=now;state.reservedNanoUsd+=policy.attemptReserveNanoUsd;
 result.state=state;result.remainingAttempts=policy.maximumAttempts-state.attempts;
 result.remainingNanoUsd=policy.maximumReservedNanoUsd-state.reservedNanoUsd;
 return {Error::none,result};
}
}
