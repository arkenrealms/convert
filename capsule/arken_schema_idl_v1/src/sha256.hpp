#pragma once

#include <string>
#include <string_view>

namespace arken::schema::idl::v1::detail
{
[[nodiscard]] std::string Sha256Hex(std::string_view bytes);
}
