#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1 {
// Trusted host configuration plus HTTP metadata; never a customer MCP tool.
std::string EndpointPacket(std::string_view) noexcept;
}
