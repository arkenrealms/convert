#include "arken_schema_idl_v1/allocation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "allocation_internal.hpp"

namespace arken::schema::idl::v1
{
namespace
{
constexpr std::array<RegistryKind, 7> kGlobalRegistryKinds = {
    RegistryKind::Model,
    RegistryKind::SemanticType,
    RegistryKind::WireType,
    RegistryKind::StorageCodec,
    RegistryKind::Enum,
    RegistryKind::Command,
    RegistryKind::Event,
};

SourceSpan EmptySpan()
{
    return {};
}

void AddDiagnostic(
    std::vector<Diagnostic>& diagnostics,
    const ParseLimits& limits,
    std::string code,
    std::string message,
    std::string source_name,
    SourceSpan span = {}
)
{
    if (diagnostics.size() >= limits.maximumDiagnostics) return;
    diagnostics.push_back({
        .code = std::move(code),
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .sourceName = std::move(source_name),
        .span = span,
    });
}

void SortDiagnostics(std::vector<Diagnostic>& diagnostics)
{
    std::sort(diagnostics.begin(), diagnostics.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.sourceName != rhs.sourceName) return lhs.sourceName < rhs.sourceName;
        if (lhs.span.begin.offset != rhs.span.begin.offset)
            return lhs.span.begin.offset < rhs.span.begin.offset;
        if (lhs.code != rhs.code) return lhs.code < rhs.code;
        return lhs.message < rhs.message;
    });
}

bool IsCanonicalQualifiedName(std::string_view value)
{
    const auto first_dot = value.find('.');
    if (first_dot == std::string_view::npos) return false;
    const auto second_dot = value.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos ||
        value.find('.', second_dot + 1) != std::string_view::npos)
    {
        return false;
    }
    const auto schema = value.substr(0, first_dot);
    const auto version = value.substr(first_dot + 1, second_dot - first_dot - 1);
    const auto name = value.substr(second_dot + 1);
    if (!IsCanonicalSnakeCase(schema) || !IsCanonicalSnakeCase(name) ||
        version.size() < 2 || version.front() != 'v' || version[1] == '0')
    {
        return false;
    }
    return std::all_of(version.begin() + 1, version.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

bool IsCanonicalAllocationName(const RegistryScope& scope, std::string_view name)
{
    return scope.kind == RegistryKind::Field || scope.kind == RegistryKind::EnumValue
        ? IsCanonicalSnakeCase(name)
        : IsCanonicalQualifiedName(name);
}

AllocationRegistry* FindRegistry(AllocationLedgerData& data, const RegistryScope& scope)
{
    const auto found = std::find_if(data.registries.begin(), data.registries.end(), [&scope](const auto& registry) {
        return registry.scope == scope;
    });
    return found == data.registries.end() ? nullptr : &*found;
}

const AllocationRegistry* FindRegistry(const AllocationLedgerData& data, const RegistryScope& scope)
{
    const auto found = std::find_if(data.registries.begin(), data.registries.end(), [&scope](const auto& registry) {
        return registry.scope == scope;
    });
    return found == data.registries.end() ? nullptr : &*found;
}

AllocationRegistry& EnsureRegistry(AllocationLedgerData& data, RegistryScope scope)
{
    if (auto* existing = FindRegistry(data, scope); existing != nullptr) return *existing;
    data.registries.push_back({.scope = std::move(scope)});
    return data.registries.back();
}

void Canonicalize(AllocationLedgerData& data)
{
    for (auto& registry : data.registries)
    {
        std::sort(registry.active.begin(), registry.active.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.id, lhs.canonicalName, lhs.ownerCapsule) <
                std::tie(rhs.id, rhs.canonicalName, rhs.ownerCapsule);
        });
        std::sort(registry.retiredIds.begin(), registry.retiredIds.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.id, lhs.ownerCapsule) < std::tie(rhs.id, rhs.ownerCapsule);
        });
        std::sort(registry.retiredNames.begin(), registry.retiredNames.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.canonicalName, lhs.ownerCapsule) <
                std::tie(rhs.canonicalName, rhs.ownerCapsule);
        });
    }
    std::sort(data.registries.begin(), data.registries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.scope < rhs.scope;
    });
}

