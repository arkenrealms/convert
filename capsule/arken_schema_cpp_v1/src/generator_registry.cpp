#include "generator_registry.hpp"

#include "arken_schema_cpp_v1/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace arken::schema::cpp::v1::generator_detail
{
namespace idl = ::arken::schema::idl::v1;

namespace
{
struct ModelRegistryRow
{
    const CppRegistrySourceInfo* source = nullptr;
    const idl::SemanticModel* model = nullptr;
};

struct CommandRegistryRow
{
    const CppRegistrySourceInfo* source = nullptr;
    const idl::SemanticCommand* command = nullptr;
};

struct EventRegistryRow
{
    const CppRegistrySourceInfo* source = nullptr;
    const idl::SemanticEvent* event = nullptr;
};

struct ModelNameIndexRow
{
    std::string_view key;
    std::uint32_t ordinal = 0;
    idl::SemanticProvenance provenance;
};

bool HasErrors(const std::vector<idl::Diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == idl::DiagnosticSeverity::Error;
    });
}

void AddDiagnostic(
    CppRegistrySection& result,
    std::string code,
    std::string message,
    const idl::SemanticProvenance& provenance = {}
)
{
    result.diagnostics.push_back({
        .code = std::move(code),
        .severity = idl::DiagnosticSeverity::Error,
        .message = std::move(message),
        .sourceName = provenance.sourcePath,
        .span = provenance.span,
    });
}

