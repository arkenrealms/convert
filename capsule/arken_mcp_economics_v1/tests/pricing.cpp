#include "arken_mcp_economics_v1/api.hpp"
#include <algorithm>
#include <limits>
#include <source_location>
#include <stdexcept>

namespace {
namespace E=arken::mcp::economics::v1;
void check(bool ok,std::source_location at=std::source_location::current()) {
 if(!ok)throw std::runtime_error("price qualification at line "+std::to_string(at.line()));
}
E::History qualifiedFixture() {
 // Synthetic coverage tests the native financial contract, not a live envelope.
 E::History h;
 h.service="math";h.command="math.v1.evaluate_expression";h.release=std::string(64,'a');
 h.workerVersion="version-a";h.inputClass="bounded-v1";
 h.windowStart=100;h.windowEnd=200;h.settledPurchases=30;h.rateCardIdentity=E::rateCard;
 h.evidenceSha256=std::string(64,'b');
 h.sampling=h.retention=h.purchaseAttribution=E::Coverage::complete;
 for(auto& c:h.costs)c={E::CostBasis::upperBound,0,std::string(64,'c')};
 h.costs[static_cast<std::size_t>(E::CostComponent::payment)].nanoUsd=1'000'000;
 h.costs.back().nanoUsd=61;
 return h;
}
void unqualified(const E::History& h,std::string_view reason) {
 E::PricePolicy p;p.overheadNanoUsd=1'000'000'000; // Large overhead never buys missing coverage.
 const auto r=E::recommendPrice(p,h,200);
 check(!r&&r.error==E::Error::insufficientHistory&&!r.value.priceAtomic&&
       !r.value.costBasisNanoUsd&&!r.value.nonRevenueAllocationNanoUsd);
 check(std::find(r.value.missingCoverage.begin(),r.value.missingCoverage.end(),reason)!=r.value.missingCoverage.end());
}
}
void checkPricing() {
 E::PricePolicy policy;auto history=qualifiedFixture();
 auto result=E::recommendPrice(policy,history,200);
 check(bool(result)&&result.value.priceAtomic==10000&&result.value.costBasisNanoUsd==1'000'003&&
       result.value.nonRevenueAllocationNanoUsd==3&&result.value.missingCoverage.empty());
 for(std::size_t i=0;i<E::costComponentCount;++i) {
  const auto name=E::costComponentName(static_cast<E::CostComponent>(i));
  for(const auto basis:{E::CostBasis::unavailable,E::CostBasis::observed}) {
   auto h=history;h.costs[i].basis=basis;unqualified(h,name);
  }
  auto h=history;h.costs[i].evidenceSha256.clear();unqualified(h,name);
  h.costs[i].basis=static_cast<E::CostBasis>(99);check(E::recommendPrice(policy,h,200).error==E::Error::invalid);
 }
 for(auto member:{&E::History::sampling,&E::History::retention,&E::History::purchaseAttribution}) {
  for(const auto coverage:{E::Coverage::unavailable,E::Coverage::partial}) {
   auto h=history;h.*member=coverage;
   check(E::recommendPrice(policy,h,200).error==E::Error::insufficientHistory);
  }
  auto h=history;h.*member=static_cast<E::Coverage>(99);check(E::recommendPrice(policy,h,200).error==E::Error::invalid);
 }
 auto h=history;h.evidenceSha256.clear();unqualified(h,"windowEvidence");
 h=history;h.settledPurchases=0;unqualified(h,"settledPurchases");
 h.settledPurchases=29;unqualified(h,"settledPurchases");
 h=history;h.workerVersion.clear();check(E::recommendPrice(policy,h,200).error==E::Error::invalid);
 h=history;h.rateCardIdentity="wrong-rate";check(E::recommendPrice(policy,h,200).error==E::Error::rateReview);
 check(E::recommendPrice(policy,history,E::rateValidThrough).error==E::Error::rateReview);
 check(E::recommendPrice(policy,history,200+8*86400).error==E::Error::staleHistory);
 check(E::recommendPrice(policy,history,199).error==E::Error::invalid);
 h=history;h.observedMaximumWorkerNanoUsd=1;
 check(E::recommendPrice(policy,h,200).error==E::Error::invalid);
 history.costs[0].nanoUsd=2'000'000;
 result=E::recommendPrice(policy,history,200);
 check(bool(result)&&result.value.priceAtomic==15001&&result.value.costBasisNanoUsd==3'000'003);
 policy.currentPriceAtomic=20000;check(E::recommendPrice(policy,history,200).value.priceAtomic==20000);
 policy.currentPriceAtomic=9999;check(E::recommendPrice(policy,history,200).error==E::Error::invalid);
 policy.currentPriceAtomic=10000;policy.targetMarginBasisPoints=10000;check(!E::recommendPrice(policy,history,200));
 policy.targetMarginBasisPoints=0;check(E::recommendPrice(policy,history,200).value.priceAtomic==10000);
 constexpr auto maximum=std::numeric_limits<E::Amount>::max();
 policy=E::PricePolicy{};h=history;h.costs[0].nanoUsd=maximum;
 check(E::recommendPrice(policy,h,200).error==E::Error::overflow);
 h=history;policy.overheadNanoUsd=maximum;check(E::recommendPrice(policy,h,200).error==E::Error::overflow);
 policy=E::PricePolicy{};h=history;h.costs.back().nanoUsd=maximum;
 result=E::recommendPrice(policy,h,200);
 check(bool(result)&&result.value.nonRevenueAllocationNanoUsd==maximum/30+(maximum%30!=0));
 h=qualifiedFixture();for(auto& cost:h.costs)cost.nanoUsd=0;
 h.costs[0].nanoUsd=maximum;policy.targetMarginBasisPoints=9999;
 result=E::recommendPrice(policy,h,200);check(result.error==E::Error::overflow&&!result.value.priceAtomic);
}