std::map<std::uint32_t, std::string> ActiveModelsById(const AllocationLedgerData& ledger)
{
    std::map<std::uint32_t, std::string> result;
    const auto* models = FindRegistry(ledger, {.kind = RegistryKind::Model});
    if (models == nullptr) return result;
    for (const auto& active : models->active) result.emplace(active.id, active.ownerCapsule);
    return result;
}

bool ModelIsRetired(const AllocationLedgerData& ledger, ModelTypeId model)
{
    const auto* models = FindRegistry(ledger, {.kind = RegistryKind::Model});
    if (models == nullptr) return false;
    return std::any_of(models->retiredIds.begin(), models->retiredIds.end(), [model](const auto& retired) {
        return retired.id == model.value;
    });
}

std::map<std::uint32_t, std::string> ActiveEnumsById(const AllocationLedgerData& ledger)
{
    std::map<std::uint32_t, std::string> result;
    const auto* enums = FindRegistry(ledger, {.kind = RegistryKind::Enum});
    if (enums == nullptr) return result;
    for (const auto& active : enums->active) result.emplace(active.id, active.ownerCapsule);
    return result;
}
bool EnumIsRetired(const AllocationLedgerData& ledger, EnumId enumeration)
{
    const auto* enums = FindRegistry(ledger, {.kind = RegistryKind::Enum});
    if (enums == nullptr) return false;
    return std::any_of(enums->retiredIds.begin(), enums->retiredIds.end(), [enumeration](const auto& retired) {
        return retired.id == enumeration.value;
    });
}

void AddEvolutionDiagnostic(
    AllocationEvolutionResult& result,
    const ParseLimits& limits,
    std::string code,
    std::string message,
    const SemanticProvenance* provenance = nullptr
)
{
    AddDiagnostic(
        result.diagnostics,
        limits,
        std::move(code),
        std::move(message),
        provenance == nullptr || provenance->sourcePath.empty()
            ? "<allocation-evolution>"
            : provenance->sourcePath,
        provenance == nullptr ? EmptySpan() : provenance->span
    );
}

void CompareRegistry(
    const AllocationRegistry& previous,
    const AllocationRegistry& current,
    AllocationEvolutionResult& result,
    const ParseLimits& limits
)
{
    const auto current_active_by_id = [&]() {
        std::map<std::uint32_t, const ActiveAllocation*> values;
        for (const auto& active : current.active) values.emplace(active.id, &active);
        return values;
    }();
    const auto current_active_by_name = [&]() {
        std::map<std::string, const ActiveAllocation*> values;
        for (const auto& active : current.active) values.emplace(active.canonicalName, &active);
        return values;
    }();
    const auto current_retired_ids = [&]() {
        std::map<std::uint32_t, const RetiredIdAllocation*> values;
        for (const auto& retired : current.retiredIds) values.emplace(retired.id, &retired);
        return values;
    }();
    const auto current_retired_names = [&]() {
        std::map<std::string, const RetiredNameAllocation*> values;
        for (const auto& retired : current.retiredNames) values.emplace(retired.canonicalName, &retired);
        return values;
    }();

    for (const auto& retired : previous.retiredIds)
    {
        const auto found = current_retired_ids.find(retired.id);
        if (found == current_retired_ids.end())
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_id_lost",
                "A previously retired numeric identity is missing from the current ledger."
            );
        }
        else if (found->second->ownerCapsule != retired.ownerCapsule)
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_owner_changed",
                "A retired numeric identity changed owner capsule."
            );
        }
    }
    for (const auto& retired : previous.retiredNames)
    {
        const auto found = current_retired_names.find(retired.canonicalName);
        if (found == current_retired_names.end())
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_name_lost",
                "A previously retired canonical name is missing from the current ledger."
            );
        }
        else if (found->second->ownerCapsule != retired.ownerCapsule)
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_owner_changed",
                "A retired canonical name changed owner capsule."
            );
        }
    }
    for (const auto& active : current.active)
    {
        const auto old_retired_id = std::find_if(
            previous.retiredIds.begin(),
            previous.retiredIds.end(),
            [&active](const auto& retired) { return retired.id == active.id; }
        );
        if (old_retired_id != previous.retiredIds.end())
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_id_reused",
                "A retired numeric identity was reused by an active allocation.",
                &active.provenance
            );
        }
        const auto old_retired_name = std::find_if(
            previous.retiredNames.begin(),
            previous.retiredNames.end(),
            [&active](const auto& retired) { return retired.canonicalName == active.canonicalName; }
        );
        if (old_retired_name != previous.retiredNames.end())
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_retired_name_reused",
                "A retired canonical name was reused by an active allocation.",
                &active.provenance
            );
        }
    }

    for (const auto& old_active : previous.active)
    {
        const auto same_id = current_active_by_id.find(old_active.id);
        const auto same_name = current_active_by_name.find(old_active.canonicalName);
        if (same_name != current_active_by_name.end() && same_name->second->id != old_active.id)
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_active_name_renumbered",
                "An active canonical name moved to a different numeric identity.",
                &same_name->second->provenance
            );
        }
        if (same_id == current_active_by_id.end())
        {
            if (!current_retired_ids.contains(old_active.id) ||
                !current_retired_names.contains(old_active.canonicalName))
            {
                AddEvolutionDiagnostic(
                    result,
                    limits,
                    "idl.allocation_removed_without_reservation",
                    "Removing an active allocation requires retiring both its ID and canonical name."
                );
            }
            continue;
        }
        const auto& new_active = *same_id->second;
        if (new_active.ownerCapsule != old_active.ownerCapsule)
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_owner_changed",
                "An active allocation changed owner capsule.",
                &new_active.provenance
            );
        }
        if (new_active.canonicalName != old_active.canonicalName &&
            !current_retired_names.contains(old_active.canonicalName))
        {
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_rename_without_reservation",
                "Renaming an active allocation requires retiring its previous canonical name.",
                &new_active.provenance
            );
        }
    }
}
}

