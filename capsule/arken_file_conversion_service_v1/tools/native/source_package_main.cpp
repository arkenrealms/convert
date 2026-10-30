#include "arken_paid_mcp_cloudflare_v1/source_package_files.hpp"
#include "arken_mcp_economics_cloudflare_v1/source_guard.hpp"
#include "source_resources.hpp"
#include <iostream>
namespace C=arken::cloudflare::v1;
namespace P=arken::paid::cloudflare::v1;
namespace E=arken::mcp::economics::cloudflare::v1;
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
        std::cout<<"arken-convert-source-package-r6252\n";return 0;
    }
    try {
        const bool resources=argc==10&&std::string_view(argv[1])=="--qualify-source-resources";
        const bool guard=resources||(argc==7&&std::string_view(argv[1])=="--qualify-source-guard");
        C::Require(guard||(argc==6&&std::string_view(argv[1])=="--source-package"),"source_package_arguments_invalid");
        const auto root=C::fs::canonical(argv[2]);
        const auto family=root/"capsule/arken_file_conversion_service_v1/platform/cloudflare";
        std::vector<C::WorkerModule> modules;
        const auto script=[&](const C::fs::path& file){modules.push_back({file.filename().string(),"application/javascript+module",file});};
        for(const auto* name:{"journal_entry.js","production_worker.js","runtime.js","worker.js","paid_conversion.js",
            "payment_ledger.js","x402_facilitator.js","settlement_reconciliation.js","public_admission.js","production_startup.js",
            "journal_worker.js","journal_ledger.js","journal_report_runtime.js"})script(family/name);
        for(const auto* name:{"cdp_authorization.js","bounded_body.js"})script(root/"capsule/arken_cdp_transport_v1/worker"/name);
        for(const auto* name:{"report_runtime.js","report_http_transport.js"})script(root/"capsule/arken_paid_operation_v1/platform/cloudflare"/name);
        modules.push_back({"conversion.wasm","application/wasm",C::fs::canonical(argv[3])});
        modules.push_back({"conversion-journal-report.wasm","application/wasm",C::fs::canonical(argv[4])});
        modules.push_back({"NOTICE.txt","text/plain",family/"NOTICE.txt",false});
        if(!guard){std::cout<<P::PrepareSourcePackageFiles("convert","journal_entry.js",root,modules,C::fs::absolute(argv[5])).dump(2)<<'\n';return 0;}
        const auto output=C::fs::absolute(argv[resources?9:6]);
        C::Require(output.is_absolute()&&output==output.lexically_normal()&&!C::fs::exists(C::fs::symlink_status(output)),"source_guard_new_output_required");
        for(const auto& part:output)C::Require(part!=".tmp"&&part!="tmp","guard_durable_output_required");
        C::PrivateDirectory(output);
        const auto original=output/"ordinary";
        const auto package=P::PrepareSourcePackageFiles("convert","journal_entry.js",root,modules,original);
        std::vector<E::Module> ordinary;
        for(const auto& row:package.at("files")){
            const auto name=row.at("name").get<std::string>();const auto bytes=C::Read(original/name,C::workerModuleByteLimit);
            C::Require(C::Hash(bytes)==row.at("sha256").get<std::string>()&&bytes.size()==row.at("bytes").get<std::size_t>(),"source_guard_ordinary_file_changed");
            ordinary.push_back({name,row.at("mediaType"),bytes});
        }
        const auto adapter=root/"capsule/arken_mcp_economics_v1/platform/cloudflare/attempt_gate.js";
        const auto wrapped=E::ComposeSourceGuard("convert","journal_entry.js",ordinary,C::Read(adapter,65536));
        E::SourceGuardConfiguration config;
        config.textBindings={{"MCP_ENDPOINT_PATH","/v1"},{"MCP_PUBLIC_ORIGIN","https://convert.mcp.arken.gg"},{"PAYMENTS_ENABLED","false"}};
        config.unavailableBindings={"PAYMENT_LEDGER","FILES"};
        auto result=E::QualifySourceGuard(wrapped,config,"https://convert.mcp.arken.gg/v1",root,C::fs::canonical(argv[5]),output/"guard");
        const auto proof=C::Json::parse(C::Read(output/"guard/proof.json",1048576));
        C::Require(proof.at("schema")=="arken.mcp.source-attempt-guard-proof.v1"&&!proof.contains("predecessorSha256")&&
            proof.at("deploymentAuthorized")==false&&proof.at("releaseQualified")==false,"source_guard_authority_changed");
        const auto recipe=root/"capsule/arken_mcp_economics_cloudflare_v1/tests/guard_package.js";
        auto mappings=C::Read(original/"source-inputs.txt");
        for(const auto& file:{adapter,recipe})C::Require(file.string().find_first_of("|\r\n")==std::string::npos,"source_guard_mapping_path_invalid");
        mappings+=adapter.string()+"|"+(output/"guard/mcp_attempt_gate.js").string()+"\n";
        mappings+=recipe.string()+"|"+(output/"guard/guard_package.js").string()+"\n";
        if(resources){
            result["resources"]=arken::conversion::source_package::QualifyResources(wrapped,config,root,C::fs::canonical(argv[7]),
                C::fs::canonical(argv[8]),C::fs::canonical(argv[5]),C::fs::canonical(argv[6]),output);
            mappings+=C::Read(output/"resources/source-inputs.txt",1048576);
        }
        C::Write(output/"source-inputs.txt",mappings,true);
        result["sourcePackageSha256"]=package.at("packageSha256");
        C::Write(output/"result.json",result.dump(2)+"\n",true);
        std::cout<<result.dump(2)<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
