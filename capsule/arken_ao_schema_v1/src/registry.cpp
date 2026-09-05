#include "arken/ao/schema/v1/registry.hpp"

#include <array>

namespace arken::ao::schema::v1
{
namespace
{
constexpr std::array<AoFormatDescriptor, 15u> format_descriptors{{
    {aoab_format_id, "arken.ao.aoab", "AOAB", {2u, 0u, 0u}, 2u,
     {'A', 'O', 'A', 'B'}},
    {aoan_format_id, "arken.ao.aoan", "AOAN", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'A', 'N'}},
    {aoav_format_id, "arken.ao.aoav", "AOAV", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'A', 'V'}},
    {aoau_format_id, "arken.ao.aoau", "AOAU", {1u, 0u, 0u}, 2u,
     {'A', 'O', 'A', 'U'}},
    {aoef_format_id, "arken.ao.aoef", "AOEF", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'E', 'F'}},
    {aosc_format_id, "arken.ao.aosc", "AOSC", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'S', 'C'}},
    {aodb_format_id, "arken.ao.aodb", "AODB", {2u, 0u, 0u}, 2u,
     {'A', 'O', 'D', 'B'}},
    {aodg_format_id, "arken.ao.aodg", "AODG", {1u, 0u, 0u}, 2u,
     {'A', 'O', 'D', 'G'}},
    {aois_format_id, "arken.ao.aois", "AOIS", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'I', 'S'}},
    {aogm_format_id, "arken.ao.aogm", "AOGM", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'G', 'M'}},
    {aopb_format_id, "arken.ao.aopb", "AOPB", {2u, 0u, 0u}, 2u,
     {'A', 'O', 'P', 'B'}},
    {aopg_format_id, "arken.ao.aopg", "AOPG", {1u, 0u, 0u}, 2u,
     {'A', 'O', 'P', 'G'}},
    {pspj_format_id, "arken.ao.pspj", "PSPJ", {4u, 0u, 0u}, 1u,
     {'P', 'S', 'P', 'J'}},
    {aotm_format_id, "arken.ao.aotm", "AOTM", {2u, 0u, 0u}, 2u,
     {'A', 'O', 'T', 'M'}},
    {aoui_format_id, "arken.ao.aoui", "AOUI", {1u, 0u, 0u}, 1u,
     {'A', 'O', 'U', 'I'}},
}};

