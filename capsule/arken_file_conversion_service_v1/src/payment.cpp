#include "arken_file_conversion_service_v1/payment.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include <algorithm>
#include <limits>
namespace arken::file_conversion::service::payment {
namespace {
bool HasClaim(Stage stage){return stage==Stage::authorized||stage==Stage::computing||stage==Stage::outputReady||stage==Stage::settling||stage==Stage::settlementUnknown||stage==Stage::settled||stage==Stage::settlementFailed;}
bool HasOutput(Stage stage){return stage==Stage::outputReady||stage==Stage::settling||stage==Stage::settlementUnknown||stage==Stage::settled||stage==Stage::settlementFailed;}
template<std::size_t N> bool Nonzero(const std::array<std::uint8_t,N>& value){return std::ranges::any_of(value,[](auto b){return b!=0;});}
bool OutputValid(const Output& output,const Quote& quote){
  namespace C=arken::file_conversion::v1;
  return output.bytes>0&&output.bytes<=C::maximumOutputBytes&&output.width>0&&output.height>0
    &&output.width<=C::maximumDimension&&output.height<=C::maximumDimension
    &&std::uint64_t(output.width)*output.height<=C::maximumPixels&&output.release==quote.release;
}
bool TransferValid(const Transfer& t,const Operation& op){return op.authorization&&Nonzero(t.transaction)
  &&t.scope==op.quote.scope&&t.recipient==op.quote.recipient&&t.amount==op.quote.amount&&t.nonce==op.authorization->nonce;}
bool Window(const Operation& op,std::uint64_t now){return op.authorization&&now<op.quote.expiresAt
  &&op.authorization->validAfter<now&&now<op.authorization->validBefore&&op.authorization->validBefore<=op.quote.expiresAt;}
bool RecordValid(const Operation& op){
  if(op.quote.amount==0||op.quote.scope.chain==0||op.quote.expiresAt==0||op.stage>Stage::settlementFailed||op.failure>Failure::settlementRejected)return false;
  if(!Nonzero(op.quote.scope.asset)||!Nonzero(op.quote.scope.payer)||!Nonzero(op.quote.recipient))return false;
  if(op.authorization&&(op.authorization->validBefore<=op.authorization->validAfter||op.authorization->validBefore>op.quote.expiresAt))return false;
  if(op.stage==Stage::quoted&&(op.authorization||op.output||op.transfer))return false;
  if((HasClaim(op.stage)||op.stage==Stage::verifying)&&!op.authorization)return false;
  if(HasOutput(op.stage)&&!op.output)return false;
  if(!HasOutput(op.stage)&&op.stage!=Stage::failedNoCharge&&op.output)return false;
  if(op.output&&!OutputValid(*op.output,op.quote))return false;
  if(op.stage==Stage::settled&&(!op.transfer||!TransferValid(*op.transfer,op)))return false;
  if(op.stage!=Stage::settled&&op.transfer)return false;
  if((op.stage==Stage::failedNoCharge||op.stage==Stage::settlementFailed)!=(op.failure!=Failure::none))return false;
  if(op.stage==Stage::settlementFailed&&op.failure!=Failure::settlementRejected)return false;
  if(op.stage==Stage::failedNoCharge&&op.failure==Failure::settlementRejected)return false;
  return true;
}
}
Decision Advance(const Operation& original,const Quote& observed,const std::optional<NonceClaim>& claim,const Event& event) noexcept {
  Decision result{.operation=original,.nonceClaim=claim};
  const auto reject=[&](Error error){result.error=error;result.action=Action::none;result.changed=false;result.operation=original;result.nonceClaim=claim;return result;};
  if(!RecordValid(original))return reject(Error::invalidRecord);
  if(original.quote!=observed)return reject(Error::quoteConflict);
  if(original.revision!=event.expectedRevision)return reject(Error::staleRevision);
  if(event.authorization&&original.authorization&&event.authorization!=original.authorization)return reject(Error::authorizationConflict);
  const auto expectedClaim=[&]{return NonceClaim{original.quote.scope,original.authorization->nonce,original.quote.operation,original.authorization->payload};};
  if(HasClaim(original.stage)){
    if(!claim)return reject(Error::nonceClaimMissing);
    if(*claim!=expectedClaim())return reject(Error::nonceConflict);
  }
  const auto change=[&](Stage stage,Action action){
    if(original.revision==std::numeric_limits<std::uint64_t>::max())return reject(Error::revisionOverflow);
    result.operation.stage=stage;result.operation.revision=original.revision+1;result.action=action;result.changed=true;return result;
  };
  switch(event.kind) {
    case EventKind::request:
      if(original.stage==Stage::settled){result.action=Action::receipt;return result;}
      if(original.stage==Stage::failedNoCharge||original.stage==Stage::settlementFailed){result.action=Action::failure;return result;}
      if(original.stage==Stage::settlementUnknown){result.action=Action::reconcile;return result;}
      // Resume safe pre-effect boundaries here, not in a JS state machine.
      if(original.stage==Stage::authorized||original.stage==Stage::outputReady){auto resumed=event;
        resumed.kind=original.stage==Stage::authorized?EventKind::beginConversion:EventKind::beginSettlement;
        return Advance(original,observed,claim,resumed);}
      if(original.stage!=Stage::quoted){result.action=Action::wait;return result;}
      if(!event.authorization)return reject(Error::invalidEvent);
      result.operation.authorization=event.authorization;
      if(!Window(result.operation,event.now))return reject(Error::expired);
      return change(Stage::verifying,Action::verify);
    case EventKind::retryVerification:
      if(original.stage!=Stage::verifying)return reject(Error::invalidEvent);
      if(!Window(original,event.now))return reject(Error::expired);
      return change(Stage::verifying,Action::verify);
    case EventKind::verificationPassed:
      if(original.stage!=Stage::verifying)return reject(Error::invalidEvent);
      if(event.verifiedPayer!=original.quote.scope.payer)return reject(Error::payerMismatch);
      if(!Window(original,event.now))return reject(Error::expired);
      if(claim&&*claim!=expectedClaim())return reject(Error::nonceConflict);
      result.nonceClaim=expectedClaim();return change(Stage::authorized,Action::none);
    case EventKind::verificationRejected:
      if(original.stage!=Stage::verifying)return reject(Error::invalidEvent);
      result.operation.failure=Failure::verificationRejected;return change(Stage::failedNoCharge,Action::failure);
    case EventKind::beginConversion:
      if(original.stage!=Stage::authorized)return reject(Error::invalidEvent);
      if(event.executingRelease!=original.quote.release)return reject(Error::releaseMismatch);
      if(!Window(original,event.now))return reject(Error::expired);
      return change(Stage::computing,Action::convert);
    case EventKind::outputCommitted:
      if(original.stage!=Stage::computing||!event.output)return reject(Error::invalidEvent);
      if(!OutputValid(*event.output,original.quote))return reject(Error::outputInvalid);
      result.operation.output=event.output;return change(Stage::outputReady,Action::none);
    case EventKind::failBeforeSettlement:
      if(original.stage>Stage::outputReady||event.failure==Failure::none||event.failure>=Failure::settlementRejected)return reject(Error::invalidEvent);
      result.operation.failure=event.failure;return change(Stage::failedNoCharge,Action::failure);
    case EventKind::beginSettlement:
      if(original.stage!=Stage::outputReady)return reject(Error::invalidEvent);
      if(!Window(original,event.now))return reject(Error::expired);
      return change(Stage::settling,Action::settle);
    case EventKind::settlementUncertain:
      if(original.stage!=Stage::settling)return reject(Error::invalidEvent);
      return change(Stage::settlementUnknown,Action::reconcile);
    case EventKind::settlementSucceeded:
    case EventKind::reconciledSucceeded:
      if(event.kind==EventKind::settlementSucceeded&&original.stage!=Stage::settling)return reject(Error::invalidEvent);
      if(event.kind==EventKind::reconciledSucceeded&&original.stage!=Stage::settlementUnknown)return reject(Error::invalidEvent);
      if(!event.transfer||!TransferValid(*event.transfer,original))return reject(Error::transferMismatch);
      result.operation.transfer=event.transfer;return change(Stage::settled,Action::receipt);
    case EventKind::settlementRejected:
    case EventKind::reconciledFailed:
      if(event.kind==EventKind::settlementRejected&&original.stage!=Stage::settling)return reject(Error::invalidEvent);
      if(event.kind==EventKind::reconciledFailed&&original.stage!=Stage::settlementUnknown)return reject(Error::invalidEvent);
      result.operation.failure=Failure::settlementRejected;return change(Stage::settlementFailed,Action::failure);
  }
  return reject(Error::invalidEvent);
}
}
