#pragma once

#include "arken/identity/v1/identity.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <string_view>

namespace arken::ao::schema::v1
{
struct AoFormatIdentityKind;
using AoFormatId = arken::identity::v1::TypedId<AoFormatIdentityKind>;
struct AoChunkTypeIdentityKind;
using AoChunkTypeId = arken::identity::v1::TypedId<AoChunkTypeIdentityKind>;

static_assert(arken::identity::v1::typed_id_has_canonical_layout<AoFormatIdentityKind>);
static_assert(arken::identity::v1::typed_id_has_canonical_layout<AoChunkTypeIdentityKind>);

struct AoFormatVersion final
{
    std::uint32_t major = 0u;
    std::uint32_t minor = 0u;
    std::uint32_t patch = 0u;
    [[nodiscard]] constexpr auto operator<=>(AoFormatVersion const&) const noexcept = default;
};

using SemanticVersion = AoFormatVersion;

enum class AoChunkScope : std::uint8_t
{
    shared = 1u,
    format = 2u,
};

struct AoFormatDescriptor final
{
    AoFormatId id{};
    std::string_view canonical_name{};
    std::string_view diagnostic_name{};
    AoFormatVersion format_version{};
    std::uint32_t container_version = 0u;
    std::array<std::uint8_t, 4u> magic{};
};

struct AoFormatLookup final
{
    AoFormatDescriptor const* descriptor = nullptr;
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return descriptor != nullptr;
    }
};

struct AoChunkTypeDescriptor final
{
    AoChunkTypeId id{};
    std::string_view canonical_name{};
    std::string_view diagnostic_name{};
    AoFormatVersion schema_version{};
    std::uint32_t chunk_version = 0u;
    AoChunkScope scope = AoChunkScope::shared;
    AoFormatId owner_format_id{};
    std::array<std::uint8_t, 4u> magic{};
};

struct AoChunkTypeLookup final
{
    AoChunkTypeDescriptor const* descriptor = nullptr;
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return descriptor != nullptr;
    }
};

inline constexpr AoFormatId aoab_format_id{arken::identity::v1::ArkenId{{
    0x27u, 0x1fu, 0xceu, 0xecu, 0x68u, 0x3bu, 0x8bu, 0x71u,
    0x82u, 0xa2u, 0x27u, 0xa5u, 0xccu, 0x81u, 0x13u, 0xadu,
}}};
inline constexpr AoFormatId aogm_format_id{arken::identity::v1::ArkenId{{
    0xdfu, 0x25u, 0x6eu, 0xa7u, 0x0cu, 0xacu, 0x80u, 0xd6u,
    0xbdu, 0x23u, 0xe9u, 0x72u, 0x48u, 0x39u, 0x51u, 0x08u,
}}};
inline constexpr AoFormatId aopg_format_id{arken::identity::v1::ArkenId{{
    0x38u, 0x36u, 0xeau, 0x9au, 0xfau, 0x36u, 0x8eu, 0x55u,
    0x93u, 0x58u, 0xfeu, 0x1eu, 0xf0u, 0x29u, 0x25u, 0xe5u,
}}};
inline constexpr AoFormatId aotm_format_id{arken::identity::v1::ArkenId{{
    0xd8u, 0x37u, 0x34u, 0xadu, 0xebu, 0xa7u, 0x88u, 0xbdu,
    0x91u, 0x1au, 0xa8u, 0x27u, 0x44u, 0x05u, 0xe6u, 0x7au,
}}};
inline constexpr AoFormatId aoui_format_id{arken::identity::v1::ArkenId{{
    0xb0u, 0xd1u, 0x8cu, 0x7eu, 0x99u, 0x94u, 0x80u, 0x02u,
    0x86u, 0x5bu, 0xadu, 0xc9u, 0x30u, 0x74u, 0x56u, 0x03u,
}}};

inline constexpr AoChunkTypeId core_stbl_chunk_type_id{arken::identity::v1::ArkenId{{
    0x1eu, 0x06u, 0xf1u, 0x49u, 0xf6u, 0xb1u, 0x88u, 0x79u,
    0xa2u, 0x98u, 0x1bu, 0xc6u, 0xf1u, 0x51u, 0x77u, 0x3eu,
}}};
inline constexpr AoChunkTypeId core_deps_chunk_type_id{arken::identity::v1::ArkenId{{
    0x11u, 0x00u, 0xb1u, 0xb8u, 0xcdu, 0x90u, 0x8au, 0x24u,
    0x99u, 0x39u, 0xe3u, 0xa0u, 0x44u, 0x5bu, 0x4cu, 0x14u,
}}};
inline constexpr AoChunkTypeId core_prov_chunk_type_id{arken::identity::v1::ArkenId{{
    0x3cu, 0x54u, 0xf6u, 0x9au, 0xe4u, 0xf9u, 0x8du, 0xadu,
    0xaau, 0x3cu, 0x94u, 0x97u, 0xbfu, 0x57u, 0x2du, 0x70u,
}}};
inline constexpr AoChunkTypeId core_exts_chunk_type_id{arken::identity::v1::ArkenId{{
    0xfeu, 0xf2u, 0x10u, 0x33u, 0xe9u, 0xa0u, 0x86u, 0xfcu,
    0xa3u, 0xaeu, 0xecu, 0x58u, 0xa3u, 0xa9u, 0xf0u, 0xc6u,
}}};
inline constexpr AoChunkTypeId core_meta_chunk_type_id{arken::identity::v1::ArkenId{{
    0x42u, 0x99u, 0x80u, 0x05u, 0x51u, 0xc0u, 0x8au, 0xd3u,
    0xa5u, 0x41u, 0x9fu, 0x31u, 0x91u, 0xa0u, 0x02u, 0x95u,
}}};
inline constexpr AoChunkTypeId core_indx_chunk_type_id{arken::identity::v1::ArkenId{{
    0xf0u, 0x5fu, 0x7du, 0x2cu, 0xd3u, 0x2cu, 0x86u, 0x97u,
    0xbau, 0xcbu, 0xd9u, 0x5du, 0x51u, 0x88u, 0xd3u, 0x75u,
}}};
inline constexpr AoChunkTypeId core_manf_chunk_type_id{arken::identity::v1::ArkenId{{
    0xf5u, 0x6eu, 0xd8u, 0x15u, 0x59u, 0x64u, 0x8fu, 0x7bu,
    0x94u, 0xeau, 0x1au, 0xd0u, 0xd7u, 0x1bu, 0xddu, 0x49u,
}}};
inline constexpr AoChunkTypeId core_sign_chunk_type_id{arken::identity::v1::ArkenId{{
    0x87u, 0x00u, 0x74u, 0x7eu, 0xa6u, 0x1au, 0x89u, 0x49u,
    0x96u, 0xa6u, 0x17u, 0xe5u, 0x9fu, 0x6eu, 0x86u, 0x38u,
}}};

