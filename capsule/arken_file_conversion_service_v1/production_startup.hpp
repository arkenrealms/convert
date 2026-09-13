#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1 {
// Private C++ admission controller. Results are not commercial acceptance.
std::string ProductionStartupPacket(std::string_view) noexcept;
// Private operator request gate and disabled-provider check. Never admits a purchase.
std::string ProductionQualificationPacket(std::string_view) noexcept;
}
