#include "guard_execution.hpp"
#include "package_internal.hpp"
#include "arken_cloudflare_client_v1/qualification.hpp"
#include <regex>
namespace arken::mcp::economics::cloudflare::v1 {
void detail::guardOutput(const path& p){
 C::Require(p.is_absolute()&&p==p.lexically_normal()&&!C::fs::exists(C::fs::symlink_status(p)),"guard_new_absolute_output_required");
 for(const auto& part:p)C::Require(part!=".tmp"&&part!="tmp","guard_durable_output_required");
}

namespace {std::string quote(std::string_view value){return Json(value).dump();}}
Json detail::RunGuard(std::string_view service,std::string_view mainModule,const std::vector<Module>& modules,
 const ExecutionContext& context,std::string_view endpoint,const path& source,const path& workerd,const path& destination,
 const std::function<void()>& revalidate){
 guardOutput(destination);
 C::Require(std::regex_match(std::string(endpoint),std::regex("https://[a-z0-9.-]+/(v1|latest)")),"guard_endpoint_invalid");
 const auto files=detail::manifest(modules);const auto fingerprint=C::Hash(files.dump());
 std::map<std::string,std::string> contents;
 for(const auto& m:modules)contents.emplace(m.name,m.bytes);
 C::Require(contents.contains(guardOriginalModule)&&contents.contains(guardAdapterModule)&&contents.contains(std::string(mainModule))&&
  contents.at(std::string(mainModule))==Wrapper(std::string(service))&&
  contents.at(guardAdapterModule)==C::Read(source/"capsule/arken_mcp_economics_v1/platform/cloudflare/attempt_gate.js",65536),
  "guard_qualification_structure_invalid");
 C::PrivateDirectory(destination);
 C::RunQualificationProcess(workerd,{"--version"},destination/"workerd-version.log",5);
 const auto version=C::ReadPrivateFile(destination/"workerd-version.log",4096);
 C::Require(version.find("workerd 2026-09-11")!=std::string::npos,"guard_workerd_version_mismatch");
 const auto executableHash=C::Hash(C::Read(workerd,256u*1024u*1024u));
 const auto recipe=C::Read(source/"capsule/arken_mcp_economics_cloudflare_v1/tests/guard_package.js",65536);
 C::Write(destination/"guard_package.js",recipe,true);
 std::string moduleList="(name=\"guard_package.js\",esModule=embed \"guard_package.js\")";
 Json hashes=Json::object();
 for(const auto& m:modules){
  C::Require(m.name!="guard_package.js"&&m.name!="guard_entry.js"&&m.name!="guard_configuration.json","guard_proof_module_collision");
  C::Write(destination/m.name,m.bytes,true);hashes[m.name]=C::Hash(m.bytes);
  moduleList+=",(name="+quote(m.name)+","+(m.mediaType=="application/wasm"?"wasm":m.mediaType=="text/plain"?"text":"esModule")+"=embed "+quote(m.name)+")";
 }
 const auto entry="import * as guarded from './"+std::string(mainModule)+"';\nimport * as original from './mcp_original.js';\nexport {guarded,original};\n";
 C::Write(destination/"guard_entry.js",entry,true);
 moduleList+=",(name=\"guard_entry.js\",esModule=embed \"guard_entry.js\")";
 Json bindings=context.bindings,unavailableBindings=context.unavailableBindings;
 // Team discovery needs an isolated test credential, never the real token.
 const std::string fixtureToken(64,'e');
 if(std::string(service)=="team")bindings["ARKEN_TEAM_TOKEN_SHA256"]="sha256:"+C::Hash(fixtureToken);
 const auto configuration=Json{{"service",std::string(service)},{"endpoint",endpoint},{"bindings",bindings},{"unavailableBindings",unavailableBindings},
  {"fixtureAuthorization",std::string(service)=="team"?"Bearer "+fixtureToken:""}}.dump();
 C::Write(destination/"guard_configuration.json",configuration,true);
 moduleList+=",(name=\"guard_configuration.json\",text=embed \"guard_configuration.json\")";
 std::string flags;
 for(const auto& flag:context.flags){if(!flags.empty())flags+=",";flags+=quote(flag.get<std::string>());}
 const auto config="using Workerd=import \"/workerd/workerd.capnp\";\nconst config:Workerd.Config=(services=[(name=\"guard-test\",worker=(compatibilityDate="+
  quote(context.compatibilityDate)+",compatibilityFlags=["+flags+"],modules=["+moduleList+
  "])),(name=\"internet\",network=(allow=[]))]);\n";
 C::Write(destination/"config.capnp",config,true);
 C::WritePrivateFileExclusive(destination/"intent.json",Json{{"packageSha256",fingerprint},
  {"workerdSha256",executableHash},{"recipeSha256",C::Hash(recipe)},{"configurationSha256",C::Hash(configuration)},{"configSha256",C::Hash(config)}}.dump(2)+"\n");
 C::RunQualificationProcess(workerd,{"test",(destination/"config.capnp").string()},destination/"workerd.log",60);
 const auto log=C::ReadPrivateFile(destination/"workerd.log",1048576);
 const std::string marker="ARKEN_GUARD_PROOF=";const auto start=log.find(marker);
 C::Require(start!=std::string::npos&&log.find(marker,start+marker.size())==std::string::npos,"guard_execution_report_missing");
 const auto end=log.find('\n',start);const auto executed=Json::parse(log.substr(start+marker.size(),end-start-marker.size()));
 C::Require(executed.at("status")=="passed"&&executed.at("admissionBeforeEffects")==true&&executed.at("originalResponseParity")==true&&
  executed.at("namedExportIdentity")==true&&executed.at("discoveryPositiveControl")==true&&executed.at("downstreamEffects")==0&&
  executed.at("service")==std::string(service)&&executed.at("paymentSubmissions")==0,"guard_execution_rejected");
 C::Require(C::Hash(C::Read(workerd,256u*1024u*1024u))==executableHash,"guard_inputs_changed_during_execution");
 for(const auto& m:modules)C::Require(hashes.at(m.name)==C::Hash(C::Read(destination/m.name,C::workerModuleByteLimit)),"guard_inputs_changed_during_execution");
 if(revalidate)revalidate();
 auto proof=executed;proof["schema"]=context.currentSource?"arken.mcp.source-attempt-guard-proof.v1":"arken.mcp.attempt-guard-proof.v1";proof["packageSha256"]=fingerprint;
 if(context.currentSource){
  proof["scope"]="local-qualification-only";proof["deploymentAuthorized"]=false;proof["releaseQualified"]=false;
  proof["ordinaryModulesSha256"]=context.authoritySha256;
 }else proof["predecessorSha256"]=context.authoritySha256;
 proof["workerdSha256"]=executableHash;proof["recipeSha256"]=C::Hash(recipe);proof["configurationSha256"]=C::Hash(configuration);
 proof["externalNetwork"]="denied";proof["endpoint"]=endpoint;proof["cloudMutations"]=0;
 for(const auto& [name,hash]:hashes.items())proof[name]=hash;
 C::WritePrivateFileExclusive(destination/"proof.json",proof.dump(2)+"\n");
 return {{"status","passed"},{"service",std::string(service)},{"proofPath",(destination/"proof.json").string()},
  {"packageSha256",fingerprint},{"modules",hashes.size()},{"cloudMutations",0},{"paymentSubmissions",0}};
}
}
