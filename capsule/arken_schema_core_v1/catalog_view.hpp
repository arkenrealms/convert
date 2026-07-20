#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arken::schema::core::v1
{
template <typename Tag>
struct RegistryId
{
    std::uint32_t value = 0;

    constexpr RegistryId() noexcept = default;
    constexpr RegistryId(std::uint32_t initial) noexcept : value(initial) {}

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] auto operator<=>(const RegistryId&) const = default;
};

struct ModelTypeIdTag;
struct FieldIdTag;
struct EnumValueIdTag;
struct SemanticTypeIdTag;
struct WireTypeIdTag;
struct StorageCodecIdTag;
struct CommandIdTag;
struct EventIdTag;

using ModelTypeId = RegistryId<ModelTypeIdTag>;
using FieldId = RegistryId<FieldIdTag>;
using EnumValueId = RegistryId<EnumValueIdTag>;
using SemanticTypeId = RegistryId<SemanticTypeIdTag>;
using WireTypeId = RegistryId<WireTypeIdTag>;
using StorageCodecId = RegistryId<StorageCodecIdTag>;
using CommandId = RegistryId<CommandIdTag>;
using EventId = RegistryId<EventIdTag>;

struct numeric_index_entry
{
    std::uint32_t key = 0;
    std::uint32_t ordinal = 0;
};

struct name_index_entry
{
    std::string_view key;
    std::uint32_t ordinal = 0;
};

struct schema_contract_ref
{
    std::string_view schema_name;
    std::uint32_t schema_version = 0;
    std::string_view owner_capsule;
    std::string_view semantic_fingerprint;
    std::string_view allocation_fingerprint;

    [[nodiscard]] bool operator==(const schema_contract_ref&) const = default;
};

template <typename Value, std::size_t Size>
[[nodiscard]] constexpr std::span<const Value> descriptor_span(
    const Value (&values)[Size]
) noexcept
{
    return {values, Size};
}

template <typename Value, std::size_t Size>
[[nodiscard]] constexpr std::span<const Value> descriptor_span(
    const std::array<Value, Size>& values
) noexcept
{
    return {values.data(), values.size()};
}

template <typename Value>
class descriptor_view
{
public:
    constexpr descriptor_view() noexcept = default;

    constexpr descriptor_view(std::span<const Value> values) noexcept
        : base_(values.data()), count_(values.size())
    {
    }

    constexpr descriptor_view(
        const Value* base,
        std::size_t offset,
        std::size_t count
    ) noexcept
        : base_(base), offset_(offset), count_(count)
    {
    }

    [[nodiscard]] constexpr const Value* data() const noexcept
    {
        return base_ == nullptr ? nullptr : base_ + offset_;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return count_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return count_ == 0u;
    }

    [[nodiscard]] constexpr const Value* begin() const noexcept
    {
        return data();
    }

    [[nodiscard]] constexpr const Value* end() const noexcept
    {
        return count_ == 0U ? data() : data() + count_;
    }

    [[nodiscard]] constexpr const Value& front() const noexcept
    {
        return *data();
    }

    [[nodiscard]] constexpr const Value& back() const noexcept
    {
        return data()[count_ - 1u];
    }

    [[nodiscard]] constexpr const Value& operator[](
        const std::size_t index
    ) const noexcept
    {
        return data()[index];
    }

    [[nodiscard]] constexpr std::span<const Value> span() const noexcept
    {
        return {data(), count_};
    }

    [[nodiscard]] constexpr operator std::span<const Value>() const noexcept
    {
        return span();
    }

private:
    const Value* base_ = nullptr;
    std::size_t offset_ = 0u;
    std::size_t count_ = 0u;
};

template <typename Value, std::size_t Size>
[[nodiscard]] constexpr descriptor_view<Value> descriptor_slice(
    const std::array<Value, Size>& values,
    std::size_t offset,
    std::size_t count
) noexcept
{
    return {values.data(), offset, count};
}

