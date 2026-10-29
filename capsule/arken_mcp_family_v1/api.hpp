#pragma once
#include <span>
#include <string_view>
namespace arken::mcp::family::v1 {
struct Family {const char *id,*name,*summary,*intent;};
[[nodiscard]] std::span<const Family> families() noexcept;
[[nodiscard]] const Family* find(std::string_view id) noexcept;
// Reserved existing deployments must never be replaced by a scaffold.
[[nodiscard]] bool scaffold(std::string_view id) noexcept;
}
