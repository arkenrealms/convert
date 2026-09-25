#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1 {
// Pure bounded provider qualification, independent of product bindings/cache.
// Emits only fixed read-only supported and Base safe-block/USDC-decimals probes.
// The returned RPC URL is sensitive transport data, never public diagnostics.
std::string ProviderReadinessPacket(std::string_view input) noexcept;
// Shared 300-second verdict cache. The trusted product supplies a bounded,
// opaque context identity containing its release and platform binding epoch.
// Credentials are included independently in the cache fingerprint.
std::string CachedProviderReadinessPacket(std::string_view input) noexcept;
}