constexpr std::array<AoChunkTypeDescriptor, 52u> chunk_type_descriptors{{
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
    {aoau_amnf_chunk_type_id, "arken.ao.chunk.aoau.amnf", "AMNF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoau_format_id,
     {'A', 'M', 'N', 'F'}},
    {aoau_payl_chunk_type_id, "arken.ao.chunk.aoau.payl", "PAYL",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoau_format_id,
     {'P', 'A', 'Y', 'L'}},
    {aosc_scne_chunk_type_id, "arken.ao.chunk.aosc.scne", "SCNE",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aosc_format_id,
     {'S', 'C', 'N', 'E'}},
    {aosc_lite_chunk_type_id, "arken.ao.chunk.aosc.lite", "LITE",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aosc_format_id,
     {'L', 'I', 'T', 'E'}},
    {aosc_fogv_chunk_type_id, "arken.ao.chunk.aosc.fogv", "FOGV",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aosc_format_id,
     {'F', 'O', 'G', 'V'}},
    {aosc_lref_chunk_type_id, "arken.ao.chunk.aosc.lref", "LREF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aosc_format_id,
     {'L', 'R', 'E', 'F'}},
    {aosc_fref_chunk_type_id, "arken.ao.chunk.aosc.fref", "FREF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aosc_format_id,
     {'F', 'R', 'E', 'F'}},
    {aoan_anim_chunk_type_id, "arken.ao.chunk.aoan.anim", "ANIM",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'A', 'N', 'I', 'M'}},
    {aoan_clip_chunk_type_id, "arken.ao.chunk.aoan.clip", "CLIP",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'C', 'L', 'I', 'P'}},
    {aoan_gclk_chunk_type_id, "arken.ao.chunk.aoan.gclk", "GCLK",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'G', 'C', 'L', 'K'}},
    {aoan_chan_chunk_type_id, "arken.ao.chunk.aoan.chan", "CHAN",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'C', 'H', 'A', 'N'}},
    {aoan_keyf_chunk_type_id, "arken.ao.chunk.aoan.keyf", "KEYF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'K', 'E', 'Y', 'F'}},
    {aoan_evnt_chunk_type_id, "arken.ao.chunk.aoan.evnt", "EVNT",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'E', 'V', 'N', 'T'}},
    {aoan_look_chunk_type_id, "arken.ao.chunk.aoan.look", "LOOK",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'L', 'O', 'O', 'K'}},
    {aoan_edef_chunk_type_id, "arken.ao.chunk.aoan.edef", "EDEF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoan_format_id,
     {'E', 'D', 'E', 'F'}},
    {aoef_efct_chunk_type_id, "arken.ao.chunk.aoef.efct", "EFCT",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoef_format_id,
     {'E', 'F', 'C', 'T'}},
    {aoef_part_chunk_type_id, "arken.ao.chunk.aoef.part", "PART",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoef_format_id,
     {'P', 'A', 'R', 'T'}},
    {aoef_ribn_chunk_type_id, "arken.ao.chunk.aoef.ribn", "RIBN",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoef_format_id,
     {'R', 'I', 'B', 'N'}},
    {aoef_spln_chunk_type_id, "arken.ao.chunk.aoef.spln", "SPLN",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aoef_format_id,
     {'S', 'P', 'L', 'N'}},
    {aopb_caps_v2_chunk_type_id, "arken.ao.chunk.aopb.caps", "CAPS",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'C', 'A', 'P', 'S'}},
    {aopb_deps_v2_chunk_type_id, "arken.ao.chunk.aopb.deps", "DEPS",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'D', 'E', 'P', 'S'}},
    {aopb_entr_v2_chunk_type_id, "arken.ao.chunk.aopb.entr", "ENTR",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'E', 'N', 'T', 'R'}},
    {aopb_manf_v2_chunk_type_id, "arken.ao.chunk.aopb.manf", "PMNF",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'P', 'M', 'N', 'F'}},
    {aopb_payl_v2_chunk_type_id, "arken.ao.chunk.aopb.payl", "PAYL",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'P', 'A', 'Y', 'L'}},
    {aopb_poli_v2_chunk_type_id, "arken.ao.chunk.aopb.poli", "POLI",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'P', 'O', 'L', 'I'}},
    {aopb_prov_v2_chunk_type_id, "arken.ao.chunk.aopb.prov", "PROV",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aopb_format_id,
     {'P', 'R', 'O', 'V'}},
    {aodb_asst_chunk_type_id, "arken.ao.chunk.aodb.asst", "ASST",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'A', 'S', 'S', 'T'}},
    {aodb_cols_chunk_type_id, "arken.ao.chunk.aodb.cols", "COLS",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'C', 'O', 'L', 'S'}},
    {aodb_iden_chunk_type_id, "arken.ao.chunk.aodb.iden", "IDEN",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'I', 'D', 'E', 'N'}},
    {aodb_indx_chunk_type_id, "arken.ao.chunk.aodb.indx", "INDX",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'I', 'N', 'D', 'X'}},
    {aodb_part_chunk_type_id, "arken.ao.chunk.aodb.part", "PART",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'P', 'A', 'R', 'T'}},
    {aodb_plac_chunk_type_id, "arken.ao.chunk.aodb.plac", "PLAC",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'P', 'L', 'A', 'C'}},
    {aodb_port_chunk_type_id, "arken.ao.chunk.aodb.port", "PORT",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'P', 'O', 'R', 'T'}},
    {aodb_rels_chunk_type_id, "arken.ao.chunk.aodb.rels", "RELS",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'R', 'E', 'L', 'S'}},
    {aodb_tabl_chunk_type_id, "arken.ao.chunk.aodb.tabl", "TABL",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'T', 'A', 'B', 'L'}},
    {aodb_type_chunk_type_id, "arken.ao.chunk.aodb.type", "TYPE",
     {1u, 0u, 0u}, 1u, AoChunkScope::format, aodb_format_id, {'T', 'Y', 'P', 'E'}},
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

