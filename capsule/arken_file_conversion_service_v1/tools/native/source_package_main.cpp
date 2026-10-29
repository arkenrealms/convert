#include "arken_paid_mcp_cloudflare_v1/source_package_files.hpp"
#include <iostream>
namespace C=arken::cloudflare::v1;
namespace P=arken::paid::cloudflare::v1;
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
        std::cout<<"arken-convert-source-package-r6250\n";return 0;
    }
    try {
        C::Require(argc==6&&std::string_view(argv[1])=="--source-package","source_package_arguments_invalid");
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
        std::cout<<P::PrepareSourcePackageFiles("convert","journal_entry.js",root,modules,C::fs::absolute(argv[5])).dump(2)<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