namespace detail
{
bool ValidateAllocationLedgerData(
    AllocationLedgerData& data,
    std::string_view source_name,
    const ParseLimits& limits,
    std::vector<Diagnostic>& diagnostics
)
{
    if (data.formatVersion != 2)
    {
        AddDiagnostic(diagnostics, limits, "idl.allocation_version_unsupported", "Allocation ledger version is not supported.", std::string(source_name));
        return false;
    }
    if (data.registries.size() > limits.maximumModels + limits.maximumEnums + kGlobalRegistryKinds.size())
    {
        AddDiagnostic(diagnostics, limits, "idl.allocation_registry_limit_exceeded", "Allocation registry count exceeds the configured limit.", std::string(source_name));
        return false;
    }

    bool valid = true;
    std::set<RegistryScope> seen_scopes;
    std::optional<RegistryScope> previous_scope;
    std::size_t retired_count = 0;
    for (const auto& registry : data.registries)
    {
        retired_count += registry.retiredIds.size() + registry.retiredNames.size();
        if (!registry.scope.valid() || !seen_scopes.insert(registry.scope).second ||
            (previous_scope.has_value() && *previous_scope >= registry.scope))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_scope_invalid", "Allocation registry scopes must be valid, unique, and canonically ordered.", std::string(source_name));
            valid = false;
        }
        previous_scope = registry.scope;

        std::set<std::uint32_t> active_ids;
        std::set<std::string> active_names;
        std::uint32_t previous_active_id = 0;
        for (const auto& active : registry.active)
        {
            if (active.id == 0 || active.id <= previous_active_id ||
                !active_ids.insert(active.id).second ||
                !active_names.insert(active.canonicalName).second ||
                !IsCanonicalAllocationName(registry.scope, active.canonicalName) ||
                !IsCanonicalSnakeCase(active.ownerCapsule))
            {
                AddDiagnostic(diagnostics, limits, "idl.allocation_active_invalid", "Active allocations must have nonzero ordered unique IDs, unique canonical names, and canonical owners.", std::string(source_name));
                valid = false;
            }
            previous_active_id = active.id;
        }

        std::uint32_t previous_retired_id = 0;
        std::set<std::uint32_t> retired_ids;
        for (const auto& retired : registry.retiredIds)
        {
            if (retired.id == 0 || retired.id <= previous_retired_id ||
                !retired_ids.insert(retired.id).second || active_ids.contains(retired.id) ||
                !IsCanonicalSnakeCase(retired.ownerCapsule))
            {
                AddDiagnostic(diagnostics, limits, "idl.allocation_retired_id_invalid", "Retired IDs must be nonzero, ordered, unique, owner-scoped, and absent from active allocations.", std::string(source_name));
                valid = false;
            }
            previous_retired_id = retired.id;
        }

        std::string previous_retired_name;
        std::set<std::string> retired_names;
        for (const auto& retired : registry.retiredNames)
        {
            if ((!previous_retired_name.empty() && previous_retired_name >= retired.canonicalName) ||
                !retired_names.insert(retired.canonicalName).second ||
                active_names.contains(retired.canonicalName) ||
                !IsCanonicalAllocationName(registry.scope, retired.canonicalName) ||
                !IsCanonicalSnakeCase(retired.ownerCapsule))
            {
                AddDiagnostic(diagnostics, limits, "idl.allocation_retired_name_invalid", "Retired names must be ordered, unique, canonical, owner-scoped, and absent from active allocations.", std::string(source_name));
                valid = false;
            }
            previous_retired_name = retired.canonicalName;
        }
    }
    if (retired_count > limits.maximumReservations)
    {
        AddDiagnostic(diagnostics, limits, "idl.allocation_reservation_limit_exceeded", "Allocation retirement count exceeds the configured limit.", std::string(source_name));
        valid = false;
    }
    for (const auto kind : kGlobalRegistryKinds)
    {
        if (!seen_scopes.contains({.kind = kind}))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_registry_missing", "Allocation ledger is missing a required global registry.", std::string(source_name));
            valid = false;
        }
    }

    const auto active_models = ActiveModelsById(data);
    const auto active_enums = ActiveEnumsById(data);
    std::set<ModelTypeId> field_scopes;
    std::set<EnumId> enum_value_scopes;
    for (const auto& registry : data.registries)
    {
        if (registry.scope.kind != RegistryKind::Field) continue;
        const auto model = active_models.find(registry.scope.modelTypeId->value);
        if (model == active_models.end() || !field_scopes.insert(*registry.scope.modelTypeId).second)
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_field_scope_invalid", "Field registries require one unique active model scope.", std::string(source_name));
            valid = false;
            continue;
        }
        const auto owner_matches = [&model](const auto& allocation) {
            return allocation.ownerCapsule == model->second;
        };
        if (!std::all_of(registry.active.begin(), registry.active.end(), owner_matches) ||
            !std::all_of(registry.retiredIds.begin(), registry.retiredIds.end(), owner_matches) ||
            !std::all_of(registry.retiredNames.begin(), registry.retiredNames.end(), owner_matches))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_field_owner_invalid", "Field allocations must retain their model owner capsule.", std::string(source_name));
            valid = false;
        }
    }
    for (const auto& registry : data.registries)
    {
        if (registry.scope.kind != RegistryKind::EnumValue) continue;
        const auto enumeration = active_enums.find(registry.scope.enumId->value);
        if (enumeration == active_enums.end() || !enum_value_scopes.insert(*registry.scope.enumId).second)
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_enum_value_scope_invalid", "Enum value registries require one unique active enum scope.", std::string(source_name));
            valid = false;
            continue;
        }
        const auto owner_matches = [&enumeration](const auto& allocation) {
            return allocation.ownerCapsule == enumeration->second;
        };
        if (!std::all_of(registry.active.begin(), registry.active.end(), owner_matches) ||
            !std::all_of(registry.retiredIds.begin(), registry.retiredIds.end(), owner_matches) ||
            !std::all_of(registry.retiredNames.begin(), registry.retiredNames.end(), owner_matches))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_enum_value_owner_invalid", "Enum value allocations must retain their enum owner capsule.", std::string(source_name));
            valid = false;
        }
    }
    for (const auto& [model_id, owner] : active_models)
    {
        static_cast<void>(owner);
        if (!field_scopes.contains(ModelTypeId{model_id}))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_field_registry_missing", "Every active model requires a field allocation registry.", std::string(source_name));
            valid = false;
        }
    }
    for (const auto& [enum_id, owner] : active_enums)
    {
        static_cast<void>(owner);
        if (!enum_value_scopes.contains(EnumId{enum_id}))
        {
            AddDiagnostic(diagnostics, limits, "idl.allocation_enum_value_registry_missing", "Every active enum requires an enum value allocation registry.", std::string(source_name));
            valid = false;
        }
    }
    SortDiagnostics(diagnostics);
    return valid && diagnostics.empty();
}
}

