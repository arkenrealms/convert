#include "arken_mcp_economics_cloudflare_v1/source_guard.hpp"
#include <iostream>
namespace E=arken::mcp::economics::cloudflare::v1;
namespace C=arken::cloudflare::v1;
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
  std::cout<<"arken-mcp-source-guard-tests-r6251\n";return 0;
 }
 try {
  C::Require(argc==1,"unexpected_arguments");
  const std::vector<E::Module> modules{{"worker.js","application/javascript+module","export default {};"},
   {"data.txt","text/plain","immutable"}};
  const std::string adapter="export function withAttemptBudget(handler){return handler;}";
  const auto a=E::ComposeSourceGuard("convert","worker.js",modules,adapter);
  C::Require(a.modules.size()==4&&a.service=="convert"&&a.mainModule=="worker.js"&&a.ordinaryModulesSha256.size()==64,"source_composition_shape");
  auto changed=modules;changed[1].bytes="changed";
  C::Require(E::ComposeSourceGuard("convert","worker.js",changed,adapter).ordinaryModulesSha256!=a.ordinaryModulesSha256,"source_identity_not_content_bound");
  const auto reject=[&](auto action,std::string_view expected){bool failed=false;
   try{action();}catch(const std::exception& e){C::Require(e.what()==expected,"unexpected_rejection");failed=true;}
   C::Require(failed,"invalid_source_guard_accepted");};
  reject([&]{E::ComposeSourceGuard("unknown","worker.js",modules,adapter);},"budget_unknown_service");
  reject([&]{E::ComposeSourceGuard("convert","missing.js",modules,adapter);},"budget_main_missing");
  changed=modules;changed.push_back({"mcp_original.js","text/plain","collision"});
  reject([&]{E::ComposeSourceGuard("convert","worker.js",changed,adapter);},"budget_already_wrapped_or_collision");
  auto bad=a;bad.ordinaryModulesSha256=std::string(64,'0');
  reject([&]{E::QualifySourceGuard(bad,{},"https://example.test/v1","/missing","/missing","/missing");},"source_guard_ordinary_identity_changed");
  E::SourceGuardConfiguration config;config.textBindings["ARKEN_MCP_BUDGET"]="not a source binding";
  reject([&]{E::QualifySourceGuard(a,config,"https://example.test/v1","/missing","/missing","/missing");},"source_guard_binding_invalid");
  config={};config.compatibilityFlags={"same","same"};
  reject([&]{E::QualifySourceGuard(a,config,"https://example.test/v1","/missing","/missing","/missing");},"source_guard_flag_invalid");
  std::cout<<"PASS source guard identity, reserved names, canonical family and configuration fences\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
