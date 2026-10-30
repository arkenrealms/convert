#include "guard_execution.hpp"
#include "package_internal.hpp"
#include "resource_builder.hpp"
#include <regex>
#include <set>
namespace arken::mcp::economics::cloudflare::v1 {
detail::ExecutionContext detail::SourceContext(const SourceGuardPackage& package,const SourceGuardConfiguration& config){
 detail::validateService(package.service);
 C::Require(std::regex_match(config.compatibilityDate,std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}"))&&
  config.compatibilityFlags.size()<=32&&config.textBindings.size()<=32&&config.unavailableBindings.size()<=32,
  "source_guard_configuration_invalid");
 std::set<std::string> names,flags;
 for(const auto& flag:config.compatibilityFlags)C::Require(std::regex_match(flag,std::regex("[a-z][a-z0-9_]{0,63}"))&&flags.insert(flag).second,"source_guard_flag_invalid");
 const auto binding=[&](const std::string& name){C::Require(std::regex_match(name,std::regex("[A-Z][A-Z0-9_]{0,127}"))&&
  name!="ARKEN_MCP_BUDGET"&&names.insert(name).second,"source_guard_binding_invalid");};
 for(const auto& [name,value]:config.textBindings){binding(name);C::Require(value.size()<=16384,"source_guard_binding_limit");}
 for(const auto& name:config.unavailableBindings)binding(name);
 std::vector<Module> ordinary;
 for(const auto& m:package.modules){
  if(m.name==guardAdapterModule||m.name==package.mainModule)continue;
  auto original=m;if(original.name==guardOriginalModule)original.name=package.mainModule;
  ordinary.push_back(std::move(original));
 }
 C::Require(C::Hash(detail::manifest(ordinary).dump())==package.ordinaryModulesSha256,"source_guard_ordinary_identity_changed");
 detail::ExecutionContext context;
 context.currentSource=true;context.authoritySha256=package.ordinaryModulesSha256;
 context.compatibilityDate=config.compatibilityDate;context.flags=config.compatibilityFlags;
 context.bindings=config.textBindings;context.unavailableBindings=config.unavailableBindings;
 return context;
}
Json QualifySourceGuard(const SourceGuardPackage& package,const SourceGuardConfiguration& config,
 std::string_view endpoint,const path& source,const path& workerd,const path& output){
 const auto context=detail::SourceContext(package,config);
 auto result=detail::RunGuard(package.service,package.mainModule,package.modules,context,endpoint,source,workerd,output);
 result["scope"]="local-qualification-only";result["deploymentAuthorized"]=false;result["releaseQualified"]=false;
 return result;
}
Json PrepareSourceGuardResources(const SourceGuardPackage& package,const SourceGuardConfiguration& config,
 const path& guardProof,const path& source,const MaintenanceResources& profile,const path& output){
 const auto context=detail::SourceContext(package,config);
 C::Require(profile.state==MaintenanceState::financialJournal||profile.state==MaintenanceState::stateless,"source_resource_state_invalid");
 return detail::PrepareResourceModules(package.modules,package.service,package.mainModule,context,
  "arken.mcp.source-stateless.v1",guardProof,source,profile,output,[]{});
}
}
