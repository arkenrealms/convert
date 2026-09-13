#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
// Private server protocol adapter. Produces x402 MCP challenges, validates
// shape/quote binding (NOT signatures), and fences facilitator callbacks.
std::string X402Packet(std::string_view) noexcept;
}
