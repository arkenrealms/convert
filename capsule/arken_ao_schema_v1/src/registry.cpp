#include "arken/ao/schema/v1/registry.hpp"

#include <array>

namespace arken::ao::schema::v1
{
namespace
{
constexpr std::array<AoFormatDescriptor, 5u> format_descriptors{{
    {aoab_format_id, "arken.ao.aoab", "AOAB", {2u, 0u, 0u}, 2u,
     {'A', 'O', 'A', 'B'}},
    {aogm_format_id, "arken.ao.aogm", "AOGM", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'G', 'M'}},
    {aopg_format_id, "arken.ao.aopg", "AOPG", {1u, 0u, 0u}, 2u,
     {'A', 'O', 'P', 'G'}},
    {aotm_format_id, "arken.ao.aotm", "AOTM", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'T', 'M'}},
    {aoui_format_id, "arken.ao.aoui", "AOUI", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'U', 'I'}},
}};

constexpr std::array<AoChunkTypeDescriptor, 16u> chunk_type_descriptors{{
    {core_stbl_chunk_type_id, "arken.ao.chunk.core.stbl", "STBL",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'S', 'T', 'B', 'L'}},
    {core_deps_chunk_type_id, "arken.ao.chunk.core.deps", "DEPS",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'D', 'E', 'P', 'S'}},
    {core_prov_chunk_type_id, "arken.ao.chunk.core.prov", "PROV",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'P', 'R', 'O', 'V'}},
    {core_exts_chunk_type_id, "arken.ao.chunk.core.exts", "EXTS",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'E', 'X', 'T', 'S'}},
    {core_meta_chunk_type_id, "arken.ao.chunk.core.meta", "META",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'M', 'E', 'T', 'A'}},
    {core_indx_chunk_type_id, "arken.ao.chunk.core.indx", "INDX",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'I', 'N', 'D', 'X'}},
    {core_manf_chunk_type_id, "arken.ao.chunk.core.manf", "MANF",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'M', 'A', 'N', 'F'}},
    {core_sign_chunk_type_id, "arken.ao.chunk.core.sign", "SIGN",
     {1u, 0u, 0u}, 1u, AoChunkScope::shared, {}, {'S', 'I', 'G', 'N'}},
    {aoab_amnf_v2_chunk_type_id, "arken.ao.chunk.aoab.amnf", "AMNF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoab_format_id,
     {'A', 'M', 'N', 'F'}},
    {aoab_asst_v2_chunk_type_id, "arken.ao.chunk.aoab.asst", "ASST",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoab_format_id,
     {'A', 'S', 'S', 'T'}},
    {aoab_midx_v2_chunk_type_id, "arken.ao.chunk.aoab.midx", "MIDX",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoab_format_id,
     {'M', 'I', 'D', 'X'}},
    {aoab_segm_v2_chunk_type_id, "arken.ao.chunk.aoab.segm", "SEGM",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoab_format_id,
     {'S', 'E', 'G', 'M'}},
    {aoab_vari_v2_chunk_type_id, "arken.ao.chunk.aoab.vari", "VARI",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoab_format_id,
     {'V', 'A', 'R', 'I'}},
    {aogm_posn_chunk_type_id, "arken.ao.chunk.aogm.posn", "POSN",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aogm_format_id,
     {'P', 'O', 'S', 'N'}},
    {parametric_recipe_chunk_type_id, "arken.ao.chunk.aopg.prcp", "PRCP",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopg_format_id,
     {'P', 'R', 'C', 'P'}},
    {parametric_output_chunk_type_id, "arken.ao.chunk.aopg.pout", "POUT",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopg_format_id,
     {'P', 'O', 'U', 'T'}},
}};
}

AoFormatLookup AoSchemaRegistry::find_ao_format(AoFormatId id) const noexcept
{
    for(auto const& descriptor : format_descriptors)
    {
        if(descriptor.id == id)
            return {&descriptor};
    }
    return {};
}

AoFormatLookup AoSchemaRegistry::find_ao_format(
    std::array<std::uint8_t, 4u> magic) const noexcept
{
    for(auto const& descriptor : format_descriptors)
    {
        if(descriptor.magic == magic)
            return {&descriptor};
    }
    return {};
}

AoChunkTypeLookup AoSchemaRegistry::find_ao_chunk_type(
    AoChunkTypeId id) const noexcept
{
    for(auto const& descriptor : chunk_type_descriptors)
    {
        if(descriptor.id == id)
            return {&descriptor};
    }
    return {};
}

AoChunkTypeLookup AoSchemaRegistry::find_ao_chunk_type(
    AoFormatId owner_format_id,
    std::array<std::uint8_t, 4u> magic) const noexcept
{
    for(auto const& descriptor : chunk_type_descriptors)
    {
        bool const owner_matches = descriptor.scope == AoChunkScope::shared ||
                                   descriptor.owner_format_id == owner_format_id;
        if(owner_matches && descriptor.magic == magic)
            return {&descriptor};
    }
    return {};
}

std::span<AoFormatDescriptor const> AoSchemaRegistry::formats() const noexcept
{
    return format_descriptors;
}

std::span<AoChunkTypeDescriptor const>
AoSchemaRegistry::chunk_types() const noexcept
{
    return chunk_type_descriptors;
}

AoSchemaRegistry bootstrap_registry() noexcept
{
    return {};
}
}