template <typename Value>
[[nodiscard]] constexpr const Value* find_numeric(
    std::span<const Value> values,
    std::span<const numeric_index_entry> index,
    std::uint32_t key
) noexcept
{
    std::size_t begin = 0;
    std::size_t end = index.size();
    while (begin < end)
    {
        const std::size_t middle = begin + (end - begin) / 2;
        if (index[middle].key < key)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == index.size() || index[begin].key != key ||
        index[begin].ordinal >= values.size())
    {
        return nullptr;
    }
    return &values[index[begin].ordinal];
}

template <typename Value, typename KeyProjection>
[[nodiscard]] constexpr bool numeric_index_valid(
    std::span<const Value> values,
    std::span<const numeric_index_entry> index,
    KeyProjection key_projection
) noexcept
{
    if (index.size() != values.size()) return false;

    std::uint32_t previous_key = 0;
    for (std::size_t ordinal = 0; ordinal < index.size(); ++ordinal)
    {
        const auto& entry = index[ordinal];
        if (entry.key == 0 || entry.ordinal >= values.size() ||
            (ordinal != 0 && previous_key >= entry.key) ||
            static_cast<std::uint32_t>(
                key_projection(values[entry.ordinal])
            ) != entry.key)
        {
            return false;
        }
        previous_key = entry.key;
    }
    return true;
}

template <typename Value>
[[nodiscard]] constexpr const Value* find_named(
    std::span<const Value> values,
    std::span<const name_index_entry> index,
    std::string_view key
) noexcept
{
    std::size_t begin = 0;
    std::size_t end = index.size();
    while (begin < end)
    {
        const std::size_t middle = begin + (end - begin) / 2;
        if (index[middle].key < key)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == index.size() || index[begin].key != key ||
        index[begin].ordinal >= values.size())
    {
        return nullptr;
    }
    return &values[index[begin].ordinal];
}

template <typename Traits>
struct basic_schema_catalog_view
{
    using field_descriptor_type = typename Traits::field_descriptor_type;
    using relation_descriptor_type = typename Traits::relation_descriptor_type;
    using model_descriptor_type = typename Traits::model_descriptor_type;
    using wire_field_descriptor_type = typename Traits::wire_field_descriptor_type;
    using wire_message_descriptor_type = typename Traits::wire_message_descriptor_type;
    using command_descriptor_type = typename Traits::command_descriptor_type;
    using event_descriptor_type = typename Traits::event_descriptor_type;
    using catalog_fragment_descriptor_type =
        typename Traits::catalog_fragment_descriptor_type;

    std::uint32_t idl_format_version = 0;
    std::uint32_t edition = 0;
    std::uint32_t schema_version = 0;
    std::string_view source_path;
    std::string_view schema_name;
    std::string_view owner_capsule;
    std::string_view semantic_fingerprint;
    std::string_view allocation_fingerprint;
    std::span<const field_descriptor_type> fields;
    std::span<const relation_descriptor_type> relations;
    std::span<const model_descriptor_type> models;
    std::span<const wire_field_descriptor_type> wire_fields;
    std::span<const wire_message_descriptor_type> wire_messages;
    std::span<const command_descriptor_type> commands;
    std::span<const event_descriptor_type> events;
    std::span<const numeric_index_entry> model_id_index;
    std::span<const name_index_entry> model_name_index;
    std::span<const numeric_index_entry> wire_message_id_index;
    std::span<const numeric_index_entry> command_id_index;
    std::span<const name_index_entry> command_name_index;
    std::span<const numeric_index_entry> event_id_index;
    std::span<const name_index_entry> event_name_index;
    std::string_view catalog_id;
    std::uint32_t catalog_version = 0;
    std::span<const catalog_fragment_descriptor_type> catalog_fragments;

