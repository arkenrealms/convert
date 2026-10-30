#pragma once
#include "guard_execution.hpp"
#include "arken_mcp_economics_cloudflare_v1/resource_profile.hpp"
namespace arken::mcp::economics::cloudflare::v1::detail {
Json PrepareResourceModules(const std::vector<Module>&,std::string_view service,std::string_view mainModule,
 const ExecutionContext&,const std::string& stateContract,const path& guardProof,const path& source,
 const MaintenanceResources&,const path& output,const std::function<void()>& revalidate);
}
