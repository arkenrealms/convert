#include "arken_paid_mcp_cloudflare_v1/source_package_files.hpp"
#include <iostream>
namespace C=arken::cloudflare::v1;
namespace P=arken::paid::cloudflare::v1;
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
        std::cout<<"arken-paid-source-package-tests-r6250\n";return 0;
    }
    try {
        C::Require(argc==2,"test_output_required");
        const auto root=C::fs::absolute(argv[1])/C::Random();C::PrivateDirectory(root);
        const auto source=root/"source";C::PrivateDirectory(source);
        C::Write(source/"entry.js","export default {};\n");
        C::Write(root/"service.wasm",std::string("\0asm\1\0\0\0",8));
        std::vector<C::WorkerModule> modules{{"entry.js","application/javascript+module",source/"entry.js"},
            {"service.wasm","application/wasm",root/"service.wasm"}};
        const auto prepare=[&](const auto& input,const C::fs::path& output){return P::PrepareSourcePackageFiles("fixture","entry.js",source,input,output);};
        const auto first=prepare(modules,root/"first"),second=prepare(modules,root/"second");
        C::Require(first==second && first.at("deploymentAuthorized")==false && first.at("runtimeQualified")==false,
            "identity_or_authority_changed");
        C::Require(C::Json::parse(C::Read(root/"first/candidate.json"))==first,"commit_marker_changed");
        std::string expected;
        for(const auto& m:modules)expected+=m.source.string()+"|"+(root/"first"/m.name).string()+"\n";
        C::Require(C::Read(root/"first/source-inputs.txt")==expected,"actual_input_mapping_missing");
        const auto reject=[&](auto action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}C::Require(failed,"invalid_package_accepted");};
        reject([&]{prepare(modules,root/"first");});
        auto duplicate=modules;duplicate.push_back(modules[0]);reject([&]{prepare(duplicate,root/"duplicate");});
        auto reserved=modules;reserved.push_back({"source-inputs.txt","text/plain",source/"entry.js"});
        reject([&]{prepare(reserved,root/"reserved");});
        reject([&]{prepare(modules,root/"bad|path");});
        C::Require(!C::fs::exists(root/"duplicate")&&!C::fs::exists(root/"reserved")&&!C::fs::exists(root/"bad|path"),"rejection_published_output");
        C::fs::create_symlink(source/"entry.js",source/"link.js");
        auto linked=modules;linked[0].source=source/"link.js";reject([&]{prepare(linked,root/"linked");});
        C::Write(source/"entry.js","export default {changed:true};\n");
        C::Require(prepare(modules,root/"changed").at("packageSha256")!=first.at("packageSha256"),"changed_bytes_not_observed");
        C::Write(root/"service.wasm","not wasm");reject([&]{prepare(modules,root/"invalid-wasm");});
        C::Require(!C::fs::exists(root/"invalid-wasm"),"invalid_wasm_published");
        std::cout<<"PASS immutable package, deterministic identity, exact input mappings, reserved/duplicate/path/symlink/Wasm rejection\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
