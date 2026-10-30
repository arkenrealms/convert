#pragma once
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
namespace arken::codecast::proof {
enum class CdpPageAccess { assets, inspection, ui_inspection, worker_memory };
bool CdpPageMethodAllowed(std::string_view method,CdpPageAccess access) noexcept;
bool CdpPageEndpointValid(std::string_view endpoint);
bool CdpWorkerEndpointValid(std::string_view endpoint);
class CdpPageSession final {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    CdpPageSession(const std::string& endpoint,CdpPageAccess access);
    ~CdpPageSession();
    CdpPageSession(const CdpPageSession&)=delete;
    CdpPageSession& operator=(const CdpPageSession&)=delete;
    // Finish the inspector close handshake before its owning process stops.
    // Bounded and idempotent; false preserves a failed cleanup verdict.
    bool Close() noexcept;
    // Diagnostic snapshot of the owned Worker; no caller code is evaluated.
    // Taking a snapshot may trigger GC. Output must be a new private directory.
    nlohmann::json WasmMemoryInventory(const std::filesystem::path& output);
    nlohmann::json Call(std::string_view method,nlohmann::json params=nlohmann::json::object());
};
}