bool AllocationLedgerResult::ok() const
{
    return ledger.has_value() && diagnostics.empty();
}

bool AllocationEvolutionResult::ok() const
{
    return diagnostics.empty();
}

bool AllocationSuggestionResult::ok() const
{
    return suggestion.has_value() && diagnostics.empty();
}

AllocationLedgerResult BuildAllocationLedger(const SemanticIr& ir, const ParseLimits& limits)
{
    AllocationLedgerResult result;
    if (limits.maximumDiagnostics == 0)
    {
        result.diagnostics.push_back({
            .code = "idl.invalid_parse_limits",
            .severity = DiagnosticSeverity::Error,
            .message = "The diagnostic limit must be greater than zero.",
            .sourceName = "<allocation-ledger>",
            .span = EmptySpan(),
        });
        return result;
    }

    AllocationLedgerData data;
    for (const auto kind : kGlobalRegistryKinds)
        data.registries.push_back({.scope = {.kind = kind}});

    auto& semantic_types = *FindRegistry(data, {.kind = RegistryKind::SemanticType});
    for (const auto& type : ir.semanticTypes)
    {
        semantic_types.active.push_back({
            .id = type.semanticTypeId.value,
            .canonicalName = type.qualifiedName,
            .ownerCapsule = type.ownerCapsule,
            .provenance = type.provenance,
        });
    }
    auto& wire_types = *FindRegistry(data, {.kind = RegistryKind::WireType});
    for (const auto& type : ir.wireTypes)
    {
        wire_types.active.push_back({
            .id = type.wireTypeId.value,
            .canonicalName = type.qualifiedName,
            .ownerCapsule = type.ownerCapsule,
            .provenance = type.provenance,
        });
    }
    auto& storage_codecs = *FindRegistry(data, {.kind = RegistryKind::StorageCodec});
    for (const auto& codec : ir.storageCodecs)
    {
        storage_codecs.active.push_back({
            .id = codec.storageCodecId.value,
            .canonicalName = codec.qualifiedName,
            .ownerCapsule = codec.ownerCapsule,
            .provenance = codec.provenance,
        });
    }
    auto& commands = *FindRegistry(data, {.kind = RegistryKind::Command});
    for (const auto& command : ir.commands)
    {
        commands.active.push_back({
            .id = command.commandId.value,
            .canonicalName = command.qualifiedName,
            .ownerCapsule = command.ownerCapsule,
            .provenance = command.provenance,
        });
    }
    auto& events = *FindRegistry(data, {.kind = RegistryKind::Event});
    for (const auto& event : ir.events)
    {
        events.active.push_back({
            .id = event.eventId.value,
            .canonicalName = event.qualifiedName,
            .ownerCapsule = event.ownerCapsule,
            .provenance = event.provenance,
        });
    }
    for (const auto& enumeration : ir.enums)
    {
        EnsureRegistry(data, {.kind = RegistryKind::Enum}).active.push_back({
            .id = enumeration.enumId.value,
            .canonicalName = enumeration.qualifiedName,
            .ownerCapsule = enumeration.ownerCapsule,
            .provenance = enumeration.provenance,
        });
        auto& values = EnsureRegistry(data, {
            .kind = RegistryKind::EnumValue,
            .enumId = enumeration.enumId,
        });
        for (const auto& value : enumeration.values)
        {
            values.active.push_back({
                .id = value.valueId.value,
                .canonicalName = value.name,
                .ownerCapsule = enumeration.ownerCapsule,
                .provenance = value.provenance,
            });
        }
    }
    for (const auto& model : ir.models)
    {
        EnsureRegistry(data, {.kind = RegistryKind::Model}).active.push_back({
            .id = model.typeId.value,
            .canonicalName = model.qualifiedName,
            .ownerCapsule = model.ownerCapsule,
            .provenance = model.provenance,
        });
        auto& fields = EnsureRegistry(data, {
            .kind = RegistryKind::Field,
            .modelTypeId = model.typeId,
        });
        for (const auto& field : model.fields)
        {
            fields.active.push_back({
                .id = field.fieldId.value,
                .canonicalName = field.name,
                .ownerCapsule = model.ownerCapsule,
                .provenance = field.provenance,
            });
        }
    }
    for (const auto& reserved : ir.reservedIds)
    {
        EnsureRegistry(data, reserved.scope).retiredIds.push_back({
            .id = reserved.value,
            .ownerCapsule = reserved.ownerCapsule,
            .provenance = reserved.provenance,
        });
    }
    for (const auto& reserved : ir.reservedNames)
    {
        EnsureRegistry(data, reserved.scope).retiredNames.push_back({
            .canonicalName = reserved.canonicalName,
            .ownerCapsule = reserved.ownerCapsule,
            .provenance = reserved.provenance,
        });
    }
    Canonicalize(data);
    if (!detail::ValidateAllocationLedgerData(
            data,
            "<allocation-ledger>",
            limits,
            result.diagnostics
        ))
    {
        return result;
    }
    result.ledger = detail::CreateAllocationLedger(std::move(data));
    return result;
}