inline constexpr AoChunkTypeId aoab_amnf_v2_chunk_type_id{arken::identity::v1::ArkenId{{
    0xbbu, 0x01u, 0x36u, 0x0fu, 0xffu, 0x09u, 0x80u, 0x86u,
    0xa4u, 0x5bu, 0xcfu, 0x41u, 0xedu, 0x74u, 0x26u, 0xbbu,
}}};
inline constexpr AoChunkTypeId aoab_asst_v2_chunk_type_id{arken::identity::v1::ArkenId{{
    0x95u, 0x0bu, 0x1du, 0x6du, 0x23u, 0x3bu, 0x89u, 0x03u,
    0xa7u, 0x55u, 0x40u, 0x21u, 0xefu, 0x34u, 0x18u, 0xb6u,
}}};
inline constexpr AoChunkTypeId aoab_midx_v2_chunk_type_id{arken::identity::v1::ArkenId{{
    0xd1u, 0xd1u, 0x3fu, 0x38u, 0x47u, 0xd5u, 0x8fu, 0x3au,
    0xbau, 0x38u, 0x5du, 0x30u, 0xd3u, 0x0fu, 0x67u, 0xf2u,
}}};
inline constexpr AoChunkTypeId aoab_segm_v2_chunk_type_id{arken::identity::v1::ArkenId{{
    0x20u, 0xffu, 0xf8u, 0x0fu, 0xe3u, 0x0bu, 0x86u, 0xe9u,
    0xb1u, 0x61u, 0xcdu, 0x17u, 0xc5u, 0x83u, 0x52u, 0xb2u,
}}};
inline constexpr AoChunkTypeId aoab_vari_v2_chunk_type_id{arken::identity::v1::ArkenId{{
    0x49u, 0x3fu, 0x16u, 0xccu, 0xd8u, 0x0cu, 0x85u, 0x30u,
    0x87u, 0x3cu, 0x5du, 0xc4u, 0x7bu, 0xfcu, 0x39u, 0x78u,
}}};

inline constexpr AoChunkTypeId aogm_posn_chunk_type_id{arken::identity::v1::ArkenId{{
    0xbau, 0xbfu, 0x9fu, 0xf0u, 0x00u, 0x6bu, 0x8fu, 0xa3u,
    0x82u, 0x02u, 0xb9u, 0x33u, 0x8eu, 0x9bu, 0x50u, 0xf6u,
}}};

inline constexpr AoChunkTypeId parametric_recipe_chunk_type_id{
    arken::identity::v1::ArkenId{{0x84u, 0x92u, 0x01u, 0x28u, 0x36u, 0x1eu,
                                  0x82u, 0xe5u, 0xa8u, 0x11u, 0xdcu, 0x70u,
                                  0xe2u, 0x5bu, 0x89u, 0xa9u}}};
inline constexpr AoChunkTypeId parametric_output_chunk_type_id{
    arken::identity::v1::ArkenId{{0x21u, 0xa7u, 0xb8u, 0x4au, 0xa6u, 0x3cu,
                                  0x86u, 0x4bu, 0xaau, 0xf9u, 0x6bu, 0x20u,
                                  0x2du, 0x45u, 0xe1u, 0xf3u}}};

class AoSchemaRegistry final
{
  public:
    [[nodiscard]] AoFormatLookup find_ao_format(AoFormatId id) const noexcept;
    [[nodiscard]] AoFormatLookup find_ao_format(
        std::array<std::uint8_t, 4u> magic) const noexcept;
    [[nodiscard]] AoChunkTypeLookup find_ao_chunk_type(
        AoChunkTypeId id) const noexcept;
    [[nodiscard]] AoChunkTypeLookup find_ao_chunk_type(
        AoFormatId owner_format_id,
        std::array<std::uint8_t, 4u> magic) const noexcept;
    [[nodiscard]] std::span<AoFormatDescriptor const> formats() const noexcept;
    [[nodiscard]] std::span<AoChunkTypeDescriptor const> chunk_types() const noexcept;
};

[[nodiscard]] AoSchemaRegistry bootstrap_registry() noexcept;
}
