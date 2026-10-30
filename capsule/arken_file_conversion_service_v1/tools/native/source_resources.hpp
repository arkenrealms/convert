#pragma once
#include "arken_mcp_economics_cloudflare_v1/resource_profile.hpp"
namespace arken::conversion::source_package {
namespace E=arken::mcp::economics::cloudflare::v1;
E::Json QualifyResources(const E::SourceGuardPackage&,const E::SourceGuardConfiguration&,
 const E::path& source,const E::path& startup,const E::path& payments,const E::path& workerd,
 const E::path& profiler,const E::path& output);
}