std::string StringLiteral(std::string_view value)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20)
            {
                output << '\\' << std::oct << std::setw(3) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            }
            else
            {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

const CppRegistrySourceInfo* SourceInfo(
    const std::vector<CppRegistrySourceInfo>& sources,
    idl::SourceHandle handle
)
{
    if (!handle.valid() || handle.value >= sources.size()) return nullptr;
    return sources[handle.value].source == nullptr ? nullptr : &sources[handle.value];
}

std::string CatalogExpression(
    const CppRegistrySourceInfo& source,
    const idl::SemanticSource& entry_source
)
{
    if (source.source->handle == entry_source.handle) return "schema_catalog";
    return "::" + source.namespaceName + "::schema_catalog";
}

std::string CatalogPointerExpression(
    const CppRegistrySourceInfo& source,
    const idl::SemanticSource& entry_source
)
{
    return "&" + CatalogExpression(source, entry_source);
}

void ValidateSourceRows(
    CppRegistrySection& result,
    const std::vector<const CppRegistrySourceInfo*>& sources
)
{
    for (std::size_t ordinal = 1; ordinal < sources.size(); ++ordinal)
    {
        const auto& previous = *sources[ordinal - 1]->source;
        const auto& current = *sources[ordinal]->source;
        if (previous.schemaName != current.schemaName ||
            previous.schemaVersion != current.schemaVersion)
        {
            continue;
        }
        AddDiagnostic(
            result,
            "schema.cpp.registry_source_duplicate",
            "Generated schema registry has duplicate schema contract identity '" +
                current.schemaName + ".v" + std::to_string(current.schemaVersion) + "'.",
            {.sourcePath = current.path}
        );
    }
}

void ValidateModelRows(CppRegistrySection& result, const std::vector<ModelRegistryRow>& models)
{
    for (std::size_t ordinal = 1; ordinal < models.size(); ++ordinal)
    {
        if (models[ordinal - 1].model->typeId != models[ordinal].model->typeId) continue;
        AddDiagnostic(
            result,
            "schema.cpp.registry_model_type_id_duplicate",
            "Generated schema registry has duplicate model type id " +
                std::to_string(models[ordinal].model->typeId.value) + ".",
            models[ordinal].model->provenance
        );
    }

    const auto validate_names = [&result, &models](
                                    auto projection,
                                    std::string_view code,
                                    std::string_view subject,
                                    bool skip_empty) {
        std::vector<const ModelRegistryRow*> ordered;
        ordered.reserve(models.size());
        for (const auto& model : models)
        {
            if (skip_empty && projection(model).empty()) continue;
            ordered.push_back(&model);
        }
        std::sort(ordered.begin(), ordered.end(), [&](const auto* lhs, const auto* rhs) {
            return std::tuple{projection(*lhs), lhs->model->typeId.value} <
                std::tuple{projection(*rhs), rhs->model->typeId.value};
        });
        for (std::size_t ordinal = 1; ordinal < ordered.size(); ++ordinal)
        {
            if (projection(*ordered[ordinal - 1]) != projection(*ordered[ordinal])) continue;
            AddDiagnostic(
                result,
                std::string(code),
                "Generated schema registry has duplicate " + std::string(subject) + " '" +
                    std::string(projection(*ordered[ordinal])) + "'.",
                ordered[ordinal]->model->provenance
            );
        }
    };
    validate_names(
        [](const auto& row) -> std::string_view { return row.model->qualifiedName; },
        "schema.cpp.registry_model_qualified_name_duplicate",
        "model qualified name",
        false
    );
    validate_names(
        [](const auto& row) -> std::string_view {
            return row.model->catalogIdentity.has_value()
                ? std::string_view(row.model->catalogIdentity->modelId)
                : std::string_view{};
        },
        "schema.cpp.registry_catalog_model_id_duplicate",
        "catalog model id",
        true
    );
    validate_names(
        [](const auto& row) -> std::string_view { return row.model->storageName; },
        "schema.cpp.registry_model_storage_name_duplicate",
        "model storage name",
        false
    );
}

template <typename Row, typename Projection, typename Provenance>
void ValidateQualifiedRows(
    CppRegistrySection& result,
    const std::vector<Row>& rows,
    Projection projection,
    Provenance provenance,
    std::string_view code,
    std::string_view subject
)
{
    for (std::size_t ordinal = 1; ordinal < rows.size(); ++ordinal)
    {
        if (projection(rows[ordinal - 1]) != projection(rows[ordinal])) continue;
        AddDiagnostic(
            result,
            std::string(code),
            "Generated schema registry has duplicate " + std::string(subject) + " '" +
                std::string(projection(rows[ordinal])) + "'.",
            provenance(rows[ordinal])
        );
    }
}

std::vector<ModelNameIndexRow> ModelNameIndex(
    const std::vector<ModelRegistryRow>& models,
    const std::map<std::uint32_t, std::uint32_t>& type_ordinals,
    schema_model_name_index_kind kind
)
{
    std::vector<ModelNameIndexRow> result;
    result.reserve(models.size());
    for (const auto& row : models)
    {
        std::string_view key;
        switch (kind)
        {
        case schema_model_name_index_kind::qualified_name:
            key = row.model->qualifiedName;
            break;
        case schema_model_name_index_kind::catalog_model_id:
            if (row.model->catalogIdentity.has_value())
                key = row.model->catalogIdentity->modelId;
            break;
        case schema_model_name_index_kind::storage_name:
            key = row.model->storageName;
            break;
        }
        if (key.empty()) continue;
        result.push_back({
            .key = key,
            .ordinal = type_ordinals.at(row.model->typeId.value),
            .provenance = row.model->provenance,
        });
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.key, lhs.ordinal) < std::tie(rhs.key, rhs.ordinal);
    });
    return result;
}

void EmitModelNameIndex(
    std::ostringstream& output,
    std::string_view name,
    const std::vector<ModelNameIndexRow>& rows
)
{
    output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
           << rows.size() << "u> " << name << "{{\n";
    for (const auto& row : rows)
    {
        output << "    {.key = " << StringLiteral(row.key)
               << ", .ordinal = " << row.ordinal << "u},\n";
    }
    output << "}};\n";
}
} // namespace

bool CppRegistrySection::ok() const
{
    return !HasErrors(diagnostics);
}

