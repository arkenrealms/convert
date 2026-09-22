#include "arken_file_conversion_service_v1/x402.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/paid_conversion.hpp"
#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "../src/payment_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "../tools/native/platform.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>
namespace P=arken::file_conversion::service::v1::payment;
namespace J=arken::schema::cpp::v1::tool_json;
using Json=nlohmann::json;
namespace {
unsigned checks=0;Json vectors=Json::array(),paidVectors=Json::array();
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
Json Wire(Json input){const auto text=input.dump(),result=P::X402Packet(text);vectors.push_back({{"input",text},{"expected",result}});return Json::parse(result);}
Json Paid(Json input){const auto text=input.dump(),result=P::PaidConversionPacket(text);paidVectors.push_back({{"input",text},{"expected",result}});return Json::parse(result);}
P::Address Address(std::string_view text){const auto value=J::read<arken::schema::cpp::v1::fixed_id<20,1>>(std::string(text));P::Address out{};for(unsigned i=0;i<20;++i)out[i]=std::to_integer<std::uint8_t>(value.value[i]);return out;}
P::Quote Quote(std::uint64_t now=100,bool mainnet=false){P::Quote q;q.operation[0]=42;q.tenant[0]=2;q.input[0]=3;q.options[0]=4;q.release[0]=5;q.priceVersion[0]=6;
  const auto profile=P::PaymentProfileById(mainnet?"base-mainnet-v1":"base-sepolia-v1");q.scope.chain=profile->chain;q.scope.asset=profile->asset;
  q.recipient=Address("554933cd7c5275ff3692bf97e0bc90b69c85d3dc");q.amount=10000;q.expiresAt=now+600;return q;}
Json Payment(const Json& challenge,std::uint64_t now=100){const auto& required=challenge.at("result").at("structuredContent");
  return {{"x402Version",2},{"resource",required["resource"]},{"accepted",required["accepts"][0]},
    {"payload",{{"signature","0x"+std::string(130,'0')},{"authorization",{{"from","0x"+std::string(40,'1')},{"to",required["accepts"][0]["payTo"]},
      {"value","10000"},{"validAfter",std::to_string(now-10)},{"validBefore",std::to_string(now+50)},{"nonce","0x"+std::string(64,'9')}}}}}};}
struct Ledger {
  std::map<std::string,std::string> values;
  Json transition(std::string command){
    auto keys=Json::parse(P::StorageLookupPacket(Json{{"command",command},{"record",nullptr}}.dump()));
    const auto get=[&](const Json& key)->Json{if(key.is_null())return nullptr;const auto it=values.find(key.get<std::string>());return it==values.end()?Json(nullptr):Json(it->second);};
    const auto record=get(keys.at("operationKey"));keys=Json::parse(P::StorageLookupPacket(Json{{"command",command},{"record",record}}.dump()));
    auto plan=Json::parse(P::StorageAdvancePacket(Json{{"command",command},{"record",record},{"scope",get(keys.at("scopeKey"))},{"claim",get(keys.at("nonceKey"))}}.dump()));
    Check(!plan.contains("packetError"),"storage admission failed");
    for(const auto& write:plan.at("writes"))values[write.at("key").get<std::string>()]=write.at("value").get<std::string>();
    return plan.at("decision");
  }
  bool dispatch(const Json& decision){
    const auto control=Wire({{"kind","control"},{"operation",decision["operation"]},{"event","inspect"},{"now","100"},{"release","0x"+std::string(64,'0')}});
    const auto command=control.at("command").get<std::string>();auto keys=Json::parse(P::StorageLookupPacket(Json{{"command",command},{"record",nullptr}}.dump()));
    const auto get=[&](const Json& key)->Json{if(key.is_null())return nullptr;const auto it=values.find(key.get<std::string>());return it==values.end()?Json(nullptr):Json(it->second);};
    const auto record=get(keys.at("operationKey"));keys=Json::parse(P::StorageLookupPacket(Json{{"command",command},{"record",record}}.dump()));
    auto input=Json{{"command",command},{"record",record},{"scope",get(keys.at("scopeKey"))},{"claim",get(keys.at("nonceKey"))},{"decision",decision.dump()},{"dispatch",nullptr}};
    auto plan=Json::parse(P::StorageDispatchPacket(input.dump()));Check(!plan.contains("packetError"),"dispatch shape");input["dispatch"]=get(plan.at("key"));
    plan=Json::parse(P::StorageDispatchPacket(input.dump()));Check(!plan.contains("packetError"),"dispatch state");
    if(plan.at("claimed")==true)values[plan["key"].get<std::string>()]=plan["value"].get<std::string>();return plan.at("claimed").get<bool>();
  }
};
Json Fixture(bool mainnet=false){
  const auto profileId=mainnet?"base-mainnet-v1":"base-sepolia-v1";
  const std::string network=mainnet?"eip155:8453":"eip155:84532",otherNetwork=mainnet?"eip155:84532":"eip155:8453";
  const std::string endpoint=mainnet?"https://api.cdp.coinbase.com/platform/v2/x402":"https://x402.org/facilitator";
  const auto q=P::detail::WriteQuote(Quote(100,mainnet));const Json prepare={{"kind","prepare"},{"quote",q},{"payment",nullptr},{"now","100"}};
  const auto challenge=Wire(prepare);Check(challenge.at("kind")=="challenge","challenge");const auto& r=challenge.at("result");
  Check(r.at("isError")==true&&Json::parse(r.at("content")[0].at("text").get<std::string>())==r.at("structuredContent"),"MCP challenge mirrors");
  Check(r.at("structuredContent").at("accepts")[0].at("extra")==Json{{"name",mainnet?"USD Coin":"USDC"},{"version","2"}},"wrong token signature domain");
  const auto payment=Payment(challenge);auto admission=prepare;admission["payment"]=payment.dump();const auto admitted=Wire(admission);
  Check(admitted.at("kind")=="admit","payment shape admission");Ledger ledger;auto decision=ledger.transition(admitted.at("command").get<std::string>());
  Check(decision["action"]==unsigned(P::Action::verify),"verify not selected");
  Check(std::ranges::any_of(ledger.values,[&](const auto& pair){return pair.first.starts_with("authorization/")&&pair.second==admitted["payment"].get<std::string>();}),"authorization was not retained before verification");
  auto effect=Wire({{"kind","effect"},{"decision",decision},{"payment",admitted["payment"]},{"now","100"}});
  Check(effect.at("request").at("url")==endpoint+"/verify"&&effect["request"]["method"]=="POST"&&effect["request"]["authentication"]==(mainnet?"cdp":"none"),"profile verification endpoint");
  Check(Wire({{"kind","transport"},{"request",effect.at("request")}}).at("request")==effect.at("request"),"transport scope rejected owned request");
  for(const auto field:{"url","method","authentication"}){auto invalid=effect.at("request");invalid[field]="unapproved";Check(Wire({{"kind","transport"},{"request",invalid}}).contains("packetError"),"transport scope widened");}
  for(unsigned i=0;i<4;++i){auto invalid=effect.at("request");
    if(i==0)invalid["authentication"]=mainnet?"none":"cdp";
    if(i==1)invalid["method"]="GET";
    if(i==2)invalid["url"]=mainnet?"https://x402.org/facilitator/verify":"https://api.cdp.coinbase.com/platform/v2/x402/verify";
    if(i==3)invalid["body"]=std::string(8193,'x');
    Check(Wire({{"kind","transport"},{"request",invalid}}).contains("packetError"),"valid alternate transport value bypassed profile");}
  Check(ledger.dispatch(decision)&&!ledger.dispatch(decision),"verify dispatch replay");
  for(const auto& invalid:{Json{{"isValid",true},{"payer","0x"+std::string(40,'3')}},Json{{"isValid","true"}},Json::object()}){
    const auto bad=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","verify"},{"response",invalid.dump()},{"status",200},{"now","101"}});
    Check(bad.at("kind")=="unavailable","untrusted facilitator verification accepted");}
  const Json goodVerify={{"isValid",true},{"payer","0x"+std::string(40,'1')}};
  for(const auto& [status,response,reason]:std::vector<std::tuple<unsigned,std::string,std::string>>{
    {403,"private provider text","http_403"},{200,"not JSON","invalid_json"},{200,"{}","invalid_validity"},{0,"{\"transportError\":\"TypeError\"}","transport_type_error"}}){
    const auto diagnostic=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","verify"},{"response",response},{"status",status},{"now","101"}});
    Check(diagnostic.at("code")=="facilitator_verification_unavailable_"+reason,"unsafe or missing facilitator diagnostic");
  }
  auto callback=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","verify"},{"response",goodVerify.dump()},{"status",200},{"now","101"}});
  const auto verified=ledger.transition(callback["command"].get<std::string>());Check(verified["operation"]["stage"]==unsigned(P::Stage::authorized),"verified callback");
  const auto resume=Wire({{"kind","control"},{"operation",verified["operation"]},{"event","resume"},{"now","101"},{"release","0x05"+std::string(62,'0')}});
  decision=ledger.transition(resume["command"].get<std::string>());Check(decision["action"]==unsigned(P::Action::execute),"resume compute");
  auto op=P::detail::ReadOperation(decision["operation"]);P::Event output;output.kind=P::EventKind::outputCommitted;output.expectedRevision=op.revision;output.now=101;
  output.output=P::Output{.digest=op.quote.input,.release=op.quote.release,.bytes=76,.width=1,.height=1};
  const std::string outputCommand=Json{{"version",1},{"quote",P::detail::WriteQuote(op.quote)},{"event",P::detail::WriteEvent(output)}}.dump();
  decision=ledger.transition(outputCommand);
  const auto settle=Wire({{"kind","control"},{"operation",decision["operation"]},{"event","resume"},{"now","101"},{"release","0x05"+std::string(62,'0')}});
  decision=ledger.transition(settle["command"].get<std::string>());Check(decision["action"]==unsigned(P::Action::settle),"settlement not admitted");
  const auto settleEffect=Wire({{"kind","effect"},{"decision",decision},{"payment",admitted["payment"]},{"now","101"}});
  Check(settleEffect["request"]["url"]==endpoint+"/settle"&&settleEffect["request"]["authentication"]==(mainnet?"cdp":"none"),"settle endpoint");
  Check(ledger.dispatch(decision)&&!ledger.dispatch(decision),"settle dispatch replay");
  auto malformedDecision=decision;malformedDecision["operation"]["output"]=nullptr;
  Check(Wire({{"kind","effect"},{"decision",malformedDecision},{"payment",admitted["payment"]},{"now","101"}}).contains("packetError"),"settlement without output accepted");
  const Json goodSettle={{"success",true},{"network",network},{"payer","0x"+std::string(40,'1')},{"transaction","0x"+std::string(64,'2')}};
  for(const auto& response:{Json{{"success",false}},Json{{"success",true},{"payer","wrong"}},Json::object()}){
    const auto unknown=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",response.dump()},{"status",200},{"now","151"}});
    Check(Json::parse(unknown.at("command").get<std::string>())["event"]["kind"]==unsigned(P::EventKind::settlementUncertain),"failed settlement solicited new charge");}
  auto wrongNetwork=goodSettle;wrongNetwork["network"]=otherNetwork;
  const auto unknown=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",wrongNetwork.dump()},{"status",200},{"now","151"}});
  Check(Json::parse(unknown.at("command").get<std::string>())["event"]["kind"]==unsigned(P::EventKind::settlementUncertain),"wrong settlement network accepted");
  const auto receipt=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",goodSettle.dump()},{"status",200},{"now","151"}});
  const auto fulfilled=ledger.transition(receipt["command"].get<std::string>());
  Check(fulfilled["operation"]["stage"]==unsigned(P::Stage::settled),"settlement receipt");
  Check(!ledger.dispatch(decision),"old settle decision dispatched after receipt");
  for(unsigned i=0;i<11;++i){auto invalid=payment;
    if(i==0)invalid["accepted"]["amount"]="1";if(i==1)invalid["accepted"]["network"]=otherNetwork;
    if(i==2)invalid["accepted"]["asset"]="0x"+std::string(40,'0');if(i==3)invalid["accepted"]["payTo"]="0x"+std::string(40,'1');
    if(i==4)invalid["payload"]["authorization"]["value"]="9999";if(i==5)invalid["resource"]["url"]="mcp://different-operation";
    if(i==6)invalid["payload"]["authorization"]["validBefore"]="0100";if(i==7)invalid["payload"]["signature"]="0xgg";
    if(i==8)invalid["x402Version"]=1;
    if(i==9)invalid["accepted"]["extra"]["name"]=mainnet?"USDC":"USD Coin";
    if(i==10)invalid["accepted"]["extra"]["version"]="1";
    auto request=admission;request["payment"]=invalid.dump();Check(Wire(request).contains("packetError"),"untrusted payment mismatch accepted");}
  auto wrongChain=prepare;wrongChain["quote"]["scope"]["chain"]=mainnet?84532:8453;Check(Wire(wrongChain).contains("packetError"),"mismatched chain/token accepted");
  auto quoteRequest=Json{{"kind","quote"},{"profileId",profileId},{"fileId",q["operation"]},{"owner",q["tenant"]},{"inputDigest",q["input"]},{"release",q["release"]},{"expiresAt",q["expiresAt"]},
    {"arguments",{{"source_bytes",22},{"input_format","tga"},{"output_format","png"}}}};
  const auto fileQuote=Paid(quoteRequest).at("quote");Check(fileQuote["amount"]=="10000"&&fileQuote["scope"]["chain"]==(mainnet?8453:84532),"profile quote");
  if(!mainnet)Check(P::detail::ReadQuote(fileQuote).priceVersion==P::detail::PayloadDigest("arken-convert:base-sepolia:image:v1:10000"),"existing price identity changed");
  auto invalidProfile=quoteRequest;invalidProfile["profileId"]="unknown";Check(Paid(invalidProfile).contains("packetError"),"unknown private profile admitted");
  const auto bound=Paid({{"kind","bind"},{"quote",fileQuote},{"payment",payment.dump()},{"binding",nullptr},{"now","100"}});
  Check(bound["kind"]=="admit","file authorization binding");
  const auto repeated=Paid({{"kind","bind"},{"quote",fileQuote},{"payment",payment.dump()},{"binding",bound["binding"]},{"now","101"}});
  Check(repeated["binding"]==bound["binding"],"clock changed immutable payment binding");
  auto newDefault=quoteRequest;newDefault["profileId"]=mainnet?"base-sepolia-v1":"base-mainnet-v1";const auto newQuote=Paid(newDefault).at("quote");
  Check(newQuote["priceVersion"]!=fileQuote["priceVersion"],"profiles share price identity");
  const auto retained=Paid({{"kind","bind"},{"quote",newQuote},{"payment",nullptr},{"binding",bound["binding"]},{"now","101"}});
  Check(retained.at("binding")==bound.at("binding"),"current default rerouted old bound operation");
  const auto newChallenge=Wire({{"kind","prepare"},{"quote",newQuote},{"payment",nullptr},{"now","100"}});
  Check(Paid({{"kind","bind"},{"quote",newQuote},{"payment",Payment(newChallenge).dump()},{"binding",bound["binding"]},{"now","101"}}).at("result").at("isError")==true,"new network payment replaced retained authorization");
  for(const auto field:{"operation","tenant","input","options"}){auto changed=fileQuote;changed[field]=std::string(64,'e');
    Check(Paid({{"kind","bind"},{"quote",changed},{"payment",nullptr},{"binding",bound["binding"]},{"now","101"}})["result"]["isError"]==true,"changed file identity rebound");}
  const Json publicReceipt={{"file_id",q["operation"]},{"download_url","https://converter.example/output"},{"expires_at","2026-09-13T02:00:00.000Z"},
    {"width",1},{"height",1},{"output_bytes",76},{"sha256",q["input"]},{"media_type","image/png"},{"alpha_disposition","preserved"}};
  const auto delivered=Paid({{"kind","next"},{"decision",fulfilled},{"receipt",publicReceipt},{"now","151"},{"release",std::string(64,'8')}});
  Check(delivered["result"]["_meta"]["x402/payment-response"]["success"]==true&&delivered["result"]["_meta"]["x402/payment-response"]["network"]==network,"public settlement metadata");
  if(mainnet){const auto recovery=Json::parse(P::SettlementReconciliationPacket(Json{{"operation",decision["operation"]},{"anchor",nullptr},{"logs",nullptr},{"receipt",nullptr},{"now","151"},{"rpcClientKey",nullptr}}.dump()));
    Check(recovery==Json{{"kind","pending"},{"code","mainnet_reconciliation_not_configured"}},"mainnet recovery used unqualified RPC");}
  auto incorrect=publicReceipt;incorrect["output_bytes"]=77;
  Check(Paid({{"kind","next"},{"decision",fulfilled},{"receipt",incorrect},{"now","151"},{"release",q["release"]}}).contains("packetError"),"receipt bytes mismatch disclosed");
  const auto waiting=Paid({{"kind","next"},{"decision",decision},{"receipt",publicReceipt},{"now","101"},{"release",q["release"]}});
  Check(waiting["kind"]=="facilitate"&&!waiting.contains("result"),"unsettled receipt exposed");
  const Json publicRequest={{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",{{"name","conversion.v1.convert_image"},{"arguments",{{"file_id",q["operation"]}}},{"_meta",{{"x402/payment",payment}}}}}};
  Check(Json::parse(arken::file_conversion::service::v1::PlanRequest(publicRequest.dump(),arken::file_conversion::service::v1::ServerMode::sandbox))["payment"]==payment.dump(),"payment metadata not retained");
  Check(Json::parse(arken::file_conversion::service::v1::PlanRequest(publicRequest.dump()))["kind"]=="response","preview accepted paid metadata");
  return {{"quote",q},{"payment",payment.dump()},{"challenge",challenge},{"verifiedResponse",goodVerify.dump()},{"settledResponse",goodSettle.dump()},
    {"outputCommand",outputCommand},{"verified",verified},{"settling",decision},{"release","0x05"+std::string(62,'0')}};
}
}
int main(int argc,char** argv){
  if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-x402-tests r4407\n";return 0;}
  try{const bool probe=argc==3&&std::string_view(argv[1])=="--probe-rejection";const bool write=argc==3&&std::string_view(argv[1])=="--write-fixtures";
    Check(argc==1||probe||write,"arguments");const auto fixture=Fixture();const auto mainnetFixture=Fixture(true);
    if(write){const std::filesystem::path root(argv[2]);std::filesystem::create_directories(root);std::ofstream out(root/"x402-fixtures.json");out<<Json{{"fixture",fixture},{"mainnetFixture",mainnetFixture},{"vectors",vectors},{"paidVectors",paidVectors}}.dump();Check(bool(out),"fixture write");}
    if(probe){namespace C=arken::conversion::cloudflare;const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const auto quote=P::detail::WriteQuote(Quote(now));auto prep=Wire({{"kind","prepare"},{"quote",quote},{"payment",nullptr},{"now",std::to_string(now)}});
      prep=Wire({{"kind","prepare"},{"quote",quote},{"payment",Payment(prep,now).dump()},{"now",std::to_string(now)}});Ledger ledger;
      const auto decision=ledger.transition(prep["command"].get<std::string>());const auto request=Wire({{"kind","effect"},{"decision",decision},{"payment",prep["payment"]},{"now",std::to_string(now)}})["request"];
      Check(request.at("url")=="https://x402.org/facilitator/verify","rejection probe must never settle");
      const auto send=C::CurlTransport(std::filesystem::path(argv[2])/"http");const auto response=send({"POST",request["url"].get<std::string>(),request["body"].get<std::string>(),"application/json",{}});
      const auto json=Json::parse(response.body);Check(response.status==200&&json.at("isValid")==false,"invalid signature was not rejected");
      const Json proof={{"status","passed"},{"httpStatus",response.status},{"isValid",false},{"invalidReason",json.value("invalidReason","")},{"settlementCalls",0}};
      C::Write(std::filesystem::path(argv[2])/"proof.json",proof.dump(2)+"\n");std::cout<<proof.dump(2)<<'\n';}
    std::cout<<checks<<" x402 checks passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
