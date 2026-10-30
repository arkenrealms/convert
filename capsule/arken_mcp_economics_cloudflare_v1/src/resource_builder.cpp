#include "resource_builder.hpp"
#include "package_internal.hpp"
#include <regex>
#include <set>
namespace arken::mcp::economics::cloudflare::v1 {
namespace {
bool identifier(const std::string& value){return std::regex_match(value,std::regex("[A-Za-z_][A-Za-z0-9_]{0,127}"));}
std::string kind(const std::string& type){
 if(type=="application/javascript+module")return "esModule";
 if(type=="application/wasm")return "wasm";
 if(type=="text/plain")return "text";
 if(type=="application/octet-stream")return "data";
 throw std::runtime_error("maintenance_resource_media_type");
}
}
Json detail::PrepareResourceModules(const std::vector<Module>& modules,std::string_view service,std::string_view mainModule,
 const ExecutionContext& context,const std::string& stateContract,const path& guardProof,const path& source,
 const MaintenanceResources& profile,const path& output,const std::function<void()>& revalidate){
 const auto files=detail::manifest(modules);
 const bool stateless=profile.state==MaintenanceState::stateless;
 C::Require(output.is_absolute()&&output==output.lexically_normal()&&!C::fs::exists(C::fs::symlink_status(output)),
  "maintenance_resource_new_output_required");
 path prefix;for(const auto& part:output){prefix/=part;C::Require(part!=".tmp"&&part!="tmp"&&
  !C::fs::is_symlink(C::fs::symlink_status(prefix)),"maintenance_resource_durable_required");}
 std::map<path,std::string> inputs;
 const auto read=[&](const path& input,std::size_t limit){
  C::Require(input.is_absolute()&&C::fs::canonical(input)==input,"maintenance_resource_input_path");
  auto bytes=C::Read(input,limit);inputs[input]=C::Hash(bytes);return bytes;
 };
 const auto guardBytes=read(guardProof,C::workerQualificationByteLimit);
 const auto guard=Json::parse(guardBytes);
 const auto recipe=read(source/"capsule/arken_mcp_economics_cloudflare_v1/tests/guard_package.js",65536);
 C::Require(guard.at("schema")==(context.currentSource?"arken.mcp.source-attempt-guard-proof.v1":"arken.mcp.attempt-guard-proof.v1")&&guard.at("status")=="passed"&&
  guard.at("service")==std::string(service)&&guard.at("packageSha256")==C::Hash(files.dump())&&
  (context.currentSource?(guard.at("ordinaryModulesSha256")==context.authoritySha256&&guard.at("deploymentAuthorized")==false&&guard.at("releaseQualified")==false&&!guard.contains("predecessorSha256")):guard.at("predecessorSha256")==context.authoritySha256)&&guard.at("recipeSha256")==C::Hash(recipe)&&
  guard.at("namedExportIdentity")==true&&guard.at("originalResponseParity")==true&&
  guard.at("admissionBeforeEffects")==true&&guard.at("externalNetwork")=="denied"&&
  guard.at("cloudMutations")==0&&guard.at("paymentSubmissions")==0,"maintenance_resource_guard_required");
 C::Require(identifier(profile.factory)&&!profile.phases.empty()&&profile.phases.size()<=128&&
  (stateless?profile.actors.empty()&&profile.factory=="default":!profile.actors.empty()&&profile.factory!="default")&&
  profile.actors.size()<=16,"maintenance_resource_profile_invalid");
 C::Require(profile.fixtureOutbound.empty()||(identifier(profile.fixtureOutbound)&&profile.fixtureOutbound!="default"),
  "maintenance_resource_outbound_entrypoint_invalid");
 std::map<std::string,Module> contents;Json identity=Json::object();std::size_t total=0;
 const auto add=[&](Module module){
  C::Require(std::regex_match(module.name,std::regex("[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}"))&&
   module.bytes.size()<=C::workerModuleByteLimit,"maintenance_resource_member_invalid");
  if(!module.mediaType.empty())(void)kind(module.mediaType);
  total+=module.bytes.size();C::Require(total<=256u*1024u*1024u&&contents.size()<128,"maintenance_resource_package_limit");
  identity[module.name]=C::Hash(module.bytes);
  C::Require(contents.emplace(module.name,std::move(module)).second,"maintenance_resource_member_collision");
 };
 for(const auto& module:modules){
  C::Require(guard.at(module.name)==C::Hash(module.bytes),"maintenance_resource_guard_module_changed");add(module);
 }
 for(const auto& fixture:profile.fixtures){
  auto bytes=read(fixture.source,C::workerModuleByteLimit);
  C::Require(C::Hash(bytes)==fixture.sha256,"maintenance_resource_fixture_changed");
  add({fixture.name,fixture.mediaType,std::move(bytes)});
 }
 C::Require(contents.contains("resource_profile.js")&&contents.at("resource_profile.js").mediaType=="application/javascript+module",
  "maintenance_resource_entry_missing");
 add({"resource_guard.js","application/javascript+module",read(source/"capsule/arken_mcp_economics_cloudflare_v1/tests/resource_guard.js",65536)});
 add({"resource_subject.js","application/javascript+module",
  "import * as ordinary from './mcp_original.js';\nimport * as guarded from './"+std::string(mainModule)+"';\n"
  "import {resourceSubject} from './resource_guard.js';\nexport * from './"+std::string(mainModule)+"';\n"
  "const subject=resourceSubject(ordinary,guarded,"+Json(profile.factory).dump()+","+Json(std::string(service)).dump()+");\n"
  "export const createResourceWorker=subject.createWorker,createDefaultResourceWorker=subject.createDefaultWorker,resourceGuardEvidence=subject.evidence;\n"});
 std::set<std::string> bindings,classes,keys,phases;std::string bindingText,actorText;
 for(const auto& actor:profile.actors){
  C::Require(identifier(actor.binding)&&identifier(actor.klass)&&identifier(actor.key)&&actor.binding!="ARKEN_MCP_BUDGET"&&
   bindings.insert(actor.binding).second&&classes.insert(actor.klass).second&&keys.insert(actor.key).second,"maintenance_resource_actor_invalid");
  if(!bindingText.empty()){bindingText+=",";actorText+=",";}
  bindingText+="(name="+Json(actor.binding).dump()+",durableObjectNamespace=(className="+Json(actor.klass).dump()+"))";
  actorText+="(className="+Json(actor.klass).dump()+",uniqueKey="+Json(actor.key).dump()+",enableSql=true)";
 }
 for(const auto& [name,value]:profile.textBindings){
  C::Require(identifier(name)&&name!="ARKEN_MCP_BUDGET"&&bindings.insert(name).second&&value.size()<=65536,
   "maintenance_resource_binding_invalid");
  if(!bindingText.empty())bindingText+=",";
  bindingText+="(name="+Json(name).dump()+",text="+Json(value).dump()+")";
 }
 Json steps=Json::array();for(const auto& phase:profile.phases){
  C::Require(identifier(phase)&&phases.insert(phase).second,"maintenance_resource_phase_invalid");
  steps.push_back({{"id",phase},{"path","/"+phase}});
 }
 std::string moduleList="(name=\"resource_profile.js\",esModule=embed \"resource_profile.js\")",flags;
 for(const auto& [name,module]:contents)if(name!="resource_profile.js")
  moduleList+=",(name="+Json(name).dump()+","+kind(module.mediaType)+"=embed "+Json(name).dump()+")";
 for(const auto& flag:context.flags){
  if(!flags.empty())flags+=",";flags+=flag.dump();}
 const auto storage=stateless?std::string{}:",durableObjectNamespaces=["+actorText+"],durableObjectStorage=(localDisk=\"state\")";
 const auto disk=stateless?std::string{}:"(name=\"state\",disk=(path=\"STATE_DIRECTORY\",writable=true)),";
 const auto outbound=profile.fixtureOutbound.empty()?std::string{}:
  ",globalOutbound=(name=\"maintenance-profile\",entrypoint="+Json(profile.fixtureOutbound).dump()+")";
 const std::string config="using Workerd=import \"/workerd/workerd.capnp\";\nconst config:Workerd.Config=(services=[(name=\"maintenance-profile\",worker=(compatibilityDate="+
  Json(context.compatibilityDate).dump()+",compatibilityFlags=["+flags+"],modules=["+moduleList+
  "],bindings=["+bindingText+"]"+storage+outbound+")),"+disk+
  "(name=\"internet\",network=(allow=[]))],sockets=[(name=\"profile\",http=(),service=\"maintenance-profile\")]);\n";
 Json workload{{"schema","arken.codecast.workerd-profile.v1"},{"workerName","maintenance-profile"},
  {"nativeWasmInventory",true},{"maximumAccountedBytes",96u*1024u*1024u},{"steps",steps},
  {"calibrationSteps",Json::array({{{"id","array"},{"kind","arrayBuffer"},{"path","/calibrate-array"},{"expectedBackingIncrease",4194304}},
   {{"id","wasm"},{"kind","wasm"},{"path","/calibrate-wasm"},{"expectedBackingIncrease",8388608}}})}};
 if(stateless)workload["diskState"]=false;
 if(!profile.checkpointObservationPath.empty()){
  C::Require(std::regex_match(profile.checkpointObservationPath,std::regex("/[a-z0-9_-]{1,128}")),"maintenance_resource_observation_path");
  workload["checkpointObservationPath"]=profile.checkpointObservationPath;
 }
 Json composition{{"schema",context.currentSource?"arken.mcp.source-resources.v1":"arken.mcp.maintenance-resources.v1"},{"service",std::string(service)},{"files",files},
  {"packageSha256",C::Hash(files.dump())},
  {"guardProofSha256",C::Hash(guardBytes)},{"factory",profile.factory},{"phases",profile.phases},
  {"productionModules",files.size()},{"qualifiedForDeployment",false},{"cloudMutations",0},{"paymentSubmissions",0}};
 if(context.currentSource){composition["scope"]="local-qualification-only";composition["ordinaryModulesSha256"]=context.authoritySha256;}
 else composition["predecessorSha256"]=context.authoritySha256;
 if(stateless)composition["stateContract"]=stateContract;
 if(!profile.fixtureOutbound.empty())composition["fixtureOutbound"]=profile.fixtureOutbound;
 add({"config.capnp",{},config});add({"recipe.json",{},workload.dump(2)+'\n'});
 add({"resource-composition.json",{},composition.dump(2)+'\n'});
 if(context.currentSource){
  std::string mappings;
  for(const auto& [input,hash]:inputs){
   (void)hash;const auto name=input.string();
   C::Require(name.find_first_of("|\r\n")==name.npos,"source_resource_mapping_path_invalid");
   mappings+=name+"|"+(output/"identity.json").string()+"\n";
  }
  add({"source-inputs.txt",{},std::move(mappings)});
 }
 for(const auto& [input,hash]:inputs)C::Require(C::Hash(C::Read(input,C::workerModuleByteLimit))==hash,"maintenance_resource_input_changed");
 revalidate();
 C::PrivateDirectory(output);for(const auto& [name,module]:contents)
  C::WritePrivateFileExclusive(output/name,module.bytes,C::workerModuleByteLimit);
 identity["releaseId"]=C::Hash(files.dump());C::WritePrivateFileExclusive(output/"identity.json",identity.dump(2)+'\n');
 return {{"status","prepared"},{"package",output.string()},{"productionModules",files.size()},
  {"workloads",profile.phases.size()},{"actors",profile.actors.size()},{"packageSha256",C::Hash(files.dump())},
  {"cloudMutations",0},{"paymentSubmissions",0}};
}
}
