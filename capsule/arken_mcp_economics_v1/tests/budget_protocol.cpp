#include "arken_mcp_economics_v1/protocol.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <stdexcept>
#include <map>
namespace E=arken::mcp::economics::v1;
using Json=arken::schema::cpp::v1::tool_json::Json;
namespace {
void check(bool ok){if(!ok)throw std::runtime_error("budget protocol invariant");}
Json limits(unsigned count,unsigned reserve=100){return {{"maximumAttempts",std::to_string(count)},{"maximumReservedNanoUsd","10000"},{"attemptReserveNanoUsd",std::to_string(reserve)}};}
Json run(unsigned mode,const Json& p){return Json::parse(E::budgetPacket(mode,p.dump()));}
}
void checkBudgetProtocol(){
 Json p={{"configuration",{{"scope",std::string(64,'a')},{"callerSalt",std::string(64,'b')},{"global",limits(5)},
  {"caller",limits(2)},{"services",{{"math",limits(4)},{"audio",limits(4)}}}}},
  {"now","1000000"},{"service","math"},{"caller","192.0.2.1"},{"enabled",true}};
 p["enabled"]=false;check(run(0,p).contains("packetError"));
 p["states"]=Json::array({nullptr,nullptr,nullptr});check(run(1,p).contains("packetError"));
 p.erase("states");p["enabled"]="true";check(run(0,p).contains("packetError"));p["enabled"]=true;
 const auto keys=run(0,p);check(keys.at("keys").size()==3&&keys.dump().find("192.0.2.1")==std::string::npos);
 std::map<std::string,std::string> store;
 const auto attempt=[&](){auto k=run(0,p);auto input=p;input["states"]=Json::array();
  for(const auto& key:k.at("keys")){const auto i=store.find(key.get<std::string>());input["states"].push_back(i==store.end()?Json(nullptr):Json(i->second));}
  const auto result=run(1,input);for(const auto& write:result.at("writes"))store[write.at("key")]=write.at("value");return result;};
 check(attempt().at("response").at("status")==204); // unpaid challenge
 check(attempt().at("response").at("status")==204); // failed compute
 const auto frozen=store;check(attempt().at("response").at("status")==429&&store==frozen);
 p["caller"]="192.0.2.2";check(attempt().at("response").at("status")==204);
 p["service"]="audio";check(attempt().at("response").at("status")==204);
 p["caller"]="192.0.2.3";check(attempt().at("response").at("status")==204);
 p["caller"]="192.0.2.4";check(attempt().at("response").at("status")==429); // fleet budget
 p["configuration"]["global"]=limits(6);check(attempt().at("response").at("status")==204); // not reset
 p["now"]="999999";check(attempt().at("response").at("status")==503);
 p["now"]="1086400";check(attempt().at("response").at("status")==204); // UTC day
 p["service"]="unknown";check(run(0,p).contains("packetError"));p["service"]="math";
 p["caller"]="";check(run(0,p).contains("packetError"));p["caller"]="2001:db8::1";
 check(!run(0,p).contains("packetError"));
 p["states"]=Json::array({"corrupt",nullptr,nullptr});check(run(1,p).contains("packetError"));
 p.erase("states");p["configuration"]["callerSalt"]="";check(run(0,p).contains("packetError"));
}
