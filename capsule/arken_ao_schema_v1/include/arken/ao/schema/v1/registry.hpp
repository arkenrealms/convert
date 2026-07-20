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
    0xdfu, 0x25u, 0x6eu, 0xa7u, 0x0cu, 0xacu, 0x80u, 0xd6u,
    0xbdu, 0x23u, 0xe9u, 0x72u, 0x48u, 0x39u, 0x51u, 0x08u,
}}};
inline constexpr AoFormatId aotm_format_id{arken::identity::v1::ArkenId{{
    0xd8u, 0x37u, 0x34u, 0xadu, 0xebu, 0xa7u, 0x88u, 0xbdu,
    0x91u, 0x1au, 0xa8u, 0x27u, 0x44u, 0x05u, 0xe6u, 0x7au,
}}};

class AoSchemaRegistry final
{
  public:
    [[nodiscard]] AoFormatLookup find_ao_format(AoFormatId id) const noexcept;
};

[[nodiscard]] AoSchemaRegistry bootstrap_registry() noexcept;
}