constexpr std::array<AoTableTypeDescriptor, 25u> table_type_descriptors{{
    {aodb_asset_reference_table_type_id, "arken.ao.table.aodb.asset_reference", "ASST", {1u,0u,0u}, aodb_format_id, {'A','S','S','T'}, 1u, 1u, 144u},
    {aodb_asset_use_index_table_type_id, "arken.ao.table.aodb.asset_use_index", "INDX", {1u,0u,0u}, aodb_format_id, {'I','N','D','X'}, 1u, 1u, 48u},
    {aodb_cell_table_type_id, "arken.ao.table.aodb.cell", "PART", {1u,0u,0u}, aodb_format_id, {'P','A','R','T'}, 1u, 1u, 160u},
    {aodb_definition_table_type_id, "arken.ao.table.aodb.definition", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 160u},
    {aodb_environment_table_type_id, "arken.ao.table.aodb.environment", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 48u},
    {aodb_identity_table_type_id, "arken.ao.table.aodb.identity", "IDEN", {1u,0u,0u}, aodb_format_id, {'I','D','E','N'}, 1u, 1u, 32u},
    {aodb_partition_table_type_id, "arken.ao.table.aodb.partition", "PART", {1u,0u,0u}, aodb_format_id, {'P','A','R','T'}, 1u, 1u, 144u},
    {aodb_partition_spatial_index_table_type_id, "arken.ao.table.aodb.partition_spatial_index", "INDX", {1u,0u,0u}, aodb_format_id, {'I','N','D','X'}, 1u, 1u, 96u},
    {aodb_placement_table_type_id, "arken.ao.table.aodb.placement", "PLAC", {1u,0u,0u}, aodb_format_id, {'P','L','A','C'}, 1u, 1u, 304u},
    {aodb_placement_cell_index_table_type_id, "arken.ao.table.aodb.placement_cell_index", "INDX", {1u,0u,0u}, aodb_format_id, {'I','N','D','X'}, 1u, 1u, 72u},
    {aodb_portal_table_type_id, "arken.ao.table.aodb.portal", "PORT", {1u,0u,0u}, aodb_format_id, {'P','O','R','T'}, 1u, 1u, 144u},
    {aodb_primary_index_table_type_id, "arken.ao.table.aodb.primary_index", "INDX", {1u,0u,0u}, aodb_format_id, {'I','N','D','X'}, 1u, 1u, 48u},
    {aodb_relationship_table_type_id, "arken.ao.table.aodb.relationship", "RELS", {1u,0u,0u}, aodb_format_id, {'R','E','L','S'}, 1u, 1u, 80u},
    {aodb_space_table_type_id, "arken.ao.table.aodb.space", "PART", {1u,0u,0u}, aodb_format_id, {'P','A','R','T'}, 1u, 1u, 128u},
    {aodb_terrain_layer_table_type_id, "arken.ao.table.aodb.terrain_layer", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 112u},
    {aodb_terrain_set_table_type_id, "arken.ao.table.aodb.terrain_set", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 144u},
    {aodb_terrain_tile_table_type_id, "arken.ao.table.aodb.terrain_tile", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 17600u},
    {aodb_type_descriptor_table_type_id, "arken.ao.table.aodb.type_descriptor", "TYPE", {1u,0u,0u}, aodb_format_id, {'T','Y','P','E'}, 1u, 1u, 48u},
    {aodb_parametric_definition_table_type_id, "arken.ao.table.aodb.parametric_definition.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 240u},
    {aodb_parametric_variant_table_type_id, "arken.ao.table.aodb.parametric_variant.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 144u},
    {aodb_parametric_output_table_type_id, "arken.ao.table.aodb.parametric_output.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 256u},
    {aodb_parametric_placement_binding_table_type_id, "arken.ao.table.aodb.parametric_placement_binding.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 144u},
    {aodb_parametric_archive_table_type_id, "arken.ao.table.aodb.parametric_archive.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 272u},
    {aodb_parametric_dependency_table_type_id, "arken.ao.table.aodb.parametric_dependency.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 160u},
    {aodb_parametric_cook_receipt_table_type_id, "arken.ao.table.aodb.parametric_cook_receipt.v1", "COLS", {1u,0u,0u}, aodb_format_id, {'C','O','L','S'}, 1u, 1u, 576u},
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

AoTableTypeLookup AoSchemaRegistry::find_ao_table_type(AoTableTypeId id) const noexcept
{
    for(auto const& descriptor : table_type_descriptors)
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

std::span<AoTableTypeDescriptor const> AoSchemaRegistry::table_types() const noexcept
{
    return table_type_descriptors;
}

AoSchemaRegistry bootstrap_registry() noexcept
{
    return {};
}
}
