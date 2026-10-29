#include "arken_mcp_economics_v1/api.hpp"
#include <algorithm>

namespace arken::mcp::economics::v1 {
namespace {
constexpr std::array<std::string_view,costComponentCount> names{
 "execution", "durableObjects", "storage", "routingAndAdmission", "payment", "logs", "fixedAndExternal", "nonRevenueTraffic"};
bool identity(std::string_view s) {
 return !s.empty()&&s.size()<=160&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:/-")==s.npos;
}
bool digest(std::string_view s) {return s.size()==64&&s.find_first_not_of("0123456789abcdef")==s.npos;}
bool valid(Coverage c) {return c==Coverage::unavailable||c==Coverage::partial||c==Coverage::complete;}
bool valid(CostBasis b) {return b==CostBasis::unavailable||b==CostBasis::observed||b==CostBasis::upperBound;}
Amount ceiling(Amount n,Amount d) {return n/d+(n%d!=0);}
}
std::string_view costComponentName(CostComponent component) {
 const auto index=static_cast<std::size_t>(component);
 return index<names.size()?names[index]:"invalid";
}
Result<PriceRecommendation> recommendPrice(const PricePolicy& policy,const History& history,std::uint64_t now) {
 if(policy.currentPriceAtomic<minimumPriceAtomic||policy.targetMarginBasisPoints>=10000||
    !policy.minimumSamples||!policy.maximumHistoryAgeSeconds||!now||
    !identity(history.service)||!identity(history.command)||!digest(history.release)||
    !identity(history.workerVersion)||!identity(history.inputClass)||
    !history.windowStart||history.windowEnd<=history.windowStart||history.windowEnd>now||
    !valid(history.sampling)||!valid(history.retention)||!valid(history.purchaseAttribution))
  return {Error::invalid,{}};
 if(now>=rateValidThrough||history.rateCardIdentity!=rateCard)return {Error::rateReview,{}};
 if(now-history.windowEnd>policy.maximumHistoryAgeSeconds)return {Error::staleHistory,{}};
 PriceRecommendation result;
 if(!digest(history.evidenceSha256))result.missingCoverage.emplace_back("windowEvidence");
 if(history.sampling!=Coverage::complete)result.missingCoverage.emplace_back("sampling");
 if(history.retention!=Coverage::complete)result.missingCoverage.emplace_back("retention");
 if(history.purchaseAttribution!=Coverage::complete)result.missingCoverage.emplace_back("purchaseAttribution");
 if(history.settledPurchases<policy.minimumSamples)result.missingCoverage.emplace_back("settledPurchases");
 for(std::size_t i=0;i<history.costs.size();++i) {
  const auto& evidence=history.costs[i];
  if(!valid(evidence.basis))return {Error::invalid,{}};
  if(evidence.basis!=CostBasis::upperBound||!digest(evidence.evidenceSha256))
   result.missingCoverage.emplace_back(names[i]);
 }
 const auto& execution=history.costs[static_cast<std::size_t>(CostComponent::execution)];
 if(execution.basis==CostBasis::upperBound&&history.observedMaximumWorkerNanoUsd&&
    execution.nanoUsd<*history.observedMaximumWorkerNanoUsd)return {Error::invalid,{}};
 if(!result.missingCoverage.empty())return {Error::insufficientHistory,std::move(result)};
 // This denominator counts independently attributed non-operator purchases,
 // never invocation pairs, quote requests, replays or sampled extrapolations.
 const auto nonRevenue=ceiling(history.costs.back().nanoUsd,history.settledPurchases);
 Amount cost=nonRevenue;
 for(std::size_t i=0;i+1<history.costs.size();++i) {
  const auto total=add(cost,history.costs[i].nanoUsd);if(!total)return {total.error,{}};
  cost=total.value;
 }
 const auto total=add(cost,policy.overheadNanoUsd);if(!total)return {total.error,{}};
 // ceil(nanodollars * 10 / retained-margin-bps) yields USDC atomic units.
 // Quotient/remainder keeps the large intermediate from overflowing.
 const Amount denominator=10000-policy.targetMarginBasisPoints;
 const auto whole=multiply(total.value/denominator,10);if(!whole)return {whole.error,{}};
 const auto price=add(whole.value,ceiling((total.value%denominator)*10,denominator));
 if(!price)return {price.error,{}};
 result.priceAtomic=std::max({policy.currentPriceAtomic,minimumPriceAtomic,price.value});
 result.costBasisNanoUsd=total.value;result.nonRevenueAllocationNanoUsd=nonRevenue;
 return {Error::none,std::move(result)};
}
}
