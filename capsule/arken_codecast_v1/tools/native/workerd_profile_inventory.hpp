#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
namespace arken::codecast::proof {
// Explicit fixture isolates are outside the production isolate's budget.
// Default remains exactly one isolate; never silently select the first target.
inline nlohmann::json SelectWorkerdProfileTarget(const nlohmann::json& targets,
    const std::string& worker, const nlohmann::json& fixtures, unsigned port) {
  const auto require=[](bool ok){if(!ok)throw std::runtime_error("worker isolate inventory mismatch");};
  const auto name=[](const std::string& value){return !value.empty()&&value.size()<=128&&value!="."&&value!=".."&&
    value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==value.npos;};
  require(name(worker)&&port>0&&port<65536&&fixtures.is_array()&&fixtures.size()<=16);
  std::set<std::string> expected{worker};
  for(const auto& fixture:fixtures){require(fixture.is_string());const auto value=fixture.get<std::string>();require(name(value)&&expected.insert(value).second);}
  require(targets.is_array()&&targets.size()==expected.size());
  nlohmann::json selected;
  for(const auto& target:targets) {
    require(target.is_object()&&target.at("id").is_string());
    const auto id=target.at("id").get<std::string>();
    require(expected.erase(id)==1&&target.at("type")=="node"&&target.at("description")=="workerd worker"&&
      target.at("webSocketDebuggerUrl")=="ws://127.0.0.1:"+std::to_string(port)+"/"+id);
    if(id==worker)selected=target;
  }
  require(expected.empty()&&!selected.is_null());return selected;
}
}
