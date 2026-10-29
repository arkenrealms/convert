#pragma once
#include "arken_cloudflare_client_v1/worker.hpp"
namespace arken::mcp::economics::cloudflare::v1 {
namespace C=::arken::cloudflare::v1;
using C::Json;using C::fs::path;
struct Module {std::string name,mediaType,bytes;};
inline constexpr auto guardOriginalModule="mcp_original.js";
inline constexpr auto guardAdapterModule="mcp_attempt_gate.js";
std::vector<std::string> Services();
std::string Wrapper(std::string_view service);
// Local source evidence only. Deliberately has no provider/predecessor identity.
struct SourceGuardPackage {
 std::string service,mainModule;
 std::vector<Module> modules;
 std::string ordinaryModulesSha256;
};
struct SourceGuardConfiguration {
 std::string compatibilityDate="2026-09-11";
 std::vector<std::string> compatibilityFlags;
 std::map<std::string,std::string> textBindings;
 std::vector<std::string> unavailableBindings;
};
SourceGuardPackage ComposeSourceGuard(std::string_view service,std::string_view mainModule,
 const std::vector<Module>& ordinary,std::string_view adapter);
Json QualifySourceGuard(const SourceGuardPackage&,const SourceGuardConfiguration&,std::string_view endpoint,
 const path& source,const path& workerd,const path& output);
}
