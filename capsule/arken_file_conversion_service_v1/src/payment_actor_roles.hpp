#pragma once
#include "payment_json.hpp"
#include <string_view>

namespace arken::file_conversion::service::v1::payment::detail {
inline constexpr std::string_view publicUploadActorKey = "public-upload-budget-v1";
inline Json PublicUploadActorRole() { return {{"version", 1}, {"role", "public-upload-budget"}}; }
}