    [[nodiscard]] constexpr schema_contract_ref contract() const noexcept
    {
        return {
            .schema_name = schema_name,
            .schema_version = schema_version,
            .owner_capsule = owner_capsule,
            .semantic_fingerprint = semantic_fingerprint,
            .allocation_fingerprint = allocation_fingerprint,
        };
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model(
        ModelTypeId type_id
    ) const noexcept
    {
        return find_numeric(models, model_id_index, type_id.value);
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model(
        std::string_view model_name
    ) const noexcept
    {
        return find_named(models, model_name_index, model_name);
    }

    [[nodiscard]] constexpr const field_descriptor_type* find_field(
        ModelTypeId model_type_id,
        FieldId field_id
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_field(field_id);
    }

    [[nodiscard]] constexpr const wire_message_descriptor_type* find_wire_message(
        ModelTypeId model_type_id
    ) const noexcept
    {
        return find_numeric(wire_messages, wire_message_id_index, model_type_id.value);
    }

    [[nodiscard]] constexpr const command_descriptor_type* find_command(
        CommandId command_id
    ) const noexcept
    {
        return find_numeric(commands, command_id_index, command_id.value);
    }

    [[nodiscard]] constexpr const command_descriptor_type* find_command(
        std::string_view command_name
    ) const noexcept
    {
        return find_named(commands, command_name_index, command_name);
    }

    [[nodiscard]] constexpr const event_descriptor_type* find_event(
        EventId event_id
    ) const noexcept
    {
        return find_numeric(events, event_id_index, event_id.value);
    }

    [[nodiscard]] constexpr const event_descriptor_type* find_event(
        std::string_view event_name
    ) const noexcept
    {
        return find_named(events, event_name_index, event_name);
    }

    [[nodiscard]] constexpr const field_descriptor_type* find_field(
        ModelTypeId model_type_id,
        std::string_view field_name
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_field(field_name);
    }

    [[nodiscard]] constexpr const relation_descriptor_type* find_relation(
        ModelTypeId model_type_id,
        std::string_view relation_name
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_relation(relation_name);
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        if (!numeric_index_valid(
                models,
                model_id_index,
                [](const auto& model) { return model.type_id.value; }
            ) ||
            !numeric_index_valid(
                wire_messages,
                wire_message_id_index,
                [](const auto& message) { return message.model_type_id.value; }
            ) ||
            !numeric_index_valid(
                commands,
                command_id_index,
                [](const auto& command) { return command.command_id.value; }
            ) ||
            !numeric_index_valid(
                events,
                event_id_index,
                [](const auto& event) { return event.event_id.value; }
            ))
        {
            return false;
        }

        for (const auto& model : models)
        {
            if (!numeric_index_valid(
                    model.fields,
                    model.field_id_index,
                    [](const auto& field) { return field.field_id.value; }
                ))
            {
                return false;
            }
        }
        for (const auto& message : wire_messages)
        {
            if (!numeric_index_valid(
                    message.fields.span(),
                    message.field_tag_index,
                    [](const auto& field) { return field.field_tag.value; }
                ))
            {
                return false;
            }
        }
        return true;
    }
};

template <typename CatalogView>
struct schema_catalog_model_registry_entry
{
    const CatalogView* source = nullptr;
    const typename CatalogView::model_descriptor_type* model = nullptr;
};

template <typename CatalogView>
struct schema_catalog_command_registry_entry
{
    const CatalogView* source = nullptr;
    const typename CatalogView::command_descriptor_type* command = nullptr;
};

template <typename CatalogView>
struct schema_catalog_event_registry_entry
{
    const CatalogView* source = nullptr;
    const typename CatalogView::event_descriptor_type* event = nullptr;
};

enum class schema_model_name_index_kind : std::uint8_t
{
    qualified_name = 0,
    catalog_model_id = 1,
    storage_name = 2,
};

template <typename CatalogView>
[[nodiscard]] constexpr bool schema_registry_contains_source(
    std::span<const CatalogView* const> sources,
    const CatalogView* candidate
) noexcept
{
    for (const auto* source : sources)
        if (source == candidate) return true;
    return false;
}

template <typename CatalogView>
[[nodiscard]] constexpr bool schema_registry_model_index_valid(
    std::span<const schema_catalog_model_registry_entry<CatalogView>> models,
    std::span<const name_index_entry> index,
    schema_model_name_index_kind kind
) noexcept
{
    std::size_t expected_size = 0;
    for (const auto& entry : models)
    {
        if (entry.model == nullptr) return false;
        std::string_view key;
        switch (kind)
        {
        case schema_model_name_index_kind::qualified_name:
            key = entry.model->qualified_name;
            break;
        case schema_model_name_index_kind::catalog_model_id:
            key = entry.model->catalog_model_id;
            break;
        case schema_model_name_index_kind::storage_name:
            key = entry.model->storage_name;
            break;
        }
        if (!key.empty()) ++expected_size;
    }
    if (index.size() != expected_size) return false;

    std::string_view previous;
    for (std::size_t ordinal = 0; ordinal < index.size(); ++ordinal)
    {
        const auto& entry = index[ordinal];
        if (entry.key.empty() || entry.ordinal >= models.size() ||
            (ordinal != 0 && previous >= entry.key))
        {
            return false;
        }
        const auto* model = models[entry.ordinal].model;
        if (model == nullptr) return false;
        std::string_view expected;
        switch (kind)
        {
        case schema_model_name_index_kind::qualified_name:
            expected = model->qualified_name;
            break;
        case schema_model_name_index_kind::catalog_model_id:
            expected = model->catalog_model_id;
            break;
        case schema_model_name_index_kind::storage_name:
            expected = model->storage_name;
            break;
        }
        if (entry.key != expected) return false;
        previous = entry.key;
    }
    return true;
}

template <typename CatalogView>
struct basic_schema_catalog_registry_view
{
    using model_registry_entry_type = schema_catalog_model_registry_entry<CatalogView>;
    using command_registry_entry_type =
        schema_catalog_command_registry_entry<CatalogView>;
    using event_registry_entry_type = schema_catalog_event_registry_entry<CatalogView>;
    using model_descriptor_type = typename CatalogView::model_descriptor_type;
    using command_descriptor_type = typename CatalogView::command_descriptor_type;
    using event_descriptor_type = typename CatalogView::event_descriptor_type;

    std::string_view semantic_fingerprint;
    std::string_view allocation_fingerprint;
    std::span<const CatalogView* const> sources;
    std::span<const CatalogView* const> model_sources;
    std::span<const model_registry_entry_type> models;
    std::span<const name_index_entry> model_qualified_name_index;
    std::span<const name_index_entry> model_catalog_id_index;
    std::span<const name_index_entry> model_storage_name_index;
    std::span<const command_registry_entry_type> commands;
    std::span<const event_registry_entry_type> events;

    [[nodiscard]] constexpr const CatalogView* find_source(
        std::string_view schema_name,
        std::uint32_t schema_version
    ) const noexcept
    {
        std::size_t begin = 0;
        std::size_t end = sources.size();
        while (begin < end)
        {
            const std::size_t middle = begin + (end - begin) / 2;
            const auto* source = sources[middle];
            if (source->schema_name < schema_name ||
                (source->schema_name == schema_name &&
                 source->schema_version < schema_version))
            {
                begin = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        if (begin == sources.size() || sources[begin]->schema_name != schema_name ||
            sources[begin]->schema_version != schema_version)
        {
            return nullptr;
        }
        return sources[begin];
    }

    [[nodiscard]] constexpr const model_registry_entry_type* locate_model(
        ModelTypeId type_id
    ) const noexcept
    {
        std::size_t begin = 0;
        std::size_t end = models.size();
        while (begin < end)
        {
            const std::size_t middle = begin + (end - begin) / 2;
            if (models[middle].model->type_id < type_id)
                begin = middle + 1;
            else
                end = middle;
        }
        if (begin == models.size() || models[begin].model->type_id != type_id)
            return nullptr;
        return &models[begin];
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model(
        ModelTypeId type_id
    ) const noexcept
    {
        const auto* entry = locate_model(type_id);
        return entry == nullptr ? nullptr : entry->model;
    }

    [[nodiscard]] constexpr const model_registry_entry_type*
    locate_model_by_qualified_name(std::string_view qualified_name) const noexcept
    {
        return find_named(models, model_qualified_name_index, qualified_name);
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model_by_qualified_name(
        std::string_view qualified_name
    ) const noexcept
    {
        const auto* entry = locate_model_by_qualified_name(qualified_name);
        return entry == nullptr ? nullptr : entry->model;
    }

    [[nodiscard]] constexpr const model_registry_entry_type*
    locate_model_by_catalog_id(std::string_view catalog_model_id) const noexcept
    {
        return find_named(models, model_catalog_id_index, catalog_model_id);
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model_by_catalog_id(
        std::string_view catalog_model_id
    ) const noexcept
    {
        const auto* entry = locate_model_by_catalog_id(catalog_model_id);
        return entry == nullptr ? nullptr : entry->model;
    }

    [[nodiscard]] constexpr const model_registry_entry_type*
    locate_model_by_storage_name(std::string_view storage_name) const noexcept
    {
        return find_named(models, model_storage_name_index, storage_name);
    }

    [[nodiscard]] constexpr const model_descriptor_type* find_model_by_storage_name(
        std::string_view storage_name
    ) const noexcept
    {
        const auto* entry = locate_model_by_storage_name(storage_name);
        return entry == nullptr ? nullptr : entry->model;
    }

    [[nodiscard]] constexpr const command_descriptor_type*
    find_command_by_qualified_name(std::string_view qualified_name) const noexcept
    {
        std::size_t begin = 0;
        std::size_t end = commands.size();
        while (begin < end)
        {
            const std::size_t middle = begin + (end - begin) / 2;
            if (commands[middle].command->qualified_name < qualified_name)
                begin = middle + 1;
            else
                end = middle;
        }
        if (begin == commands.size() ||
            commands[begin].command->qualified_name != qualified_name)
        {
            return nullptr;
        }
        return commands[begin].command;
    }

    [[nodiscard]] constexpr const event_descriptor_type*
    find_event_by_qualified_name(std::string_view qualified_name) const noexcept
    {
        std::size_t begin = 0;
        std::size_t end = events.size();
        while (begin < end)
        {
            const std::size_t middle = begin + (end - begin) / 2;
            if (events[middle].event->qualified_name < qualified_name)
                begin = middle + 1;
            else
                end = middle;
        }
        if (begin == events.size() ||
            events[begin].event->qualified_name != qualified_name)
        {
            return nullptr;
        }
        return events[begin].event;
    }

    [[nodiscard]] constexpr const command_descriptor_type* find_command(
        std::string_view schema_name,
        std::uint32_t schema_version,
        CommandId command_id
    ) const noexcept
    {
        const auto* source = find_source(schema_name, schema_version);
        return source == nullptr ? nullptr : source->find_command(command_id);
    }

    [[nodiscard]] constexpr const event_descriptor_type* find_event(
        std::string_view schema_name,
        std::uint32_t schema_version,
        EventId event_id
    ) const noexcept
    {
        const auto* source = find_source(schema_name, schema_version);
        return source == nullptr ? nullptr : source->find_event(event_id);
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        if (sources.empty() || semantic_fingerprint.empty() ||
            allocation_fingerprint.empty())
        {
            return false;
        }
        std::size_t expected_command_count = 0;
        std::size_t expected_event_count = 0;
        for (std::size_t ordinal = 0; ordinal < sources.size(); ++ordinal)
        {
            const auto* source = sources[ordinal];
            if (source == nullptr || source->schema_name.empty() ||
                source->schema_version == 0 || source->semantic_fingerprint.empty() ||
                source->allocation_fingerprint.empty() || !source->valid())
            {
                return false;
            }
            if (ordinal != 0)
            {
                const auto* previous = sources[ordinal - 1];
                if (previous->schema_name > source->schema_name ||
                    (previous->schema_name == source->schema_name &&
                     previous->schema_version >= source->schema_version))
                {
                    return false;
                }
            }
            expected_command_count += source->commands.size();
            expected_event_count += source->events.size();
        }

        ModelTypeId previous_type_id;
        std::size_t expected_model_count = 0;
        for (std::size_t ordinal = 0; ordinal < model_sources.size(); ++ordinal)
        {
            const auto* source = model_sources[ordinal];
            if (source == nullptr ||
                !schema_registry_contains_source(sources, source))
            {
                return false;
            }
            if (ordinal != 0)
            {
                const auto* previous = model_sources[ordinal - 1];
                if (previous->schema_name > source->schema_name ||
                    (previous->schema_name == source->schema_name &&
                     previous->schema_version >= source->schema_version))
                {
                    return false;
                }
            }
            expected_model_count += source->models.size();
        }
        if (models.size() != expected_model_count ||
            commands.size() != expected_command_count ||
            events.size() != expected_event_count)
        {
            return false;
        }

        for (std::size_t ordinal = 0; ordinal < models.size(); ++ordinal)
        {
            const auto& entry = models[ordinal];
            if (entry.source == nullptr || entry.model == nullptr ||
                !schema_registry_contains_source(model_sources, entry.source) ||
                entry.source->find_model(entry.model->type_id) != entry.model ||
                !entry.model->type_id.valid() ||
                (ordinal != 0 && previous_type_id >= entry.model->type_id))
            {
                return false;
            }
            previous_type_id = entry.model->type_id;
        }
        if (!schema_registry_model_index_valid(
                models,
                model_qualified_name_index,
                schema_model_name_index_kind::qualified_name
            ) ||
            !schema_registry_model_index_valid(
                models,
                model_catalog_id_index,
                schema_model_name_index_kind::catalog_model_id
            ) ||
            !schema_registry_model_index_valid(
                models,
                model_storage_name_index,
                schema_model_name_index_kind::storage_name
            ))
        {
            return false;
        }

        std::string_view previous_command_name;
        for (std::size_t ordinal = 0; ordinal < commands.size(); ++ordinal)
        {
            const auto& entry = commands[ordinal];
            if (entry.source == nullptr || entry.command == nullptr ||
                !schema_registry_contains_source(sources, entry.source) ||
                entry.source->find_command(entry.command->command_id) != entry.command ||
                entry.command->qualified_name.empty() ||
                (ordinal != 0 &&
                 previous_command_name >= entry.command->qualified_name))
            {
                return false;
            }
            previous_command_name = entry.command->qualified_name;
        }

        std::string_view previous_event_name;
        for (std::size_t ordinal = 0; ordinal < events.size(); ++ordinal)
        {
            const auto& entry = events[ordinal];
            if (entry.source == nullptr || entry.event == nullptr ||
                !schema_registry_contains_source(sources, entry.source) ||
                entry.source->find_event(entry.event->event_id) != entry.event ||
                entry.event->qualified_name.empty() ||
                (ordinal != 0 && previous_event_name >= entry.event->qualified_name))
            {
                return false;
            }
            previous_event_name = entry.event->qualified_name;
        }
        return true;
    }
};

struct catalog_identity_field_view
{
    ModelTypeId model_type_id;
    FieldId field_id;
    std::string_view name;
    std::string_view storage_name;
    std::string_view catalog_registry_key;
    std::string_view catalog_field_name;
};

struct catalog_identity_model_view
{
    ModelTypeId type_id;
    std::string_view name;
    std::string_view storage_name;
    std::string_view qualified_name;
    std::string_view owner_capsule;
    std::span<const catalog_identity_field_view> fields;
    std::string_view catalog_fragment_id;
    std::string_view catalog_registry_key;
    std::string_view catalog_model_id;
    std::string_view catalog_logical_name;
    std::string_view catalog_qualified_name;
};

struct catalog_identity_fragment_view
{
    std::string_view fragment_id;
    std::string_view owner_capsule;
    std::uint32_t version = 0;
    std::uint32_t declaration_order = 0;
};

struct catalog_identity_view
{
    std::uint32_t schema_version = 0;
    std::string_view schema_name;
    std::string_view owner_capsule;
    std::span<const catalog_identity_model_view> models;
    std::string_view catalog_id;
    std::uint32_t catalog_version = 0;
    std::span<const catalog_identity_fragment_view> catalog_fragments;
};

struct catalog_identity_storage
{
    std::vector<catalog_identity_field_view> fields;
    std::vector<catalog_identity_model_view> models;
    std::vector<catalog_identity_fragment_view> fragments;
};

struct catalog_view_validation_diagnostic
{
    std::string code;
    std::string model_id;
    std::string path;
    std::string message;
};

struct catalog_view_validation_result
{
    std::vector<catalog_view_validation_diagnostic> errors;

    [[nodiscard]] bool ok() const { return errors.empty(); }
};

template <typename CatalogView>
[[nodiscard]] catalog_view_validation_result ValidateCatalogView(
    const CatalogView& catalog
)
{
    catalog_view_validation_result result;
    const auto add_error = [&result](
                               std::string code,
                               std::string_view model_id,
                               std::string path,
                               std::string message) {
        result.errors.push_back({
            .code = std::move(code),
            .model_id = std::string(model_id),
            .path = std::move(path),
            .message = std::move(message),
        });
    };

    const bool has_catalog_identity = !catalog.catalog_id.empty();
    if (catalog.schema_name.empty())
    {
        add_error(
            "schema_name_required", {}, "schema_name", "Schema name is required."
        );
    }
    if (catalog.schema_version == 0)
    {
        add_error(
            "schema_version_invalid",
            {},
            "schema_version",
            "Schema version must be positive."
        );
    }
    if (has_catalog_identity && catalog.catalog_version == 0)
    {
        add_error(
            "catalog_version_invalid",
            {},
            "catalog_version",
            "Catalog version must be positive."
        );
    }

    std::map<std::string_view, std::string_view> fragment_owners;
    for (const auto& fragment : catalog.catalog_fragments)
    {
        if (fragment.fragment_id.empty() || fragment.owner_capsule.empty())
        {
            add_error(
                "fragment_identity_required",
                {},
                "catalog_fragments",
                "Fragment id and owner capsule are required."
            );
        }
        else if (!fragment_owners.emplace(
                     fragment.fragment_id, fragment.owner_capsule
                 ).second)
        {
            add_error(
                "duplicate_fragment_id",
                {},
                "catalog_fragments." + std::string(fragment.fragment_id),
                "Fragment id must be unique."
            );
        }
        if (fragment.version == 0)
        {
            add_error(
                "fragment_version_invalid",
                {},
                "catalog_fragments." + std::string(fragment.fragment_id),
                "Fragment version must be positive."
            );
        }
    }

    std::set<std::uint32_t> type_ids;
    std::set<std::string_view> model_registry_keys;
    std::set<std::string_view> model_ids;
    std::set<std::string_view> logical_names;
    std::set<std::string_view> storage_names;
    std::set<std::string_view> qualified_names;
    for (const auto& model : catalog.models)
    {
        const std::string_view model_id = model.catalog_model_id.empty()
            ? model.name
            : model.catalog_model_id;
        const std::string_view logical_name = model.catalog_logical_name.empty()
            ? model.name
            : model.catalog_logical_name;
        const std::string_view qualified_name = model.catalog_qualified_name.empty()
            ? model.qualified_name
            : model.catalog_qualified_name;
        const std::string_view registry_key = model.catalog_registry_key.empty()
            ? qualified_name
            : model.catalog_registry_key;
        if (!model.type_id.valid() || model.name.empty() ||
            model.storage_name.empty() || qualified_name.empty() ||
            model.owner_capsule.empty())
        {
            add_error(
                "model_identity_required",
                model_id,
                "model",
                "Model type id, names, and owner capsule are required."
            );
        }
        if (!type_ids.insert(model.type_id.value).second)
        {
            add_error(
                "duplicate_model_type_id",
                model_id,
                "type_id",
                "Model type id must be globally unique."
            );
        }
        if (!registry_key.empty() && !model_registry_keys.insert(registry_key).second)
        {
            add_error(
                "duplicate_model_registry_key",
                model_id,
                "catalog_registry_key",
                "Model registry keys must be globally unique."
            );
        }
        if (!model_ids.insert(model_id).second)
        {
            add_error(
                "duplicate_model_id",
                model_id,
                "catalog_model_id",
                "Model id must be unique."
            );
        }
        if (!logical_names.insert(logical_name).second)
        {
            add_error(
                "duplicate_logical_name",
                model_id,
                "catalog_logical_name",
                "Logical model name must be unique."
            );
        }
        if (!storage_names.insert(model.storage_name).second)
        {
            add_error(
                "duplicate_storage_name",
                model_id,
                "storage_name",
                "Storage name must be unique."
            );
        }
        if (!qualified_names.insert(qualified_name).second)
        {
            add_error(
                "duplicate_qualified_name",
                model_id,
                "qualified_name",
                "Qualified model name must be unique."
            );
        }
        if (qualified_name.find('.') == std::string_view::npos)
        {
            add_error(
                "qualified_name_invalid",
                model_id,
                "qualified_name",
                "Qualified model names require a domain namespace."
            );
        }
        if (has_catalog_identity)
        {
            const auto fragment = fragment_owners.find(model.catalog_fragment_id);
            if (model.catalog_fragment_id.empty() || fragment == fragment_owners.end())
            {
                add_error(
                    "model_fragment_required",
                    model_id,
                    "catalog_fragment_id",
                    "Catalog models require a declared fragment."
                );
            }
            else if (fragment->second != model.owner_capsule)
            {
                add_error(
                    "model_owner_mismatch",
                    model_id,
                    "owner_capsule",
                    "Model owner must match its fragment owner."
                );
            }
            if (model.catalog_registry_key.empty() ||
                model.catalog_model_id.empty() ||
                model.catalog_logical_name.empty() ||
                model.catalog_qualified_name.empty())
            {
                add_error(
                    "catalog_model_identity_required",
                    model_id,
                    "catalog_identity",
                    "Catalog model identity fields are required."
                );
            }
        }

        std::set<std::uint32_t> field_ids;
        std::set<std::string_view> field_registry_keys;
        std::set<std::string_view> field_names;
        std::set<std::string_view> field_storage_names;
        for (const auto& field : model.fields)
        {
            const std::string_view field_name = field.catalog_field_name.empty()
                ? field.name
                : field.catalog_field_name;
            const std::string path = "fields." + std::string(field_name);
            if (field.model_type_id != model.type_id)
            {
                add_error(
                    "field_model_type_mismatch",
                    model_id,
                    path,
                    "Field model type id must match its owning model."
                );
            }
            if (!field.field_id.valid())
            {
                add_error(
                    "field_id_required",
                    model_id,
                    path,
                    "Every field requires a nonzero stable u32 field id."
                );
            }
            else if (!field_ids.insert(field.field_id.value).second)
            {
                add_error(
                    "duplicate_field_id",
                    model_id,
                    path,
                    "Field ids must be unique within a model."
                );
            }
            if (field_name.empty() || field.storage_name.empty())
            {
                add_error(
                    "field_identity_required",
                    model_id,
                    path,
                    "Field name and storage name are required."
                );
            }
            if (!field_names.insert(field_name).second)
            {
                add_error(
                    "duplicate_field_name",
                    model_id,
                    path,
                    "Field name must be unique within a model."
                );
            }
            if (!field_storage_names.insert(field.storage_name).second)
            {
                add_error(
                    "duplicate_field_storage_name",
                    model_id,
                    path,
                    "Field storage name must be unique within a model."
                );
            }
            if (has_catalog_identity)
            {
                if (field.catalog_registry_key.empty())
                {
                    add_error(
                        "field_registry_key_required",
                        model_id,
                        path,
                        "Every catalog field requires a durable registry key."
                    );
                }
                else if (!field_registry_keys.insert(
                             field.catalog_registry_key
                         ).second)
                {
                    add_error(
                        "duplicate_field_registry_key",
                        model_id,
                        path,
                        "Field registry keys must be unique within a model."
                    );
                }
            }
        }
    }
    return result;
}

struct SchemaCatalog;

[[nodiscard]] catalog_identity_view BuildCatalogIdentityView(
    const SchemaCatalog& catalog,
    catalog_identity_storage& storage
);
} // namespace arken::schema::core::v1
