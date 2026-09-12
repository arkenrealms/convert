#include "arken_file_conversion_service_v1/x402.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "../src/payment_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "../tools/native/platform.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
namespace P=arken::file_conversion::service::v1::payment;
namespace J=arken::schema::cpp::v1::tool_json;
using Json=nlohmann::json;
namespace {
unsigned checks=0;Json vectors=Json::array();
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
Json Wire(Json input){const auto text=input.dump(),result=P::X402Packet(text);vectors.push_back({{"input",text},{"expected",result}});return Json::parse(result);}
P::Address Address(std::string_view text){const auto value=J::read<arken::schema::cpp::v1::fixed_id<20,1>>(std::string(text));P::Address out{};for(unsigned i=0;i<20;++i)out[i]=std::to_integer<std::uint8_t>(value.value[i]);return out;}
P::Quote Quote(std::uint64_t now=100){P::Quote q;q.operation[0]=42;q.tenant[0]=2;q.input[0]=3;q.options[0]=4;q.release[0]=5;q.priceVersion[0]=6;
  q.scope.chain=84532;q.scope.asset=Address("036cbd53842c5426634e7929541ec2318f3dcf7e");
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
Json Fixture(){
  const auto q=P::detail::WriteQuote(Quote());const Json prepare={{"kind","prepare"},{"quote",q},{"payment",nullptr},{"now","100"}};
  const auto challenge=Wire(prepare);Check(challenge.at("kind")=="challenge","challenge");const auto& r=challenge.at("result");
  Check(r.at("isError")==true&&Json::parse(r.at("content")[0].at("text").get<std::string>())==r.at("structuredContent"),"MCP challenge mirrors");
  const auto payment=Payment(challenge);auto admission=prepare;admission["payment"]=payment.dump();const auto admitted=Wire(admission);
  Check(admitted.at("kind")=="admit","payment shape admission");Ledger ledger;auto decision=ledger.transition(admitted.at("command").get<std::string>());
  Check(decision["action"]==unsigned(P::Action::verify),"verify not selected");
  Check(std::ranges::any_of(ledger.values,[&](const auto& pair){return pair.first.starts_with("authorization/")&&pair.second==admitted["payment"].get<std::string>();}),"authorization was not retained before verification");
  auto effect=Wire({{"kind","effect"},{"decision",decision},{"payment",admitted["payment"]},{"now","100"}});
  Check(effect.at("request").at("url")=="https://x402.org/facilitator/verify","fixed sandbox endpoint");
  Check(ledger.dispatch(decision)&&!ledger.dispatch(decision),"verify dispatch replay");
  for(const auto& invalid:{Json{{"isValid",true},{"payer","0x"+std::string(40,'3')}},Json{{"isValid","true"}},Json::object()}){
    const auto bad=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","verify"},{"response",invalid.dump()},{"status",200},{"now","101"}});
    Check(bad.at("kind")=="unavailable","untrusted facilitator verification accepted");}
  const Json goodVerify={{"isValid",true},{"payer","0x"+std::string(40,'1')}};
  auto callback=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","verify"},{"response",goodVerify.dump()},{"status",200},{"now","101"}});
  const auto verified=ledger.transition(callback["command"].get<std::string>());Check(verified["operation"]["stage"]==unsigned(P::Stage::authorized),"verified callback");
  const auto resume=Wire({{"kind","control"},{"operation",verified["operation"]},{"event","resume"},{"now","101"},{"release","0x05"+std::string(62,'0')}});
  decision=ledger.transition(resume["command"].get<std::string>());Check(decision["action"]==unsigned(P::Action::convert),"resume compute");
  auto op=P::detail::ReadOperation(decision["operation"]);P::Event output;output.kind=P::EventKind::outputCommitted;output.expectedRevision=op.revision;output.now=101;
  output.output=P::Output{.digest=op.quote.input,.release=op.quote.release,.bytes=76,.width=1,.height=1};
  const std::string outputCommand=Json{{"version",1},{"quote",P::detail::WriteQuote(op.quote)},{"event",P::detail::WriteEvent(output)}}.dump();
  decision=ledger.transition(outputCommand);
  const auto settle=Wire({{"kind","control"},{"operation",decision["operation"]},{"event","resume"},{"now","101"},{"release","0x05"+std::string(62,'0')}});
  decision=ledger.transition(settle["command"].get<std::string>());Check(decision["action"]==unsigned(P::Action::settle),"settlement not admitted");
  const auto settleEffect=Wire({{"kind","effect"},{"decision",decision},{"payment",admitted["payment"]},{"now","101"}});
  Check(settleEffect["request"]["url"]=="https://x402.org/facilitator/settle","settle endpoint");
  Check(ledger.dispatch(decision)&&!ledger.dispatch(decision),"settle dispatch replay");
  auto malformedDecision=decision;malformedDecision["operation"]["output"]=nullptr;
  Check(Wire({{"kind","effect"},{"decision",malformedDecision},{"payment",admitted["payment"]},{"now","101"}}).contains("packetError"),"settlement without output accepted");
  const Json goodSettle={{"success",true},{"network","eip155:84532"},{"payer","0x"+std::string(40,'1')},{"transaction","0x"+std::string(64,'2')}};
  for(const auto& response:{Json{{"success",false}},Json{{"success",true},{"payer","wrong"}},Json::object()}){
    const auto unknown=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",response.dump()},{"status",200},{"now","151"}});
    Check(Json::parse(unknown.at("command").get<std::string>())["event"]["kind"]==unsigned(P::EventKind::settlementUncertain),"failed settlement solicited new charge");}
  auto wrongNetwork=goodSettle;wrongNetwork["network"]="eip155:8453";
  const auto unknown=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",wrongNetwork.dump()},{"status",200},{"now","151"}});
  Check(Json::parse(unknown.at("command").get<std::string>())["event"]["kind"]==unsigned(P::EventKind::settlementUncertain),"wrong settlement network accepted");
  const auto receipt=Wire({{"kind","callback"},{"operation",decision["operation"]},{"phase","settle"},{"response",goodSettle.dump()},{"status",200},{"now","151"}});
  Check(ledger.transition(receipt["command"].get<std::string>())["operation"]["stage"]==unsigned(P::Stage::settled),"settlement receipt");
  Check(!ledger.dispatch(decision),"old settle decision dispatched after receipt");
  for(unsigned i=0;i<9;++i){auto invalid=payment;
    if(i==0)invalid["accepted"]["amount"]="1";if(i==1)invalid["accepted"]["network"]="eip155:8453";
    if(i==2)invalid["accepted"]["asset"]="0x"+std::string(40,'0');if(i==3)invalid["accepted"]["payTo"]="0x"+std::string(40,'1');
    if(i==4)invalid["payload"]["authorization"]["value"]="9999";if(i==5)invalid["resource"]["url"]="mcp://different-operation";
    if(i==6)invalid["payload"]["authorization"]["validBefore"]="0100";if(i==7)invalid["payload"]["signature"]="0xgg";
    if(i==8)invalid["x402Version"]=1;auto request=admission;request["payment"]=invalid.dump();Check(Wire(request).contains("packetError"),"untrusted payment mismatch accepted");}
  auto production=prepare;production["quote"]["scope"]["chain"]=8453;Check(Wire(production).contains("packetError"),"mainnet unintentionally enabled");
  return {{"quote",q},{"payment",payment.dump()},{"challenge",challenge},{"verifiedResponse",goodVerify.dump()},{"settledResponse",goodSettle.dump()},
    {"outputCommand",outputCommand},{"verified",verified},{"settling",decision},{"release","0x05"+std::string(62,'0')}};
}
}
int main(int argc,char** argv){
  if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-x402-tests r4334\n";return 0;}
  try{const bool probe=argc==3&&std::string_view(argv[1])=="--probe-rejection";const bool write=argc==3&&std::string_view(argv[1])=="--write-fixtures";
    Check(argc==1||probe||write,"arguments");const auto fixture=Fixture();
    if(write){const std::filesystem::path root(argv[2]);std::filesystem::create_directories(root);std::ofstream out(root/"x402-fixtures.json");out<<Json{{"fixture",fixture},{"vectors",vectors}}.dump();Check(bool(out),"fixture write");}
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
