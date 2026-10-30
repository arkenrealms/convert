#pragma once

#include <string>
#include <string_view>

namespace arken::schema::idl::v1
{
[[nodiscard]] std::string StableSha256Hex(std::string_view bytes);
}
