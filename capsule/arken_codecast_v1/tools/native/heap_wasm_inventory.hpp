#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace arken::codecast::proof {
// Pinned V8 heap graph accounting, not RSS or reserved address-space size.
nlohmann::json WasmInventoryFromSnapshot(const nlohmann::json& snapshot);
nlohmann::json ReadWasmInventory(const std::filesystem::path& snapshot);
void HeapWasmInventorySelfTest();
}