AllocationEvolutionResult ValidateAllocationEvolution(
    const AllocationLedgerData& previous,
    const AllocationLedgerData& current,
    const ParseLimits& limits
)
{
    AllocationEvolutionResult result;
    if (limits.maximumDiagnostics == 0)
    {
        result.diagnostics.push_back({
            .code = "idl.invalid_parse_limits",
            .severity = DiagnosticSeverity::Error,
            .message = "The diagnostic limit must be greater than zero.",
            .sourceName = "<allocation-evolution>",
            .span = EmptySpan(),
        });
        return result;
    }
    auto previous_copy = previous;
    auto current_copy = current;
    if (!detail::ValidateAllocationLedgerData(
            previous_copy,
            "<previous-allocation-ledger>",
            limits,
            result.diagnostics
        ) ||
        !detail::ValidateAllocationLedgerData(
            current_copy,
            "<current-allocation-ledger>",
            limits,
            result.diagnostics
        ))
    {
        return result;
    }

    for (const auto& old_registry : previous.registries)
    {
        const auto* new_registry = FindRegistry(current, old_registry.scope);
        if (new_registry == nullptr)
        {
            if (old_registry.scope.kind == RegistryKind::Field &&
                ModelIsRetired(current, *old_registry.scope.modelTypeId))
            {
                continue;
            }
            if (old_registry.scope.kind == RegistryKind::EnumValue &&
                EnumIsRetired(current, *old_registry.scope.enumId))
            {
                continue;
            }
            AddEvolutionDiagnostic(
                result,
                limits,
                "idl.allocation_registry_removed",
                "An allocation registry disappeared while its parent identity remains active."
            );
            continue;
        }
        CompareRegistry(old_registry, *new_registry, result, limits);
    }
    SortDiagnostics(result.diagnostics);
    return result;
}