CppRegistrySection GenerateCppRegistrySection(
    const idl::SemanticManifest& manifest,
    std::string_view allocation_fingerprint,
    const idl::SemanticSource& entry_source,
    const std::vector<CppRegistrySourceInfo>& sources
)
{
    CppRegistrySection result;

    std::vector<const CppRegistrySourceInfo*> ordered_sources;
    ordered_sources.reserve(sources.size());
    for (const auto& source : sources)
        if (source.source != nullptr) ordered_sources.push_back(&source);
    std::sort(ordered_sources.begin(), ordered_sources.end(), [](const auto* lhs, const auto* rhs) {
        return std::tie(
                   lhs->source->schemaName,
                   lhs->source->schemaVersion,
                   lhs->source->path
               ) <
            std::tie(
                   rhs->source->schemaName,
                   rhs->source->schemaVersion,
                   rhs->source->path
               );
    });
    ValidateSourceRows(result, ordered_sources);

    std::vector<ModelRegistryRow> models;
    for (const auto& model : manifest.ir.models)
    {
        if (model.external) continue;
        const auto* source = SourceInfo(sources, model.source);
        if (source == nullptr)
        {
            AddDiagnostic(
                result,
                "schema.cpp.registry_model_source_unresolved",
                "Generated schema registry model source is unresolved.",
                model.provenance
            );
            continue;
        }
        models.push_back({.source = source, .model = &model});
    }
    std::sort(models.begin(), models.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.model->typeId.value, lhs.model->qualifiedName) <
            std::tie(rhs.model->typeId.value, rhs.model->qualifiedName);
    });
    ValidateModelRows(result, models);

    std::map<std::uint32_t, std::uint32_t> model_ordinals;
    std::set<idl::SourceHandle> model_source_handles;
    for (const auto& model : manifest.ir.models)
        model_source_handles.insert(model.source);
    for (std::size_t ordinal = 0; ordinal < models.size(); ++ordinal)
    {
        model_ordinals.emplace(
            models[ordinal].model->typeId.value,
            static_cast<std::uint32_t>(ordinal)
        );
    }
    std::vector<const CppRegistrySourceInfo*> model_sources;
    for (const auto* source : ordered_sources)
    {
        if (model_source_handles.contains(source->source->handle))
            model_sources.push_back(source);
    }
    const auto qualified_name_index = ModelNameIndex(
        models, model_ordinals, schema_model_name_index_kind::qualified_name
    );
    const auto catalog_model_id_index = ModelNameIndex(
        models, model_ordinals, schema_model_name_index_kind::catalog_model_id
    );
    const auto storage_name_index = ModelNameIndex(
        models, model_ordinals, schema_model_name_index_kind::storage_name
    );

    std::vector<CommandRegistryRow> commands;
    for (const auto& command : manifest.ir.commands)
    {
        const auto* source = SourceInfo(sources, command.source);
        if (source == nullptr)
        {
            AddDiagnostic(
                result,
                "schema.cpp.registry_command_source_unresolved",
                "Generated schema registry command source is unresolved.",
                command.provenance
            );
            continue;
        }
        commands.push_back({.source = source, .command = &command});
    }
    std::sort(commands.begin(), commands.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.command->qualifiedName, lhs.command->commandId.value) <
            std::tie(rhs.command->qualifiedName, rhs.command->commandId.value);
    });
    ValidateQualifiedRows(
        result,
        commands,
        [](const auto& row) -> std::string_view { return row.command->qualifiedName; },
        [](const auto& row) { return row.command->provenance; },
        "schema.cpp.registry_command_qualified_name_duplicate",
        "command qualified name"
    );

    std::vector<EventRegistryRow> events;
    for (const auto& event : manifest.ir.events)
    {
        const auto* source = SourceInfo(sources, event.source);
        if (source == nullptr)
        {
            AddDiagnostic(
                result,
                "schema.cpp.registry_event_source_unresolved",
                "Generated schema registry event source is unresolved.",
                event.provenance
            );
            continue;
        }
        events.push_back({.source = source, .event = &event});
    }
    std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.event->qualifiedName, lhs.event->eventId.value) <
            std::tie(rhs.event->qualifiedName, rhs.event->eventId.value);
    });
    ValidateQualifiedRows(
        result,
        events,
        [](const auto& row) -> std::string_view { return row.event->qualifiedName; },
        [](const auto& row) { return row.event->provenance; },
        "schema.cpp.registry_event_qualified_name_duplicate",
        "event qualified name"
    );
    if (!result.ok()) return result;

    std::ostringstream output;
    output << "namespace schema_registry_data\n{\n";
    output << "inline constexpr ::std::array<const schema_cpp::schema_catalog_view*, "
           << ordered_sources.size() << "u> sources{{\n";
    for (const auto* source : ordered_sources)
    {
        output << "    " << CatalogPointerExpression(*source, entry_source) << ",\n";
    }
    output << "}};\n\n";
    output << "inline constexpr ::std::array<const schema_cpp::schema_catalog_view*, "
           << model_sources.size() << "u> model_sources{{\n";
    for (const auto* source : model_sources)
    {
        output << "    " << CatalogPointerExpression(*source, entry_source) << ",\n";
    }
    output << "}};\n\n";

    output << "inline constexpr ::std::array<schema_cpp::schema_catalog_model_registry_entry, "
           << models.size() << "u> models{{\n";
    for (const auto& row : models)
    {
        const auto catalog = CatalogExpression(*row.source, entry_source);
        output << "    {.source = &" << catalog << ", .model = "
               << catalog << ".find_model(" << row.model->typeId.value << "u)},\n";
    }
    output << "}};\n";
    EmitModelNameIndex(output, "model_qualified_name_index", qualified_name_index);
    EmitModelNameIndex(output, "model_catalog_id_index", catalog_model_id_index);
    EmitModelNameIndex(output, "model_storage_name_index", storage_name_index);
    output << '\n';

    output << "inline constexpr ::std::array<schema_cpp::schema_catalog_command_registry_entry, "
           << commands.size() << "u> commands{{\n";
    for (const auto& row : commands)
    {
        const auto catalog = CatalogExpression(*row.source, entry_source);
        output << "    {.source = &" << catalog << ", .command = "
               << catalog << ".find_command(" << row.command->commandId.value << "u)},\n";
    }
    output << "}};\n\n";

    output << "inline constexpr ::std::array<schema_cpp::schema_catalog_event_registry_entry, "
           << events.size() << "u> events{{\n";
    for (const auto& row : events)
    {
        const auto catalog = CatalogExpression(*row.source, entry_source);
        output << "    {.source = &" << catalog << ", .event = "
               << catalog << ".find_event(" << row.event->eventId.value << "u)},\n";
    }
    output << "}};\n";
    output << "}\n\n";

    output << "inline constexpr schema_cpp::schema_catalog_registry_view schema_registry{\n"
           << "    .semantic_fingerprint = "
           << StringLiteral(manifest.semanticFingerprint) << ",\n"
           << "    .allocation_fingerprint = "
           << StringLiteral(allocation_fingerprint) << ",\n"
           << "    .sources = schema_cpp::descriptor_span(schema_registry_data::sources),\n"
           << "    .model_sources = schema_cpp::descriptor_span("
              "schema_registry_data::model_sources),\n"
           << "    .models = schema_cpp::descriptor_span(schema_registry_data::models),\n"
           << "    .model_qualified_name_index = schema_cpp::descriptor_span("
              "schema_registry_data::model_qualified_name_index),\n"
           << "    .model_catalog_id_index = schema_cpp::descriptor_span("
              "schema_registry_data::model_catalog_id_index),\n"
           << "    .model_storage_name_index = schema_cpp::descriptor_span("
              "schema_registry_data::model_storage_name_index),\n"
           << "    .commands = schema_cpp::descriptor_span(schema_registry_data::commands),\n"
           << "    .events = schema_cpp::descriptor_span(schema_registry_data::events),\n"
           << "};\n"
           << "static_assert(schema_registry.valid());\n\n";
    result.content = output.str();
    return result;
}
} // namespace arken::schema::cpp::v1::generator_detail
