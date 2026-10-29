#include "arken_mcp_economics_v1/api.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <source_location>
namespace E=arken::mcp::economics::v1;
void checkBudgetProtocol();
void checkPricing();
void check(bool ok,std::source_location at=std::source_location::current()) {
 if(!ok)throw std::runtime_error("economics invariant at line "+std::to_string(at.line()));
}
int main(int argc,char** argv) {
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-mcp-economics-tests r5825\n";return 0;}
 try {
  checkBudgetProtocol();
  checkPricing();
  const E::Usage measured{176,2804800,58,189978752,92,12,std::nullopt,std::nullopt};
  const auto estimate=E::estimate(measured);check(bool(estimate));
  check(estimate.value.observedNanoUsd==132063&&estimate.value.missing.size()==2);
  check(E::usdText(estimate.value.observedNanoUsd)=="0.000132063");
  check(E::differenceText(10000000,132063)=="0.009867937");
  check(E::differenceText(0,1)=="-0.000000001");
  check(E::estimate({}).value.missing.size()==8);
  E::Usage zero;zero.workerRequests=0;zero.workerCpuMicroseconds=1;
  check(E::estimate(zero).value.observedNanoUsd==1);
  zero.workerRequests=std::numeric_limits<std::uint64_t>::max();check(!E::estimate(zero));
  check(!E::add(std::numeric_limits<std::uint64_t>::max(),1));
  E::BudgetPolicy cap{"fleet/public-v1",3,25,10};
  auto first=E::reserve(cap,std::nullopt,100);check(bool(first)&&first.value.state.attempts==1);
  // No outcome participates in admission: successful, unpaid and failed attempts
  // all retain their reservations. The storage adapter serializes these calls.
  auto unpaid=E::reserve(cap,first.value.state,101);check(bool(unpaid)&&unpaid.value.state.reservedNanoUsd==20);
  auto denied=E::reserve(cap,unpaid.value.state,102);check(denied.error==E::Error::exhausted);
  check(denied.value.state==unpaid.value.state&&denied.value.retryAfterSeconds==86298);
  cap.maximumReservedNanoUsd=40;
  auto failed=E::reserve(cap,unpaid.value.state,102);check(bool(failed)&&failed.value.state.attempts==3);
  check(E::reserve(cap,failed.value.state,103).error==E::Error::exhausted);
  cap.maximumAttempts=2;check(E::reserve(cap,failed.value.state,103).error==E::Error::exhausted);
  check(E::reserve(cap,failed.value.state,101).error==E::Error::clockReversed);
  auto next=E::reserve(cap,failed.value.state,86400);check(bool(next)&&next.value.state.attempts==1&&next.value.state.revision==4);
  cap.scope="another";check(!E::reserve(cap,next.value.state,86401));
  std::cout<<"passed: exact rates/rounding, overflow, one-cent floor, qualified upward pricing, failed/unpaid reservations, policy retention, clock and day fences\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
