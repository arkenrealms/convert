#include "resource_qualification_internal.hpp"
#include <cmath>
#include <set>
namespace arken::cloudflare::v1 {
namespace {
void path(const fs::path& p,bool durable=false){
 Require(p.is_absolute()&&p==p.lexically_normal(),"resource_path_invalid");
 fs::path prefix;
 for(const auto& part:p){
  if(durable)Require(part!=".tmp"&&part!="tmp","resource_state_must_be_durable");
  prefix/=part;Require(!fs::is_symlink(fs::symlink_status(prefix)),"resource_symlink_rejected");
 }
}
bool within(const fs::path& parent,const fs::path& child){
 const auto relative=child.lexically_relative(parent);return !relative.empty()&&*relative.begin()!="..";
}
bool filename(std::string_view s){return !s.empty()&&s!="."&&s!=".."&&s.size()<160&&
 s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==s.npos;}
bool digest(std::string_view s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==s.npos;}
std::string hashFile(const fs::path& p){path(p);Require(fs::is_regular_file(p),"resource_regular_file_required");return Hash(Read(p,256u*1024u*1024u));}
Json inventory(const WorkerResourceInput& in,const Json* recorded=nullptr){
 path(in.package,true);path(in.state,true);path(in.workerd);path(in.profiler);
 for(const auto& p:{in.package,in.workerd,in.profiler})
  Require(!within(in.state,p)&&!within(p,in.state),"resource_output_overlaps_input");
 Require(fs::is_directory(in.package),"resource_package_required");
 Json members=Json::object();std::uintmax_t bytes=0;
 for(const auto& entry:fs::directory_iterator(in.package)){
  const auto name=entry.path().filename().string();
  Require(filename(name)&&members.size()<1024&&entry.is_regular_file()&&!entry.is_symlink(),"resource_inventory_invalid");
  bytes+=entry.file_size();Require(bytes<=256u*1024u*1024u,"resource_inventory_too_large");
  members[name]=hashFile(entry.path());
 }
 const auto identity=Json::parse(Read(in.package/"identity.json",1048576));
 Require(identity.is_object()&&identity.size()>2&&digest(identity.at("releaseId").get<std::string>()),"resource_identity_invalid");
 for(const auto& [name,value]:identity.items())if(name!="releaseId"){
  Require(filename(name)&&digest(value.get<std::string>())&&members.at(name)==value,"resource_member_changed");
 }
 for(const auto& name:{"config.capnp","recipe.json"})Require(identity.contains(name),"resource_bootstrap_unfenced");
 const auto recipe=Json::parse(Read(in.package/"recipe.json",1048576));
 Require(recipe.at("schema")=="arken.codecast.workerd-profile.v1"&&recipe.at("steps").is_array()&&
  !recipe.at("steps").empty()&&recipe.at("steps").size()<=128,"resource_recipe_invalid");
 const auto limit=recipe.at("maximumAccountedBytes").get<double>();
 Require(std::isfinite(limit)&&limit>0&&limit<=96u*1024u*1024u,"resource_budget_invalid");
 std::set<std::string> ids;
 for(const auto& step:recipe.at("steps")){
  const auto id=step.at("id").get<std::string>();Require(filename(id)&&ids.insert(id).second,"resource_step_invalid");
  const auto request=step.value("requestBody",std::string{});
  Require(request.empty()||(filename(request)&&identity.contains(request)),"resource_request_unfenced");
 }
 Require(recipe.at("calibrationSteps").is_array()&&recipe.at("calibrationSteps").size()==2,"resource_calibration_invalid");
 std::set<std::string> kinds;
 for(const auto& step:recipe.at("calibrationSteps")){
  const auto kind=step.at("kind").get<std::string>();
  Require((kind=="arrayBuffer"||kind=="wasm")&&kinds.insert(kind).second&&
   filename(step.at("id").get<std::string>()),"resource_calibration_invalid");
 }
 // Historical verification never executes these tools. Their original digests
 // remain part of the accepted identity; qualification always hashes live bytes.
 const auto toolHash=[&](const char* key,const fs::path& file){
  if(!recorded)return hashFile(file);
  const auto value=recorded->at(key).get<std::string>();
  Require(digest(value),"resource_recorded_tool_digest_invalid");return value;
 };
 return {{"contract","arken.cloudflare.resource-qualification.v1"},{"procedureRevision","r5720"},
  {"packageRoot",in.package.string()},{"members",members},{"releaseId",identity.at("releaseId")},
  {"workerd",in.workerd.string()},{"workerdSha256",toolHash("workerdSha256",in.workerd)},
  {"profiler",in.profiler.string()},{"profilerSha256",toolHash("profilerSha256",in.profiler)}};
}
void validate(const Json& proof,const Json& inputs,const Json& recipe){
 Require(proof.at("schema")=="arken.codecast.workerd-resource.v1"&&proof.at("status")=="passed"&&
  proof.at("releaseId")==inputs.at("releaseId")&&proof.at("packageIdentitySha256")==inputs.at("members").at("identity.json")&&
  proof.at("configSha256")==inputs.at("members").at("config.capnp")&&proof.at("recipeSha256")==inputs.at("members").at("recipe.json")&&
  proof.at("executable")==inputs.at("workerd")&&proof.at("executableSha256")==inputs.at("workerdSha256"),"resource_proof_identity_mismatch");
 Require(proof.at("behaviorNeutral")==true&&proof.at("childReaped")==true&&proof.at("externalNetwork")=="denied by fixture",
  "resource_proof_safety_required");
 if(recipe.contains("fixtureWorkers"))Require(proof.at("profiledWorker")==recipe.at("workerName")&&
  proof.at("fixtureWorkers")==recipe.at("fixtureWorkers"),"resource_isolate_scope_changed");
 const double peak=proof.at("maximumAccountedBytes"),wasm=proof.at("maximumWasmBytes"),limit=proof.at("limitBytes");
 Require(std::isfinite(peak)&&std::isfinite(wasm)&&std::isfinite(limit)&&wasm>0&&peak>0&&peak<limit&&
  limit==recipe.at("maximumAccountedBytes").get<double>()&&limit<=96u*1024u*1024u,"resource_proof_budget_rejected");
 const auto& results=proof.at("workloadResults");Require(results.is_array()&&results.size()==recipe.at("steps").size(),"resource_workloads_incomplete");
 for(std::size_t i=0;i<results.size();++i)Require(results[i].at("id")==recipe.at("steps")[i].at("id")&&
  results[i].at("result").at("status")=="passed","resource_workloads_incomplete");
 Require(proof.at("sampleCount").get<unsigned>()>0&&proof.at("calibration").is_array()&&proof.at("calibration").size()==2,"resource_calibration_incomplete");
 for(std::size_t i=0;i<2;++i){
  const auto& requested=recipe.at("calibrationSteps")[i];const auto& result=proof.at("calibration")[i];
  Require(result.at("phase")==requested.at("id").get<std::string>()+"-complete"&&
   result.at("expectedIncrease")==requested.at("expectedBackingIncrease"),"resource_calibration_changed");
  const double observed=result.at("observedIncrease"),expected=result.at("expectedIncrease");
  Require(std::isfinite(observed)&&std::isfinite(expected)&&expected>0,"resource_calibration_invalid");
  const bool included=observed>=expected-65536&&observed<=expected+1048576;
  Require(result.at("includedInBackingStore")==included&&
   (requested.at("kind")=="arrayBuffer"?included:(requested.at("kind")=="wasm"&&(included||std::abs(observed)<=65536))),
   "resource_calibration_rejected");
 }
}
}
Json resource_detail::Qualify(const WorkerResourceInput& in,const Execute& execute){
 const auto inputs=inventory(in);const auto fingerprint=Hash(inputs.dump());
 const auto recipe=Json::parse(Read(in.package/"recipe.json",1048576));
 PrivateDirectory(in.state);Lock lock(in.state/"qualification.lock");
 const auto proofPath=in.state/"proof.json",acceptedPath=in.state/"accepted.json";
 const auto result=[&](const Json& proof,bool reused,const std::string& hash){return Json{
  {"status","passed"},{"reused",reused},{"profileExecutions",reused?0:1},{"inputSha256",fingerprint},
  {"proofPath",proofPath.string()},{"proofSha256",hash},{"releaseId",proof.at("releaseId")},
  {"maximumAccountedBytes",proof.at("maximumAccountedBytes")},{"limitBytes",proof.at("limitBytes")},
  {"cloudMutations",0},{"paymentSubmissions",0}};};
 if(fs::exists(acceptedPath)){
  const auto accepted=Json::parse(ReadPrivateFile(acceptedPath,65536));
  Require(accepted.at("schema")=="arken.cloudflare.resource-qualification-receipt.v1"&&
   accepted.at("inputSha256")==fingerprint,"resource_inputs_changed_use_new_state");
  const auto bytes=ReadPrivateFile(proofPath,1048576);const auto hash=Hash(bytes);
  Require(accepted.at("proofSha256")==hash,"resource_proof_changed");
  const auto proof=Json::parse(bytes);validate(proof,inputs,recipe);
  Require(inventory(in)==inputs,"resource_inputs_changed_during_read");return result(proof,true,hash);
 }
 Require(!fs::exists(in.state/"intent.json")&&!fs::exists(in.state/"profile")&&!fs::exists(proofPath),"resource_incomplete_use_new_state");
 WritePrivateFileExclusive(in.state/"intent.json",Json{{"inputSha256",fingerprint},{"identity",inputs}}.dump(2)+'\n');
 execute(in.profiler,{"--workerd-profile",in.workerd.string(),(in.package/"config.capnp").string(),
  (in.package/"recipe.json").string(),(in.state/"profile").string()},in.state/"profile.log",600);
 Require(inventory(in)==inputs,"resource_inputs_changed_during_execution");
 const auto emitted=in.state/"profile"/"proof.json";path(emitted);
 const auto bytes=Read(emitted,1048576);const auto proof=Json::parse(bytes);validate(proof,inputs,recipe);
 Require(inventory(in)==inputs,"resource_inputs_changed_before_acceptance");
 WritePrivateFileExclusive(proofPath,bytes);const auto hash=Hash(bytes);
 WritePrivateFileExclusive(acceptedPath,Json{{"schema","arken.cloudflare.resource-qualification-receipt.v1"},
  {"inputSha256",fingerprint},{"proofSha256",hash}}.dump(2)+'\n');return result(proof,false,hash);
}
Json QualifyWorkerResources(const WorkerResourceInput& in){return resource_detail::Qualify(in,RunQualificationProcess);}
Json VerifyWorkerResourceQualification(const WorkerResourceInput& in){
 path(in.state,true);
 Require(fs::is_regular_file(in.state/"accepted.json"),"resource_accepted_receipt_required");
 Lock lock(in.state/"qualification.lock");
 const auto intentBytes=ReadPrivateFile(in.state/"intent.json",1048576);
 const auto acceptedBytes=ReadPrivateFile(in.state/"accepted.json",65536);
 const auto intent=Json::parse(intentBytes),accepted=Json::parse(acceptedBytes);
 const auto& inputs=intent.at("identity");const auto fingerprint=Hash(inputs.dump());
 Require(accepted.at("schema")=="arken.cloudflare.resource-qualification-receipt.v1"&&
  intent.at("inputSha256")==fingerprint&&accepted.at("inputSha256")==fingerprint,
  "resource_accepted_identity_mismatch");
 Require(inventory(in,&inputs)==inputs,"resource_accepted_package_or_paths_changed");
 const auto proofPath=in.state/"proof.json";
 const auto proofBytes=ReadPrivateFile(proofPath,1048576),proofHash=Hash(proofBytes);
 Require(accepted.at("proofSha256")==proofHash,"resource_proof_changed");
 const auto proof=Json::parse(proofBytes),recipe=Json::parse(Read(in.package/"recipe.json",1048576));
 validate(proof,inputs,recipe);
 Require(inventory(in,&inputs)==inputs&&ReadPrivateFile(in.state/"intent.json",1048576)==intentBytes&&
  ReadPrivateFile(in.state/"accepted.json",65536)==acceptedBytes&&ReadPrivateFile(proofPath,1048576)==proofBytes,
  "resource_accepted_evidence_changed_during_read");
 return {{"status","passed"},{"reused",true},{"profileExecutions",0},{"inputSha256",fingerprint},
  {"proofPath",proofPath.string()},{"proofSha256",proofHash},{"releaseId",proof.at("releaseId")},
  {"maximumAccountedBytes",proof.at("maximumAccountedBytes")},{"limitBytes",proof.at("limitBytes")},
  {"cloudMutations",0},{"paymentSubmissions",0}};
}
}
