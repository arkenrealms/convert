#pragma once

#include "arken/identity/v1/identity.hpp"

#include <compare>
#include <cstdint>
#include <string_view>

namespace arken::ao::schema::v1
{
struct AoFormatIdentityKind;
using AoFormatId = arken::identity::v1::TypedId<AoFormatIdentityKind>;

struct AoFormatVersion final
{
    std::uint32_t major = 0u;
    std::uint32_t minor = 0u;
    std::uint32_t patch = 0u;
    [[nodiscard]] constexpr auto operator<=>(AoFormatVersion const&) const noexcept = default;
};

struct AoFormatDescriptor final
{
    AoFormatId format_id{};
    AoFormatVersion format_version{};
    std::string_view key{};
};

struct AoFormatLookup final
{
    AoFormatDescriptor const* descriptor = nullptr;
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return descriptor != nullptr;
    }
};

inline constexpr AoFormatId aogm_format_id{arken::identity::v1::ArkenId{{
    0x61u, 0x6fu, 0x67u, 0x6du, 0x00u, 0x01u, 0x80u, 0x01u,
    0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u,
}}};
inline constexpr AoFormatId aotm_format_id{arken::identity::v1::ArkenId{{
    0x61u, 0x6fu, 0x74u, 0x6du, 0x00u, 0x01u, 0x80u, 0x01u,
    0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u,
}}};

class AoSchemaRegistry final
{
  public:
    [[nodiscard]] AoFormatLookup find_ao_format(AoFormatId id) const noexcept;
};

[[nodiscard]] AoSchemaRegistry bootstrap_registry() noexcept;
}
