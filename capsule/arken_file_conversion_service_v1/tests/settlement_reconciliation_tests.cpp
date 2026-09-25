#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "../tools/native/platform.hpp"
#include "../src/payment_json.hpp"
#include <chrono>
#include <iostream>
namespace P=arken::file_conversion::service::v1::payment;
namespace C=arken::conversion::cloudflare;
namespace J=arken::schema::cpp::v1::tool_json;
using Json=nlohmann::json;
namespace {
unsigned checks=0;Json vectors=Json::array();
void Check(bool value,const char* label){++checks;C::Require(value,label);}
Json Run(Json input){auto text=input.dump(),output=P::SettlementReconciliationPacket(text);vectors.push_back({{"input",text},{"expected",output}});return Json::parse(output);}
P::Address Address(std::string_view text){const auto id=J::read<arken::schema::cpp::v1::fixed_id<20,1>>(std::string(text));P::Address a{};for(unsigned i=0;i<20;++i)a[i]=std::to_integer<std::uint8_t>(id.value[i]);return a;}
Json Reply(unsigned id,Json result){return {{"jsonrpc","2.0"},{"id",id},{"result",result}};}
std::string Hash(char c){return "0x"+std::string(64,c);}
Json Fixture(bool mainnet=false){
  P::Operation op;auto& q=op.quote;q.operation[0]=51;q.tenant[0]=2;q.input[0]=3;q.options[0]=4;q.release[0]=5;q.priceVersion[0]=6;
  const auto profile=P::PaymentProfileById(mainnet?"base-mainnet-v1":"base-sepolia-v1");
  q.scope.chain=profile->chain;q.scope.asset=profile->asset;q.scope.payer.fill(0x11);
  q.recipient=Address("554933cd7c5275ff3692bf97e0bc90b69c85d3dc");q.amount=10000;q.expiresAt=200;
  op.stage=P::Stage::settlementUnknown;op.revision=6;op.authorization=P::Authorization{.validAfter=90,.validBefore=150};op.authorization->nonce.fill(0x22);
  op.output=P::Output{.release=q.release,.bytes=70,.width=1,.height=1};op.output->digest[0]=8;
  const auto operation=Json::parse(P::InputPacket(op,q,{},{})).at("operation");
  const std::string asset(profile->assetText),payer="0x"+std::string(24,'0')+std::string(40,'1');
  Json event={{"address",asset},{"removed",false},{"transactionHash",Hash('a')},{"blockHash",Hash('b')},{"blockNumber","0xf0"},
    {"transactionIndex","0x1"},{"logIndex","0x2"},{"data","0x"},
    {"topics",Json::array({"0x98de503528ee59b575ef0c0a2576a82497bfc029a5685b209e9ec333479b10a5",payer,Hash('2')})}};
  auto transfer=event;transfer["logIndex"]="0x3";transfer["data"]="0x"+std::string(60,'0')+"2710";
  transfer["topics"]=Json::array({"0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",payer,"0x000000000000000000000000554933cd7c5275ff3692bf97e0bc90b69c85d3dc"});
  Json anchor={{"number","0x100"},{"timestamp","0x82"},{"hash",Hash('c')}};
  Json block={{"number","0xf0"},{"timestamp","0x6e"},{"hash",Hash('b')}};
  Json receipt={{"status","0x1"},{"transactionHash",Hash('a')},{"blockHash",Hash('b')},{"blockNumber","0xf0"},{"transactionIndex","0x1"},{"logs",Json::array({event,transfer})}};
  return {{"operation",operation},{"anchor",Json::array({Reply(2,anchor),Reply(1,mainnet?"0x2105":"0x14a34")}).dump()},
    {"logs",Json::array({Reply(3,Json::array({event}))}).dump()},{"receipt",Json::array({Reply(6,anchor),Reply(4,receipt),Reply(5,block)}).dump()},{"now","140"},
    {"rpcClientKey",mainnet?Json("fixture_cdp_client_1234"):Json(nullptr)}};
}
Json Scenario(bool mainnet=false){
  const auto target=P::detail::ReadOperation(Fixture(mainnet).at("operation"));P::Operation op{.quote=target.quote};std::optional<P::NonceClaim> claim;Json commands=Json::array();
  for(const auto kind:{P::EventKind::request,P::EventKind::verificationPassed,P::EventKind::beginExecution,P::EventKind::outputCommitted,P::EventKind::beginSettlement}){
    P::Event e;e.kind=kind;e.expectedRevision=op.revision;e.now=100;
    if(kind==P::EventKind::request)e.authorization=target.authorization;
    if(kind==P::EventKind::verificationPassed)e.verifiedPayer=op.quote.scope.payer;
    if(kind==P::EventKind::beginExecution)e.executingRelease=op.quote.release;
    if(kind==P::EventKind::outputCommitted)e.output=target.output;
    commands.push_back(Json{{"version",1},{"quote",P::detail::WriteQuote(op.quote)},{"event",P::detail::WriteEvent(e)}}.dump());
    const auto decision=P::Advance(op,op.quote,claim,e);Check(decision.error==P::Error::none,"reconciliation fixture transition");op=decision.operation;claim=decision.nonceClaim;
  }
  P::Event inspect;inspect.kind=P::EventKind::inspect;inspect.now=140;
  return {{"commands",commands},{"inspect",Json{{"version",1},{"quote",P::detail::WriteQuote(op.quote)},{"event",P::detail::WriteEvent(inspect)}}.dump()}};
}
void Tests(bool mainnet=false){
  const auto profile=P::PaymentProfileById(mainnet?"base-mainnet-v1":"base-sepolia-v1");
  const auto valid=Fixture(mainnet);auto input=valid;input["anchor"]=nullptr;input["logs"]=nullptr;input["receipt"]=nullptr;
  auto step=Run(input);Check(step.at("kind")=="rpc"&&step.at("slot")=="anchor","initial RPC");
  const auto endpoint=mainnet?"https://api.developer.coinbase.com/rpc/v1/base/fixture_cdp_client_1234":"https://sepolia.base.org";
  Check(step.at("request").at("url")==endpoint,"RPC endpoint not fixed");
  input["anchor"]=valid.at("anchor");step=Run(input);Check(step.at("slot")=="logs","log discovery");
  const auto filter=Json::parse(step.at("request").at("body").get<std::string>())[0].at("params")[0];
  Check(filter.at("address")==profile->assetText&&filter.at("topics").size()==3&&filter.at("fromBlock")=="0x0","search not bounded and scoped");
  input["logs"]=valid.at("logs");step=Run(input);Check(step.at("slot")=="receipt","receipt discovery");
  step=Run(valid);Check(step.at("kind")=="complete"&&step.at("commands").size()==1,"positive reconciliation");
  Check(step.at("evidence").at("chain")==profile->chain&&step.at("evidence").at("profileId")==profile->id,"recovery evidence wrong profile");
  Check(step.dump().find("fixture_cdp_client_1234")==std::string::npos,"RPC account key leaked into completion");
  auto command=Json::parse(step.at("commands")[0].get<std::string>());
  Check(command.at("event").at("kind")==unsigned(P::EventKind::reconciledSucceeded)&&command.at("event").at("expectedRevision")=="6","wrong completion transition");
  Check(command.at("event").at("transfer").at("amount")=="10000"&&command.at("event").at("transfer").at("transaction")==std::string(64,'a'),"unvalidated transfer values");
  auto settling=valid;settling["operation"]["stage"]=unsigned(P::Stage::settling);step=Run(settling);
  Check(step.at("commands").size()==2&&Json::parse(step.at("commands")[0].get<std::string>()).at("event").at("kind")==unsigned(P::EventKind::settlementUncertain)
    &&Json::parse(step.at("commands")[1].get<std::string>()).at("event").at("expectedRevision")=="7","stranded settling transition");
  const auto reject=[&](Json bad){const auto result=Run(bad);Check(result.at("kind")=="pending"&&!result.contains("commands"),"unproven payment completed");};
  for(unsigned field=0;field<23;++field){auto bad=valid;auto receipts=Json::parse(bad.at("receipt").get<std::string>());auto& receipt=receipts[1]["result"];auto& event=receipt["logs"][0];auto& transfer=receipt["logs"][1];
    if(field==0)receipt["status"]="0x0";if(field==1)receipt["transactionHash"]=Hash('d');if(field==2)receipt["blockHash"]=Hash('d');
    if(field==3)receipt["blockNumber"]="0xef";if(field==4)receipt["transactionIndex"]="0x2";
    if(field==5)event["removed"]=true;if(field==6)event["topics"][1]=Hash('1');if(field==7)event["topics"][2]=Hash('3');
    if(field==8)event["address"]="0x"+std::string(40,'9');if(field==9)event["logIndex"]="0x1";
    if(field==10)transfer["topics"][2]=Hash('3');if(field==11)transfer["data"]="0x"+std::string(60,'0')+"2711";
    if(field==12)transfer["data"]=Hash('f');if(field==13)transfer["logIndex"]="0x2";
    if(field==14)receipt["logs"].push_back(transfer);if(field==15)receipt["logs"].erase(1);
    if(field==16)receipts[0]["result"]["hash"]=Hash('e');if(field==17)receipts[2]["result"]["hash"]=Hash('e');
    if(field==18)receipts[2]["result"]["timestamp"]="0x5a";if(field==19)receipts[2]["result"]["timestamp"]="0x96";
    if(field==20)receipt["blockNumber"]="0x0f0";if(field==21)receipt["blockNumber"]="0x10000000000000000";
    if(field==22)receipts[2]["id"]=4;
    bad["receipt"]=receipts.dump();reject(bad);
  }
  for(unsigned field=0;field<7;++field){auto bad=valid;auto logs=Json::parse(bad.at("logs").get<std::string>());auto& event=logs[0]["result"][0];
    if(field==0)logs[0]["result"]=Json::array();if(field==1)logs[0]["result"].push_back(event);
    if(field==2)event["removed"]=true;if(field==3)event["topics"][2]=Hash('3');if(field==4)event["blockNumber"]="0x101";
    if(field==5)event["data"]="0x01";if(field==6)event["transactionHash"]=Hash('0');bad["logs"]=logs.dump();reject(bad);}
  for(unsigned field=0;field<5;++field){auto bad=valid;auto anchor=Json::parse(bad.at("anchor").get<std::string>());
    if(field==0)anchor[1]["result"]="0x1";if(field==1)anchor[0]["result"]=nullptr;if(field==2)anchor[0]["result"]["timestamp"]="0xff";
    if(field==3)anchor[0]["result"]["hash"]=Hash('0');if(field==4){anchor[1].erase("result");anchor[1]["error"]={{"code",-32000}};}bad["anchor"]=anchor.dump();reject(bad);}
  auto bad=valid;bad["receipt"]=std::string(262145,' ');reject(bad);
  bad=valid;bad["operation"]["stage"]=unsigned(P::Stage::computing);reject(bad);
  bad=settling;bad["operation"]["revision"]="18446744073709551614";reject(bad);
  bad=valid;auto receipts=Json::parse(bad["receipt"].get<std::string>());receipts[1]["result"]=nullptr;bad["receipt"]=receipts.dump();reject(bad);
  const auto duplicate="{\"now\":\"140\","+valid.dump().substr(1);Check(Json::parse(P::SettlementReconciliationPacket(duplicate)).at("kind")=="pending","duplicate packet field");
  auto large=valid;auto headers=Json::parse(large["anchor"].get<std::string>());headers[0]["result"]["transactions"]=Json::array();
  for(unsigned i=0;i<1000;++i)headers[0]["result"]["transactions"].push_back(Hash('f'));large["anchor"]=headers.dump();
  Check(Run(large).at("kind")=="complete","production-sized irrelevant block fields broke proof");
  for(const Json& key:{Json(nullptr),Json(""),Json("https://foreign.invalid"),Json("a/b"),Json(".."),Json("a?b"),Json("a#b"),Json("bad key"),Json(std::string(257,'a')),Json(12)}){
    if(!mainnet&&key.is_null())continue;
    auto wrong=valid;wrong["rpcClientKey"]=key;const auto outcome=Run(wrong);
    Check(outcome.at("kind")=="pending"&&!outcome.contains("request")&&!outcome.contains("commands"),"RPC configuration bypassed by supplied evidence");
  }
  if(mainnet){auto missing=input;missing["anchor"]=nullptr;missing["rpcClientKey"]=nullptr;
    Check(Run(missing)==Json{{"kind","pending"},{"code","mainnet_reconciliation_not_configured"}},"missing RPC key not explicit");}
  else {auto wrong=valid;wrong["rpcClientKey"]="fixture_cdp_client_1234";
    Check(Run(wrong)==Json{{"kind","pending"},{"code","testnet_rpc_configuration_rejected"}},"testnet ignored mainnet key");}
}
Json DirectFixture(bool mainnet=false){
  const auto old=Fixture(mainnet);const auto& op=old.at("operation");const auto& q=op.at("quote");const auto& auth=op.at("authorization");
  const auto receipts=Json::parse(old.at("receipt").get<std::string>());
  return {{"expected",{{"profileId",mainnet?"base-mainnet-v1":"base-sepolia-v1"},{"payer","0x"+q.at("scope").at("payer").get<std::string>()},
    {"recipient","0x"+q.at("recipient").get<std::string>()},{"amount",q.at("amount")},{"nonce","0x"+auth.at("nonce").get<std::string>()},
    {"validAfter",auth.at("validAfter")},{"validBefore",auth.at("validBefore")},{"transaction",Hash('a')}}},
    {"anchor",old.at("anchor")},{"receipt",Json::array({Reply(3,receipts[1].at("result"))}).dump()},
    {"blocks",Json::array({Reply(5,receipts[0].at("result")),Reply(4,receipts[2].at("result"))}).dump()},
    {"now","140"},{"rpcClientKey",old.at("rpcClientKey")}};
}
void DirectTests(bool mainnet){
  const auto run=[](const Json& j){return Json::parse(P::PaymentTransferEvidencePacket(j.dump()));};const auto valid=DirectFixture(mainnet);
  auto next=valid;next["anchor"]=nullptr;next["receipt"]=nullptr;next["blocks"]=nullptr;
  Check(run(next).at("slot")=="anchor","direct anchor request");next["anchor"]=valid.at("anchor");
  auto step=run(next);Check(step.at("slot")=="receipt"&&Json::parse(step.at("request").at("body").get<std::string>())[0].at("method")=="eth_getTransactionReceipt","direct exact receipt request");
  next["receipt"]=valid.at("receipt");Check(run(next).at("slot")=="blocks","direct block request");
  step=run(valid);Check(step.at("kind")=="verified"&&!step.contains("commands")&&step.at("paymentRequests")==0,"direct verifier emitted mutation");
  Check(step.at("evidence").at("finality")=="rpc-safe-not-irreversible"&&step.at("evidence").at("transaction")==Hash('a'),"direct evidence scope");
  auto historic=valid;auto anchor=Json::parse(historic.at("anchor").get<std::string>()),blocks=Json::parse(historic.at("blocks").get<std::string>());
  anchor[0]["result"]["number"]="0x10000";blocks[0]["result"]["number"]="0x10000";historic["anchor"]=anchor.dump();historic["blocks"]=blocks.dump();
  Check(run(historic).at("kind")=="verified","historic receipt incorrectly needs recent log window");
  for(unsigned field=0;field<20;++field){auto bad=valid;auto r=Json::parse(bad.at("receipt").get<std::string>());auto& receipt=r[0]["result"];auto& event=receipt["logs"][0];auto& transfer=receipt["logs"][1];
    if(field==0)receipt["status"]="0x0";if(field==1)receipt["transactionHash"]=Hash('d');if(field==2)receipt["blockHash"]=Hash('d');
    if(field==3)receipt["blockNumber"]="0x101";if(field==4)receipt["transactionIndex"]="0x2";
    if(field==5)event["removed"]=true;if(field==6)event["topics"][1]=Hash('1');if(field==7)event["topics"][2]=Hash('3');
    if(field==8)event["address"]="0x"+std::string(40,'9');if(field==9)event["data"]="0x01";
    if(field==10)transfer["topics"][2]=Hash('3');if(field==11)transfer["data"]="0x"+std::string(60,'0')+"2711";
    if(field==12)transfer["data"]=Hash('f');if(field==13)transfer["logIndex"]="0x2";
    if(field==14)receipt["logs"].push_back(transfer);if(field==15)receipt["logs"].erase(1);
    if(field==16)receipt["logs"].push_back(event);if(field==17)receipt["logs"]=Json::array();
    if(field==18)receipt=nullptr;if(field==19)r[0]["id"]=4;
    bad["receipt"]=r.dump();step=run(bad);Check(step.at("kind")=="pending"&&!step.contains("commands"),"direct bad receipt verified");
  }
  for(unsigned field=0;field<10;++field){auto bad=valid;
    if(field==0)bad["expected"]["transaction"]=Hash('d');if(field==1)bad["expected"]["recipient"]="0x"+std::string(40,'1');
    if(field==2)bad["expected"]["amount"]="1";if(field==3)bad["expected"]["nonce"]=Hash('3');if(field==4)bad["expected"]["validAfter"]="110";
    if(field==5)bad["expected"]["validBefore"]="110";if(field==6)bad["rpcClientKey"]=mainnet?Json(nullptr):Json("not-allowed");
    if(field==7)bad["now"]="1";
    if(field==8||field==9){auto b=Json::parse(bad.at("blocks").get<std::string>());b[field==8?0:1]["result"]["hash"]=Hash('e');bad["blocks"]=b.dump();}
    Check(run(bad).at("kind")=="pending","direct bad expectation verified");
  }
}
void Probe(const C::fs::path& state,const C::fs::path& clientKeyFile={}){C::PrivateDirectory(state);C::Lock lock(state/"probe.lock");C::Require(!C::fs::exists(state/"proof.json"),"probe_already_recorded");
  const bool mainnet=!clientKeyFile.empty();auto input=Fixture(mainnet);
  if(mainnet){const auto configuration=P::detail::Parse(C::Read(clientKeyFile,2048,true));P::detail::Fields(configuration,{"rpcClientKey"});input["rpcClientKey"]=configuration.at("rpcClientKey");}
  input["anchor"]=nullptr;input["logs"]=nullptr;input["receipt"]=nullptr;input["operation"]["authorization"]["nonce"]=C::Random();
  input["now"]=std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  const auto send=C::CurlTransport(state/"http");Json probes=Json::array();
  for(unsigned i=0;i<4;++i){const auto step=Json::parse(P::SettlementReconciliationPacket(input.dump()));
    if(step.at("kind")!="rpc"){C::Require(step.at("kind")=="pending"&&step.at("code")=="settlement_not_observed","chain_probe_not_accepted");
      const Json proof={{"status","passed"},{"kind","read-only-chain-probe"},{"profileId",mainnet?"base-mainnet-v1":"base-sepolia-v1"},{"requests",probes},{"result",step},{"ledgerMutations",0},{"settlementCalls",0},{"recoveredPayment",false}};
      C::Write(state/"proof.json",proof.dump(2)+"\n",true);std::cout<<proof.dump(2)<<'\n';return;}
    const auto& request=step.at("request");const auto response=send({"POST",request.at("url"),request.at("body"),"application/json",{}});
    C::Require(response.status==200&&response.body.size()<=262144,"chain_probe_http_unavailable");const auto slot=step.at("slot").get<std::string>();
    C::Write(state/(slot+".json"),response.body,true);input[slot]=response.body;
    probes.push_back({{"slot",slot},{"status",response.status},{"bytes",response.body.size()},{"sha256",C::Hash(response.body)}});
  }
  throw std::runtime_error("chain_probe_step_limit");
}
}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-reconciliation-tests r4389\n";return 0;}
  if(argc>1&&std::string_view(argv[1])=="--help"){std::cout<<"--write-fixtures OUTPUT | --probe-chain UNUSED_OUTPUT | --probe-mainnet CLIENT_KEY_JSON UNUSED_OUTPUT\nClient key file: owner-only JSON with rpcClientKey. Probe is read-only, never a recovered-payment proof.\n";return 0;}
  try{if(argc==3&&std::string_view(argv[1])=="--probe-chain"){Probe(C::fs::absolute(argv[2]));return 0;}
    if(argc==4&&std::string_view(argv[1])=="--probe-mainnet"){Probe(C::fs::absolute(argv[3]),C::fs::absolute(argv[2]));return 0;}
    Check(argc==1||(argc==3&&std::string_view(argv[1])=="--write-fixtures"),"invalid arguments");Tests();Tests(true);DirectTests(false);DirectTests(true);
    if(argc==3){const auto path=C::fs::absolute(argv[2]);C::PrivateDirectory(path);C::Write(path/"reconciliation-fixtures.json",Json{{"fixture",Fixture()},{"scenario",Scenario()},{"mainnetFixture",Fixture(true)},{"mainnetScenario",Scenario(true)},{"vectors",vectors}}.dump()+"\n");
      C::Write(path/"direct-receipt-fixtures.json",Json{{"sandbox",DirectFixture()},{"production",DirectFixture(true)}}.dump()+"\n");
      C::Write(path/"invalid-client-key.json","{\"rpcClientKey\":\"private_key_echo_marker");}
    std::cout<<checks<<" settlement reconciliation checks passed\n";return 0;
  }catch(const std::exception& e){const std::string message=e.what();
    const auto code=message.size()<=128&&message.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789")==std::string::npos?message:"reconciliation_command_failed";
    std::cerr<<Json{{"status","failed"},{"code",code}}.dump()<<'\n';return 1;}}
