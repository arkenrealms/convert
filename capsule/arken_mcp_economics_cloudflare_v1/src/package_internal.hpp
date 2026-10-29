#pragma once
#include "arken_mcp_economics_cloudflare_v1/source_guard.hpp"
namespace arken::mcp::economics::cloudflare::v1::detail {
Json manifest(const std::vector<Module>&);
void validateService(std::string_view);
std::vector<Module> wrap(std::string_view service,std::string_view mainModule,
 const std::vector<Module>&,std::string_view adapter);
}