AllocationSuggestionResult SuggestAllocationId(
    const AllocationLedgerData& ledger,
    RegistryScope scope,
    AllocationRange range
)
{
    AllocationSuggestionResult result;
    ParseLimits limits;
    auto ledger_copy = ledger;
    if (!detail::ValidateAllocationLedgerData(
            ledger_copy,
            "<allocation-suggestion>",
            limits,
            result.diagnostics
        ))
    {
        return result;
    }
    if (!scope.valid())
    {
        AddDiagnostic(
            result.diagnostics,
            limits,
            "idl.allocation_suggestion_scope_invalid",
            "Allocation suggestion requires a valid registry scope.",
            "<allocation-suggestion>"
        );
        return result;
    }
    if (range.first == 0 || range.first > range.last)
    {
        AddDiagnostic(
            result.diagnostics,
            limits,
            "idl.allocation_suggestion_range_invalid",
            "Allocation suggestion requires a nonzero ordered range.",
            "<allocation-suggestion>"
        );
        return result;
    }
    const auto* registry = FindRegistry(ledger, scope);
    if (registry == nullptr)
    {
        AddDiagnostic(
            result.diagnostics,
            limits,
            "idl.allocation_suggestion_scope_missing",
            "Allocation suggestion scope is not present in the ledger.",
            "<allocation-suggestion>"
        );
        return result;
    }
    std::set<std::uint32_t> used;
    for (const auto& active : registry->active) used.insert(active.id);
    for (const auto& retired : registry->retiredIds) used.insert(retired.id);
    for (std::uint32_t candidate = range.first;; ++candidate)
    {
        if (!used.contains(candidate))
        {
            result.suggestion = AllocationSuggestion{.scope = scope, .value = candidate};
            return result;
        }
        if (candidate == range.last) break;
    }
    AddDiagnostic(
        result.diagnostics,
        limits,
        "idl.allocation_suggestion_exhausted",
        "No unallocated numeric identity remains in the requested range.",
        "<allocation-suggestion>"
    );
    return result;
}
}
