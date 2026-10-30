#include "source_resources.hpp"
#include "arken_cloudflare_client_v1/qualification.hpp"
namespace arken::conversion::source_package {
namespace C=arken::cloudflare::v1;
E::Json QualifyResources(const E::SourceGuardPackage& package,const E::SourceGuardConfiguration& config,
 const E::path& source,const E::path& startup,const E::path& payments,const E::path& workerd,
 const E::path& profiler,const E::path& output){
 const auto facts=C::Json::parse(C::Read(startup/"resource-fixture.json"));
 const auto maximum=C::Read(startup/"maximum-input.png",C::workerModuleByteLimit);
 C::Require(facts.at("schema")=="arken.convert.resource-fixture.v1"&&facts.at("inputBytes")==8388608&&
  maximum.size()==8388608&&C::Hash(maximum)==facts.at("inputSha256").get<std::string>(),"source_resource_maximum_fixture_invalid");
 const auto access=C::Json::parse(C::Read(payments/"payment-journal-access-fixtures.json"));
 const auto configuration=access.at("seed").at("admission").at("serviceConfiguration").get<std::string>();
 const auto generated=output/"resource-fixtures";C::PrivateDirectory(generated);
 C::Write(generated/"report-configuration.json",configuration,true);
 E::MaintenanceResources profile;profile.factory="createJournalWorker";
 const auto tests=source/"capsule/arken_file_conversion_service_v1/tests";
 const auto add=[&](const char* name,const C::fs::path& file,const char* type){
  profile.fixtures.push_back({name,type,file,C::Hash(C::Read(file,C::workerModuleByteLimit))});
 };
 for(const auto* name:{"resource_profile.js","production_http_tests.js","payment_journal_http.js","r2_bucket_fixture.js"})add(name,tests/name,"application/javascript+module");
 add("report_subject.js",tests/"resource_report_subject.js","application/javascript+module");
 add("sandbox_worker.js",source/"capsule/arken_file_conversion_service_v1/platform/cloudflare/sandbox_worker.js","application/javascript+module");
 for(const auto* name:{"startup-fixtures.json","resource-fixture.json"})add(name,startup/name,"text/plain");
 for(const auto* name:{"jpeg-input.png","maximum-input.png"})add(name,startup/name,"application/octet-stream");
 for(const auto* name:{"payment-journal-access-fixtures.json","payment-journal-actors.json"})add(name,payments/name,"text/plain");
 add("report-configuration.json",generated/"report-configuration.json","text/plain");
 profile.textBindings={{"CONVERT_REPORT_CONFIGURATION",configuration},{"SOURCE_KIND","current-source"}};
 profile.actors={{"LEDGER","Ledger","convert_source_resource_paid"},{"PAYMENT_LEDGER","JournalLedger","convert_source_resource_report"}};
 profile.phases={"production","journal","maximum","retained"};
 E::PrepareSourceGuardResources(package,config,output/"guard/proof.json",source,profile,output/"resources");
 const auto accepted=C::QualifyWorkerResources({output/"resources",workerd,profiler,output/"resource-state"});
 const auto proof=C::Json::parse(C::ReadPrivateFile(output/"resource-state/proof.json",1048576));
 const auto& rows=proof.at("workloadResults");C::Require(rows.size()==4,"source_resource_workload_count");
 std::size_t index=0;
 for(const auto* phase:{"production","journal","maximum","retained"}){
  const auto& row=rows.at(index++);const auto& guard=row.at("result").at("guard");
  C::Require(row.at("id")==phase&&guard.at("service")=="convert"&&guard.at("namedExportIdentity")==true&&
   guard.at("reservations").get<unsigned>()==guard.at("responses").get<unsigned>()+guard.at("thrown").get<unsigned>(),"source_resource_guard_bypassed");
 }
 const auto& result=rows[2].at("result").at("result");const auto& replay=rows[3].at("result").at("result");
 C::Require(result.at("paidHttp")==true&&result.at("inputBytes")==8388608&&result.at("inputFormat")=="png"&&
  result.at("outputFormat")=="jpeg"&&result.at("sha256")==facts.at("outputSha256")&&result.at("outputBytes").get<unsigned>()>0&&
  replay.at("completedReplay")==true&&replay.at("sha256")==result.at("sha256")&&replay.at("outputBytes")==result.at("outputBytes")&&
  replay.at("providerVerifies")==0&&replay.at("providerSettles")==0&&replay.at("outputWrites")==0,"source_resource_conversion_or_replay_failed");
 const auto& journal=rows[1].at("result").at("result");
 C::Require(journal.at("status")=="passed"&&journal.at("storageUnchanged")==true&&journal.at("quotaStorageUnchanged")==true&&
  journal.at("foreignQuotaRejected")==true&&journal.at("reportStorageWrites")==0&&journal.at("financialEffects")==0&&journal.at("networkRequests")==0,
  "source_resource_journal_changed");
 return {{"status","passed"},{"scope","local-qualification-only"},{"resourceReceipt",accepted},
  {"maximumInputBytes",8388608},{"outputSha256",result.at("sha256")},{"replayProved",true},{"journalStorageUnchanged",true},
  {"deploymentAuthorized",false},{"realPurchaseProved",false}};
}
}
