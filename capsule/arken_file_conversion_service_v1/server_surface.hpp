#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1 {
enum class ServerMode {preview,sandbox,production};
struct ServerSurface {
    ServerMode mode;
    std::string_view profileId,displayMode,artifactPrefix,downloadDomain,instructions;
    unsigned accessMilliseconds;
    bool publicAccess,testnet;
};
ServerSurface Surface(ServerMode);
std::string SurfacePacket(std::string_view) noexcept;
}
