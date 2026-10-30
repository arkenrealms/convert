#pragma once
#include "arken_mcp_economics_cloudflare_v1/source_guard.hpp"
namespace arken::mcp::economics::cloudflare::v1::detail {
void guardOutput(const path&);
struct ExecutionContext {
 std::string compatibilityDate;
 Json flags=Json::array(),bindings=Json::object(),unavailableBindings=Json::array();
 bool currentSource=false;
 std::string authoritySha256;
};
ExecutionContext SourceContext(const SourceGuardPackage&,const SourceGuardConfiguration&);
Json RunGuard(std::string_view service,std::string_view mainModule,const std::vector<Module>&,
 const ExecutionContext&,std::string_view endpoint,const path& source,const path& workerd,
 const path& output,const std::function<void()>& revalidate={});
}
