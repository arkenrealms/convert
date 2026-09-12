#pragma once
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
// Trusted platform adapter only. Public input enters through PlanRequest.
// C++ owns quote binding, execution instructions and paid-result disclosure.
[[nodiscard]] std::string PaidConversionPacket(std::string_view) noexcept;
}
