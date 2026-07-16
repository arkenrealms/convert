#include "arken_schema_core_v1/schema.hpp"

#include <map>
#include <string_view>
#include <vector>

namespace arken::schema::core::v1
{
catalog_identity_view BuildCatalogIdentityView(
    const SchemaCatalog& catalog,
    catalog_identity_storage& storage
)
{
    storage.fields.clear();
    storage.models.clear();
    storage.fragments.clear();

    std::size_t field_count = 0;
    for (const auto& model : catalog.models) field_count += model.fields.size();
    storage.fields.reserve(field_count);
    storage.models.reserve(catalog.models.size());
    storage.fragments.reserve(catalog.fragments.size());

    std::map<std::string_view, std::string_view> model_fragments;
    for (std::size_t ordinal = 0; ordinal < catalog.fragments.size(); ++ordinal)
    {
        const auto& fragment = catalog.fragments[ordinal];
        storage.fragments.push_back({
            .fragment_id = fragment.fragmentId,
            .owner_capsule = fragment.ownerCapsule,
            .version = static_cast<std::uint32_t>(fragment.version),
            .declaration_order = static_cast<std::uint32_t>(ordinal),
        });
        for (const auto& model : fragment.models)
            model_fragments.emplace(model.modelId, fragment.fragmentId);
    }

    struct model_field_range
    {
        std::size_t offset = 0;
        std::size_t count = 0;
    };
    std::vector<model_field_range> field_ranges;
    field_ranges.reserve(catalog.models.size());
    for (const auto& model : catalog.models)
    {
        const std::size_t offset = storage.fields.size();
        for (const auto& field : model.fields)
        {
            storage.fields.push_back({
                .model_type_id = ModelTypeId{model.typeId},
                .field_id = FieldId{field.fieldId},
                .name = field.name,
                .storage_name = field.storageName,
                .catalog_registry_key = field.registryKey,
                .catalog_field_name = field.name,
            });
        }
        field_ranges.push_back({
            .offset = offset,
            .count = storage.fields.size() - offset,
        });
    }

    for (std::size_t ordinal = 0; ordinal < catalog.models.size(); ++ordinal)
    {
        const auto& model = catalog.models[ordinal];
        const auto fragment = model_fragments.find(model.modelId);
        const auto range = field_ranges[ordinal];
        const auto field_views =
            std::span<const catalog_identity_field_view>(storage.fields)
                .subspan(range.offset, range.count);
        storage.models.push_back({
            .type_id = ModelTypeId{model.typeId},
            .name = model.modelId,
            .storage_name = model.storageName,
            .qualified_name = model.qualifiedName,
            .owner_capsule = model.ownerCapsule,
            .fields = field_views,
            .catalog_fragment_id =
                fragment == model_fragments.end() ? std::string_view{} : fragment->second,
            .catalog_registry_key = model.registryKey,
            .catalog_model_id = model.modelId,
            .catalog_logical_name = model.logicalName,
            .catalog_qualified_name = model.qualifiedName,
        });
    }

    return {
        .schema_version = static_cast<std::uint32_t>(catalog.version),
        .schema_name = catalog.catalogId,
        .owner_capsule = {},
        .models = storage.models,
        .catalog_id = catalog.catalogId,
        .catalog_version = static_cast<std::uint32_t>(catalog.version),
        .catalog_fragments = storage.fragments,
    };
}
} // namespace arken::schema::core::v1
