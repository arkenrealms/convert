#include "arken_file_conversion_service_v1/paid_conversion.hpp"
#include "arken_file_conversion_service_v1/x402.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_file_conversion_service_v1/api.hpp"
#include "payment_json.hpp"
#include "arken_schema_generated/command-json.hpp"
#include <algorithm>
namespace arken::file_conversion::service::v1::payment {
namespace {
namespace J=arken::schema::cpp::v1::tool_json;
namespace S=arken::schema::generated::conversion::v1;
using detail::Json;
template<std::size_t N> auto Bytes(const Json& j){const auto value=J::read<arken::schema::cpp::v1::fixed_id<N,1>>(j);std::array<std::uint8_t,N> out{};
  for(std::size_t i=0;i<N;++i)out[i]=std::to_integer<std::uint8_t>(value.value[i]);return out;}
template<std::size_t N> std::string Hex(const std::array<std::uint8_t,N>& bytes){arken::schema::cpp::v1::fixed_id<N,1> id;
  for(std::size_t i=0;i<N;++i)id.value[i]=std::byte(bytes[i]);return J::write(id).template get<std::string>();}
Json X(const Json& input){auto result=detail::Parse(X402Packet(input.dump()));J::require(!result.contains("packetError"),"x402 rejected controller input");return result;}
Json Reply(std::string_view code){const Json value={{"code",code}};return {{"kind","result"},{"result",{{"isError",true},{"structuredContent",value},{"content",Json::array({{{"type","text"},{"text",value.dump()}}})}}}};}
Json QuotePacket(const Json& j){
  detail::Fields(j,{"kind","profileId","fileId","owner","inputDigest","arguments","release","expiresAt"});
  const auto profile=PaymentProfileById(J::readScalar<std::string>(j.at("profileId")));J::require(profile.has_value(),"unknown payment profile");
  auto args=j.at("arguments");args.erase("codec");
  const auto admission=detail::Parse(arken::file_conversion::service::v1::PlanRequest(Json{{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},
    {"params",{{"name","prepare_upload"},{"arguments",args}}}}.dump()));
  J::require(admission.at("kind")=="command"&&admission.at("commandId")==1,"invalid upload options");
  const auto upload=J::read<S::upload_request>(args);
  Quote q;q.operation=Bytes<32>(j.at("fileId"));q.tenant=Bytes<32>(j.at("owner"));q.input=Bytes<32>(j.at("inputDigest"));
  q.options=detail::PayloadDigest(J::write(upload).dump());q.release=Bytes<32>(j.at("release"));
  J::require(std::ranges::any_of(q.release,[](auto b){return b!=0;}),"release identity missing");
  q.priceVersion=detail::PayloadDigest(profile->priceIdentity);q.amount=profile->imagePrice;
  q.scope.chain=profile->chain;q.scope.asset=profile->asset;
  q.recipient=ImagePaymentRecipient();q.expiresAt=J::readScalar<std::uint64_t>(j.at("expiresAt"));
  return {{"quote",detail::WriteQuote(q)},{"codec",admission.at("arguments").at("codec")}};
}
Json Bind(const Json& j){
  detail::Fields(j,{"kind","quote","payment","binding","now"});auto quote=detail::ReadQuote(j.at("quote"));
  Json payment=j.at("payment");
  if(!j.at("binding").is_null()){
    const auto bound=detail::Parse(J::readScalar<std::string>(j.at("binding")));
    detail::Fields(bound,{"quote","payment"});
    const auto original=detail::ReadQuote(bound.at("quote"));
    J::require(original.operation==quote.operation&&original.tenant==quote.tenant&&original.input==quote.input
      &&original.options==quote.options&&original.expiresAt==quote.expiresAt,"file binding changed");
    // Existing quotes retain their original price/release. The current release
    // must still match before new conversion work, but completed receipts survive updates.
    quote=original;
    if(!payment.is_null()){
      const auto proposed=X({{"kind","prepare"},{"quote",detail::WriteQuote(quote)},{"payment",payment},{"now",j.at("now")}});
      J::require(proposed.at("payment")==bound.at("payment"),"payment binding changed");
    }
    payment=bound.at("payment");
  }
  auto result=X({{"kind","prepare"},{"quote",detail::WriteQuote(quote)},{"payment",payment},{"now",j.at("now")}});
  if(result.at("kind")=="admit")result["binding"]=Json{{"quote",detail::Parse(result.at("command").get<std::string>()).at("quote")},{"payment",result.at("payment")}}.dump();return result;
}
Output ReceiptOutput(const Json& j,const Operation& op){
  (void)J::read<S::conversion_result>(j);
  J::require(Bytes<32>(j.at("file_id"))==op.quote.operation,"receipt file mismatch");
  return {Bytes<32>(j.at("sha256")),op.quote.release,J::readScalar<std::uint32_t>(j.at("output_bytes")),
    J::readScalar<std::uint32_t>(j.at("width")),J::readScalar<std::uint32_t>(j.at("height"))};
}
Json OutputJson(const Output& o){return {{"digest",Hex(o.digest)},{"release",Hex(o.release)},{"bytes",o.bytes},{"width",o.width},{"height",o.height}};}
Json Next(const Json& j){
  detail::Fields(j,{"kind","decision","receipt","now","release"});const auto& d=j.at("decision");const auto op=detail::ReadOperation(d.at("operation"));
  J::require(ValidOperationRecord(op),"invalid controller operation");
  const auto error=J::readScalar<std::uint8_t>(d.at("error"));
  if(error==unsigned(Error::staleRevision))return {{"kind","inspect"}};
  const auto inspectCommand=[&]{Event e;e.kind=EventKind::inspect;e.expectedRevision=op.revision;e.now=J::readScalar<std::uint64_t>(j.at("now"));
    e.executingRelease=Bytes<32>(j.at("release"));return Json{{"version",1},{"quote",detail::WriteQuote(op.quote)},{"event",detail::WriteEvent(e)}}.dump();};
  if((error==unsigned(Error::expired)||error==unsigned(Error::releaseMismatch))&&op.stage<=Stage::outputReady&&op.authorization)return {{"kind","recover"},{"command",inspectCommand()}};
  if(error!=0)return Reply("payment_operation_rejected");
  const auto transition=[&](Json value){value["kind"]="transition";return value;};
  if(op.stage==Stage::settled){
    J::require(!j.at("receipt").is_null()&&ReceiptOutput(j.at("receipt"),op)==*op.output,"fulfilled output mismatch");
    const auto& receipt=j.at("receipt");
    const auto profile=PaymentProfileForQuote(op.quote);J::require(profile.has_value(),"unsupported receipt profile");
    const Json settlement={{"success",true},{"network",profile->network},{"transaction","0x"+Hex(op.transfer->transaction)},{"payer","0x"+Hex(op.quote.scope.payer)}};
    return {{"kind","result"},{"result",{{"structuredContent",receipt},{"content",Json::array({{{"type","text"},{"text",receipt.dump()}}})},
      {"_meta",{{"x402/payment-response",settlement}}}}}};
  }
  if(op.stage==Stage::failedNoCharge)return Reply("conversion_failed_no_charge");
  if(op.stage==Stage::settlementFailed)return Reply("settlement_failed");
  if(op.stage==Stage::settlementUnknown)return {{"kind","reconcile"}};
  if(op.stage==Stage::authorized||op.stage==Stage::outputReady)return transition(X({{"kind","control"},{"operation",d.at("operation")},
    {"event","resume"},{"now",j.at("now")},{"release","0x"+J::readScalar<std::string>(j.at("release"))}}));
  if(op.stage==Stage::computing&&!j.at("receipt").is_null())return transition(X({{"kind","output"},{"operation",d.at("operation")},
    {"output",OutputJson(ReceiptOutput(j.at("receipt"),op))},{"now",j.at("now")}}));
  const auto action=J::readScalar<std::uint8_t>(d.at("action"));const bool admitted=J::readScalar<bool>(d.at("changed"));
  if(admitted&&((op.stage==Stage::verifying&&action==unsigned(Action::verify))||(op.stage==Stage::settling&&action==unsigned(Action::settle))))return {{"kind","facilitate"}};
  if(admitted&&op.stage==Stage::computing&&action==unsigned(Action::execute))return {{"kind","convert"},{"inputDigest",Hex(op.quote.input)},{"command",inspectCommand()}};
  if(!admitted&&op.stage==Stage::settling)return {{"kind","reconcile"}};
  if(!admitted&&action==unsigned(Action::none)&&(op.stage==Stage::verifying||op.stage==Stage::computing))return {{"kind","recover"},{"command",inspectCommand()}};
  return Reply("operation_pending");
}
Json Fail(const Json& j){detail::Fields(j,{"kind","operation","now"});const auto op=detail::ReadOperation(j.at("operation"));
  J::require(ValidOperationRecord(op)&&op.stage==Stage::computing,"failure outside conversion");
  Event e;e.kind=EventKind::failBeforeSettlement;e.expectedRevision=op.revision;e.now=J::readScalar<std::uint64_t>(j.at("now"));e.failure=Failure::executionRejected;
  return {{"command",Json{{"version",1},{"quote",detail::WriteQuote(op.quote)},{"event",detail::WriteEvent(e)}}.dump()}};
}
}
std::string PaidConversionPacket(std::string_view text) noexcept {
  try{const auto j=detail::Parse(text);const auto kind=J::readScalar<std::string>(j.at("kind"));
    if(kind=="quote")return QuotePacket(j).dump();
    if(kind=="bind"){try{return Bind(j).dump();}catch(const std::exception&){return Reply("payment_binding_rejected").dump();}}
    if(kind=="next")return Next(j).dump();if(kind=="fail")return Fail(j).dump();
    return "{\"packetError\":\"unknown_paid_conversion_packet\"}";
  }catch(const std::exception&){return "{\"packetError\":\"invalid_paid_conversion_packet\"}";}
  catch(...){return "{\"packetError\":\"paid_conversion_unavailable\"}";}
}
}
