#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace arken::mcp::economics::v1 {
inline constexpr std::uint32_t budgetPacketLimit=32768;
// Private service/DO contract. Mode 0 derives storage keys; mode 1 reserves
// all three scopes atomically. No caller-supplied outcome or refund command.
std::string budgetPacket(std::uint32_t mode,std::string_view input) noexcept;
}
