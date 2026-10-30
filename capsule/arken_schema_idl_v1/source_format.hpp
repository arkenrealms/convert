#pragma once
#include <string_view>

namespace arken::schema::idl::v1 {
inline constexpr std::string_view kSchemaSourceExtension=".aoschema";
// New authoring uses .aoschema. Preserved existing sources remain exact-path
// inputs; this never searches for or substitutes an alternate sibling.
inline constexpr bool HasSchemaSourceExtension(std::string_view path) noexcept {
    return path.ends_with(kSchemaSourceExtension)||path.ends_with(".arken-schema");
}
}
