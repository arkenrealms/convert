#include "arken_schema_idl_v1/compatibility.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace arken::schema::idl::v1
{
std::string_view CompatibilitySubjectKindName(CompatibilitySubjectKind kind)
{
    switch (kind)
    {
    case CompatibilitySubjectKind::Source:
        return "source";
    case CompatibilitySubjectKind::WireType:
        return "wire_type";
    case CompatibilitySubjectKind::StorageCodec:
        return "storage_codec";
    case CompatibilitySubjectKind::SemanticType:
        return "semantic_type";
    case CompatibilitySubjectKind::Enum:
        return "enum";
    case CompatibilitySubjectKind::EnumValue:
        return "enum_value";
    case CompatibilitySubjectKind::Model:
        return "model";
    case CompatibilitySubjectKind::Field:
        return "field";
    case CompatibilitySubjectKind::Key:
        return "key";
    case CompatibilitySubjectKind::Index:
        return "index";
    case CompatibilitySubjectKind::Relation:
        return "relation";
    case CompatibilitySubjectKind::Join:
        return "join";
    case CompatibilitySubjectKind::OneOf:
        return "one_of";
    case CompatibilitySubjectKind::TemporalContract:
        return "temporal_contract";
    case CompatibilitySubjectKind::Reservation:
        return "reservation";
    }
    return "unknown";
}

std::string_view CompatibilityChangeKindName(CompatibilityChangeKind kind)
{
    switch (kind)
    {
    case CompatibilityChangeKind::Addition:
        return "addition";
    case CompatibilityChangeKind::Removal:
        return "removal";
    case CompatibilityChangeKind::Rename:
        return "rename";
    case CompatibilityChangeKind::ReservationAdded:
        return "reservation_added";
    case CompatibilityChangeKind::ReservationRemoved:
        return "reservation_removed";
    case CompatibilityChangeKind::TypeChanged:
        return "type_changed";
    case CompatibilityChangeKind::PresenceChanged:
        return "presence_changed";
    case CompatibilityChangeKind::RelationChanged:
        return "relation_changed";
    case CompatibilityChangeKind::KeyChanged:
        return "key_changed";
    case CompatibilityChangeKind::CodecChanged:
        return "codec_changed";
    case CompatibilityChangeKind::StorageChanged:
        return "storage_changed";
    case CompatibilityChangeKind::OwnershipChanged:
        return "ownership_changed";
    case CompatibilityChangeKind::TargetChanged:
        return "target_changed";
    case CompatibilityChangeKind::PolicyChanged:
        return "policy_changed";
    case CompatibilityChangeKind::ContractChanged:
        return "contract_changed";
    }
    return "unknown";
}

std::string_view CompatibilityDomainName(CompatibilityDomain domain)
{
    switch (domain)
    {
    case CompatibilityDomain::Source:
        return "source";
    case CompatibilityDomain::Semantic:
        return "semantic";
    case CompatibilityDomain::Wire:
        return "wire";
    case CompatibilityDomain::Storage:
        return "storage";
    case CompatibilityDomain::CppApi:
        return "cpp_api";
    case CompatibilityDomain::CSharpApi:
        return "csharp_api";
    case CompatibilityDomain::JavaScriptApi:
        return "javascript_api";
    }
    return "unknown";
}

std::string_view CompatibilityDispositionName(CompatibilityDisposition disposition)
{
    switch (disposition)
    {
    case CompatibilityDisposition::Unaffected:
        return "unaffected";
    case CompatibilityDisposition::Compatible:
        return "compatible";
    case CompatibilityDisposition::MigrationRequired:
        return "migration_required";
    case CompatibilityDisposition::Breaking:
        return "breaking";
    }
    return "unknown";
}

CompatibilityDisposition CompatibilityEffects::disposition(CompatibilityDomain domain) const
{
    switch (domain)
    {
    case CompatibilityDomain::Source:
        return source;
    case CompatibilityDomain::Semantic:
        return semantic;
    case CompatibilityDomain::Wire:
        return wire;
    case CompatibilityDomain::Storage:
        return storage;
    case CompatibilityDomain::CppApi:
        return cppApi;
    case CompatibilityDomain::CSharpApi:
        return csharpApi;
    case CompatibilityDomain::JavaScriptApi:
        return javaScriptApi;
    }
    return CompatibilityDisposition::Unaffected;
}

namespace
{
using Json = nlohmann::json;

constexpr std::string_view kReportFormat = "arken_schema_semantic_compatibility_report";
constexpr std::uint32_t kReportFormatVersion = 1;

bool EffectsBreak(const CompatibilityEffects& effects)
{
    for (const auto domain : {
             CompatibilityDomain::Source,
             CompatibilityDomain::Semantic,
             CompatibilityDomain::Wire,
             CompatibilityDomain::Storage,
             CompatibilityDomain::CppApi,
             CompatibilityDomain::CSharpApi,
             CompatibilityDomain::JavaScriptApi,
         })
    {
        if (effects.disposition(domain) == CompatibilityDisposition::Breaking) return true;
    }
    return false;
}

void SetApis(CompatibilityEffects& effects, CompatibilityDisposition disposition)
{
    effects.cppApi = disposition;
    effects.csharpApi = disposition;
    effects.javaScriptApi = disposition;
}

CompatibilityEffects AdditionEffects(CompatibilitySubjectKind subject, bool required = false)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Compatible;
    effects.semantic = CompatibilityDisposition::Compatible;
    switch (subject)
    {
    case CompatibilitySubjectKind::Source:
        break;
    case CompatibilitySubjectKind::WireType:
        effects.wire = CompatibilityDisposition::Compatible;
        break;
    case CompatibilitySubjectKind::StorageCodec:
        effects.storage = CompatibilityDisposition::Compatible;
        break;
    case CompatibilitySubjectKind::SemanticType:
    case CompatibilitySubjectKind::Enum:
    case CompatibilitySubjectKind::EnumValue:
        effects.wire = CompatibilityDisposition::Compatible;
        effects.storage = CompatibilityDisposition::Compatible;
        SetApis(effects, CompatibilityDisposition::Compatible);
        break;
    case CompatibilitySubjectKind::Model:
        effects.wire = CompatibilityDisposition::Compatible;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Compatible);
        break;
    case CompatibilitySubjectKind::Field:
        effects.source = CompatibilityDisposition::Breaking;
        effects.semantic = required ? CompatibilityDisposition::Breaking : CompatibilityDisposition::Compatible;
        effects.wire = required ? CompatibilityDisposition::Breaking : CompatibilityDisposition::Compatible;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Breaking);
        effects.dataRebuildRequired = required;
        break;
    case CompatibilitySubjectKind::Key:
    case CompatibilitySubjectKind::Index:
        effects.semantic = CompatibilityDisposition::Breaking;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Relation:
    case CompatibilitySubjectKind::Join:
        effects.semantic = CompatibilityDisposition::Breaking;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Compatible);
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::OneOf:
    case CompatibilitySubjectKind::TemporalContract:
        effects.semantic = CompatibilityDisposition::Breaking;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Breaking);
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Reservation:
        break;
    }
    return effects;
}

CompatibilityEffects RemovalEffects(CompatibilitySubjectKind subject)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    switch (subject)
    {
    case CompatibilitySubjectKind::Source:
        break;
    case CompatibilitySubjectKind::WireType:
        effects.wire = CompatibilityDisposition::Breaking;
        SetApis(effects, CompatibilityDisposition::Breaking);
        break;
    case CompatibilitySubjectKind::StorageCodec:
        effects.storage = CompatibilityDisposition::Breaking;
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::SemanticType:
    case CompatibilitySubjectKind::Enum:
    case CompatibilitySubjectKind::EnumValue:
        effects.wire = CompatibilityDisposition::Breaking;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Breaking);
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Model:
    case CompatibilitySubjectKind::Field:
        effects.wire = CompatibilityDisposition::Breaking;
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Breaking);
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Key:
    case CompatibilitySubjectKind::Index:
        effects.storage = CompatibilityDisposition::MigrationRequired;
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Relation:
    case CompatibilitySubjectKind::Join:
    case CompatibilitySubjectKind::OneOf:
    case CompatibilitySubjectKind::TemporalContract:
        effects.storage = CompatibilityDisposition::MigrationRequired;
        SetApis(effects, CompatibilityDisposition::Breaking);
        effects.dataRebuildRequired = true;
        break;
    case CompatibilitySubjectKind::Reservation:
        break;
    }
    return effects;
}

CompatibilityEffects RenameEffects(CompatibilitySubjectKind subject)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Compatible;
    effects.wire = CompatibilityDisposition::Compatible;
    SetApis(effects, CompatibilityDisposition::Breaking);
    if (subject == CompatibilitySubjectKind::Model || subject == CompatibilitySubjectKind::Field)
        effects.storage = CompatibilityDisposition::MigrationRequired;
    return effects;
}

CompatibilityEffects TypeEffects(bool wire_compatible = false)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    effects.wire = wire_compatible ? CompatibilityDisposition::Compatible
                                   : CompatibilityDisposition::Breaking;
    effects.storage = CompatibilityDisposition::MigrationRequired;
    SetApis(effects, CompatibilityDisposition::Breaking);
    effects.dataRebuildRequired = true;
    return effects;
}

CompatibilityEffects WireTypeEffects()
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    effects.wire = CompatibilityDisposition::Breaking;
    SetApis(effects, CompatibilityDisposition::Breaking);
    return effects;
}

CompatibilityEffects PresenceEffects(
    FieldCardinality previous,
    FieldCardinality current
)
{
    const bool became_required = current == FieldCardinality::RequiredOne &&
        previous != FieldCardinality::RequiredOne;
    const bool relaxed_required = previous == FieldCardinality::RequiredOne &&
        current == FieldCardinality::OptionalOne;
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = relaxed_required ? CompatibilityDisposition::Compatible
                                        : CompatibilityDisposition::Breaking;
    effects.wire = relaxed_required ? CompatibilityDisposition::Compatible
                                    : CompatibilityDisposition::Breaking;
    effects.storage = CompatibilityDisposition::MigrationRequired;
    SetApis(effects, CompatibilityDisposition::Breaking);
    effects.dataRebuildRequired = !relaxed_required;
    return effects;
}

CompatibilityEffects RelationEffects()
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    effects.storage = CompatibilityDisposition::MigrationRequired;
    SetApis(effects, CompatibilityDisposition::Breaking);
    effects.dataRebuildRequired = true;
    return effects;
}

CompatibilityEffects KeyEffects()
{
    CompatibilityEffects effects;
    effects.semantic = CompatibilityDisposition::Breaking;
    effects.storage = CompatibilityDisposition::MigrationRequired;
    effects.dataRebuildRequired = true;
    return effects;
}

CompatibilityEffects CodecEffects(bool data_rebuild_required = true)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    effects.storage = CompatibilityDisposition::Breaking;
    SetApis(effects, CompatibilityDisposition::Breaking);
    effects.dataRebuildRequired = data_rebuild_required;
    return effects;
}

CompatibilityEffects StorageEffects()
{
    CompatibilityEffects effects;
    effects.semantic = CompatibilityDisposition::Compatible;
    effects.storage = CompatibilityDisposition::MigrationRequired;
    return effects;
}

CompatibilityEffects OwnershipEffects()
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    return effects;
}

CompatibilityEffects TargetEffects()
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    SetApis(effects, CompatibilityDisposition::Breaking);
    return effects;
}

CompatibilityEffects PolicyEffects(bool storage_affected)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    SetApis(effects, CompatibilityDisposition::Breaking);
    if (storage_affected)
    {
        effects.storage = CompatibilityDisposition::MigrationRequired;
        effects.dataRebuildRequired = true;
    }
    return effects;
}

CompatibilityEffects ContractEffects(bool affects_api = true)
{
    CompatibilityEffects effects;
    effects.source = CompatibilityDisposition::Breaking;
    effects.semantic = CompatibilityDisposition::Breaking;
    if (affects_api) SetApis(effects, CompatibilityDisposition::Breaking);
    return effects;
}

CompatibilityEffects ReservationEffects(bool added)
{
    CompatibilityEffects effects;
    effects.source = added ? CompatibilityDisposition::Compatible : CompatibilityDisposition::Breaking;
    effects.semantic = added ? CompatibilityDisposition::Compatible : CompatibilityDisposition::Breaking;
    return effects;
}

std::optional<RegistryScope> SubjectScope(CompatibilitySubjectKind kind, std::optional<std::uint32_t> parent)
{
    switch (kind)
    {
    case CompatibilitySubjectKind::WireType:
        return RegistryScope{.kind = RegistryKind::WireType};
    case CompatibilitySubjectKind::StorageCodec:
        return RegistryScope{.kind = RegistryKind::StorageCodec};
    case CompatibilitySubjectKind::SemanticType:
        return RegistryScope{.kind = RegistryKind::SemanticType};
    case CompatibilitySubjectKind::Enum:
        return RegistryScope{.kind = RegistryKind::Enum};
    case CompatibilitySubjectKind::EnumValue:
        if (parent.has_value()) return RegistryScope{.kind = RegistryKind::EnumValue, .enumId = EnumId{*parent}};
        break;
    case CompatibilitySubjectKind::Model:
        return RegistryScope{.kind = RegistryKind::Model};
    case CompatibilitySubjectKind::Field:
        if (parent.has_value()) return RegistryScope{.kind = RegistryKind::Field, .modelTypeId = ModelTypeId{*parent}};
        break;
    case CompatibilitySubjectKind::Source:
    case CompatibilitySubjectKind::Key:
    case CompatibilitySubjectKind::Index:
    case CompatibilitySubjectKind::Relation:
    case CompatibilitySubjectKind::Join:
    case CompatibilitySubjectKind::OneOf:
    case CompatibilitySubjectKind::TemporalContract:
    case CompatibilitySubjectKind::Reservation:
        break;
    }
    return std::nullopt;
}

std::string SubjectDescription(const CompatibilitySubject& subject)
{
    std::string result(CompatibilitySubjectKindName(subject.kind));
    if (subject.numericId.has_value()) result += " numeric identity " + std::to_string(*subject.numericId);
    const auto& name = subject.currentName.empty() ? subject.previousName : subject.currentName;
    if (!name.empty()) result += " '" + name + "'";
    if (subject.parentNumericId.has_value())
        result += " under numeric identity " + std::to_string(*subject.parentNumericId);
    return result;
}

std::string ChangeMessage(CompatibilityChangeKind kind, const CompatibilitySubject& subject)
{
    if (kind == CompatibilityChangeKind::Rename)
        return std::string(CompatibilitySubjectKindName(subject.kind)) + " numeric identity " +
               std::to_string(subject.numericId.value_or(0)) + " was renamed from '" + subject.previousName + "' to '" +
               subject.currentName + "'.";
    return SubjectDescription(subject) + " has compatibility classification '" +
           std::string(CompatibilityChangeKindName(kind)) + "'.";
}

void AddChange(SemanticCompatibilityReport& report, CompatibilityChangeKind kind, CompatibilitySubjectKind subject_kind,
               std::optional<std::uint32_t> parent_id, std::optional<std::uint32_t> numeric_id,
               std::string previous_name, std::string current_name, CompatibilityEffects effects,
               const SemanticProvenance* previous_provenance, const SemanticProvenance* current_provenance,
               std::optional<RegistryScope> explicit_scope = std::nullopt)
{
    CompatibilitySubject subject{
        .kind = subject_kind,
        .registryScope = explicit_scope.has_value() ? explicit_scope : SubjectScope(subject_kind, parent_id),
        .parentNumericId = parent_id,
        .numericId = numeric_id,
        .previousName = std::move(previous_name),
        .currentName = std::move(current_name),
    };
    const SemanticProvenance* primary = current_provenance;
    if (primary == nullptr || primary->sourcePath.empty()) primary = previous_provenance;
    Diagnostic diagnostic{
        .code = "idl.compatibility_" + std::string(CompatibilitySubjectKindName(subject_kind)) + "_" +
                std::string(CompatibilityChangeKindName(kind)),
        .severity = EffectsBreak(effects) ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning,
        .message = ChangeMessage(kind, subject),
        .sourceName =
            primary == nullptr || primary->sourcePath.empty() ? "<semantic-compatibility>" : primary->sourcePath,
        .span = primary == nullptr ? SourceSpan{} : primary->span,
    };
    if (previous_provenance != nullptr && current_provenance != nullptr && !previous_provenance->sourcePath.empty())
    {
        diagnostic.relatedSpans.push_back({
            .message = "Previous declaration is here.",
            .sourceName = previous_provenance->sourcePath,
            .span = previous_provenance->span,
        });
    }
    report.changes.push_back({
        .kind = kind,
        .subject = std::move(subject),
        .effects = effects,
        .diagnostic = std::move(diagnostic),
    });
}

template <typename T, typename IdFunction>
std::map<std::uint32_t, const T*> IndexByNumericId(const std::vector<T>& values, IdFunction id_function)
{
    std::map<std::uint32_t, const T*> result;
    for (const auto& value : values) result.emplace(id_function(value), &value);
    return result;
}

template <typename T, typename NameFunction>
std::map<std::string, const T*> IndexByName(const std::vector<T>& values, NameFunction name_function)
{
    std::map<std::string, const T*> result;
    for (const auto& value : values) result.emplace(name_function(value), &value);
    return result;
}

std::optional<std::uint32_t> ModelId(const SemanticIr& ir, const std::optional<ModelHandle>& handle)
{
    if (!handle.has_value()) return std::nullopt;
    return ir.models.at(handle->value).typeId.value;
}

std::optional<std::uint32_t> EnumNumericId(const SemanticIr& ir, const std::optional<EnumHandle>& handle)
{
    if (!handle.has_value()) return std::nullopt;
    return ir.enums.at(handle->value).enumId.value;
}

std::uint32_t EnumNumericId(const SemanticIr& ir, EnumHandle handle)
{
    return ir.enums.at(handle.value).enumId.value;
}

std::uint32_t WireNumericId(const SemanticIr& ir, WireTypeHandle handle)
{
    return ir.wireTypes.at(handle.value).wireTypeId.value;
}

std::uint32_t CodecNumericId(const SemanticIr& ir, StorageCodecHandle handle)
{
    return ir.storageCodecs.at(handle.value).storageCodecId.value;
}

std::vector<std::uint32_t> FieldIds(const std::vector<SemanticFieldReference>& fields)
{
    std::vector<std::uint32_t> result;
    result.reserve(fields.size());
    for (const auto& field : fields) result.push_back(field.fieldId.value);
    return result;
}

bool ChecksEqual(const std::vector<SemanticCheck>& lhs, const std::vector<SemanticCheck>& rhs)
{
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (std::tie(left.name, left.kind, left.unit, left.value, left.message, left.targets, left.generated) !=
            std::tie(right.name, right.kind, right.unit, right.value, right.message, right.targets, right.generated))
            return false;
    }
    return true;
}

bool FieldValuePoliciesEqual(const SemanticField& lhs, const SemanticField& rhs)
{
    return std::tie(lhs.representation.normalization, lhs.temporalRole, lhs.defaultValue.kind, lhs.defaultValue.literal,
                    lhs.defaultValue.boundarySource) ==
           std::tie(rhs.representation.normalization, rhs.temporalRole, rhs.defaultValue.kind,
                    rhs.defaultValue.literal, rhs.defaultValue.boundarySource) &&
           lhs.allowedEnumValueIds == rhs.allowedEnumValueIds && ChecksEqual(lhs.checks, rhs.checks);
}

bool FieldBoundaryPoliciesEqual(const SemanticField& lhs, const SemanticField& rhs)
{
    return std::tie(lhs.query.operators, lhs.query.sortable, lhs.query.cursor, lhs.sourceField, lhs.importTransform,
                    lhs.sourceKind, lhs.serialization, lhs.redacted, lhs.immutable, lhs.writeOnce) ==
           std::tie(rhs.query.operators, rhs.query.sortable, rhs.query.cursor, rhs.sourceField, rhs.importTransform,
                    rhs.sourceKind, rhs.serialization, rhs.redacted, rhs.immutable, rhs.writeOnce);
}

const SemanticTypeDefinition* ResolveSemanticType(
    const SemanticIr& ir,
    const SemanticTypeReference& reference
)
{
    if (!reference.semanticType.valid() || reference.semanticType.value >= ir.semanticTypes.size())
        return nullptr;
    const auto& type = ir.semanticTypes[reference.semanticType.value];
    return type.semanticTypeId == reference.semanticTypeId ? &type : nullptr;
}

const WireTypeDefinition* ResolveWireType(const SemanticIr& ir, const SemanticFieldRepresentation& representation)
{
    if (!representation.wireType.valid() || representation.wireType.value >= ir.wireTypes.size())
        return nullptr;
    const auto& wire = ir.wireTypes[representation.wireType.value];
    return wire.wireTypeId == representation.wireTypeId ? &wire : nullptr;
}

bool WireShapesCompatible(
    const SemanticIr& previous_ir,
    const SemanticField& previous,
    const SemanticIr& current_ir,
    const SemanticField& current
)
{
    const auto* previous_type = ResolveSemanticType(previous_ir, previous.type);
    const auto* current_type = ResolveSemanticType(current_ir, current.type);
    const auto* previous_wire = ResolveWireType(previous_ir, previous.representation);
    const auto* current_wire = ResolveWireType(current_ir, current.representation);
    if (previous_type == nullptr || current_type == nullptr ||
        previous_wire == nullptr || current_wire == nullptr)
    {
        return false;
    }
    const auto compatibility = WireCompatibilityClassOf(previous_wire->kind);
    return compatibility == WireCompatibilityClassOf(current_wire->kind) &&
        previous_wire->bitWidth == current_wire->bitWidth &&
        previous_wire->fixedSize == current_wire->fixedSize &&
        previous_wire->byteOrder == current_wire->byteOrder &&
        (compatibility != WireCompatibilityClass::FixedBytes ||
            previous.representation.wireSize == current.representation.wireSize);
}

bool KeyEqual(const SemanticKey& lhs, const SemanticKey& rhs)
{
    return lhs.kind == rhs.kind && lhs.generated == rhs.generated && FieldIds(lhs.fields) == FieldIds(rhs.fields);
}

bool IndexEqual(const SemanticIndex& lhs, const SemanticIndex& rhs)
{
    return lhs.unique == rhs.unique && lhs.generated == rhs.generated && FieldIds(lhs.fields) == FieldIds(rhs.fields);
}

bool RelationEqual(const SemanticRelation& lhs, const SemanticRelation& rhs)
{
    return std::tie(lhs.targetTypeId, lhs.cardinality, lhs.onDelete, lhs.onUpdate) ==
               std::tie(rhs.targetTypeId, rhs.cardinality, rhs.onDelete, rhs.onUpdate) &&
           FieldIds(lhs.sourceFields) == FieldIds(rhs.sourceFields) &&
           FieldIds(lhs.targetFields) == FieldIds(rhs.targetFields);
}

bool JoinEqual(const SemanticJoin& lhs, const SemanticJoin& rhs)
{
    return std::tie(lhs.throughTypeId, lhs.sourceRelation, lhs.targetRelation) ==
           std::tie(rhs.throughTypeId, rhs.sourceRelation, rhs.targetRelation);
}

bool OneOfEqual(const SemanticIr& lhs_ir, const SemanticOneOf& lhs, const SemanticIr& rhs_ir, const SemanticOneOf& rhs)
{
    if (lhs.discriminator.fieldId != rhs.discriminator.fieldId ||
        EnumNumericId(lhs_ir, lhs.enumeration) != EnumNumericId(rhs_ir, rhs.enumeration) ||
        lhs.variants.size() != rhs.variants.size())
        return false;
    for (std::size_t index = 0; index < lhs.variants.size(); ++index)
    {
        const auto& left = lhs.variants[index];
        const auto& right = rhs.variants[index];
        if (left.tag != right.tag || FieldIds(left.fields) != FieldIds(right.fields) ||
            FieldIds(left.optionalFields) != FieldIds(right.optionalFields))
            return false;
    }
    return true;
}

bool TemporalEqual(const SemanticTemporalContract& lhs, const SemanticTemporalContract& rhs)
{
    return std::tie(lhs.startField.fieldId, lhs.endField.fieldId, lhs.ordering, lhs.openEnd, lhs.startInclusive,
                    lhs.endInclusive) == std::tie(rhs.startField.fieldId, rhs.endField.fieldId, rhs.ordering,
                                                  rhs.openEnd, rhs.startInclusive, rhs.endInclusive);
}

std::set<std::string> ImportPaths(const SemanticSource& source)
{
    std::set<std::string> result;
    for (const auto& import : source.imports) result.insert(import.sourcePath);
    return result;
}

Json PositionJson(const SourcePosition& position)
{
    return {{"column", position.column}, {"line", position.line}, {"offset", position.offset}};
}

Json SpanJson(const SourceSpan& span)
{
    return {{"begin", PositionJson(span.begin)}, {"end", PositionJson(span.end)}};
}

Json ScopeJson(const std::optional<RegistryScope>& scope)
{
    if (!scope.has_value()) return nullptr;
    return {
        {"enum_id", scope->enumId.has_value() ? Json(scope->enumId->value) : Json(nullptr)},
        {"kind", RegistryKindName(scope->kind)},
        {
            "model_type_id",
            scope->modelTypeId.has_value() ? Json(scope->modelTypeId->value) : Json(nullptr),
        },
    };
}

Json DiagnosticJson(const Diagnostic& diagnostic)
{
    Json related = Json::array();
    for (const auto& span : diagnostic.relatedSpans)
        related.push_back({
            {"message", span.message},
            {"source_name", span.sourceName},
            {"span", SpanJson(span.span)},
        });
    return {
        {"code", diagnostic.code},
        {"message", diagnostic.message},
        {"related_spans", std::move(related)},
        {"severity", diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning"},
        {"source_name", diagnostic.sourceName},
        {"span", SpanJson(diagnostic.span)},
    };
}

Json ChangeJson(const SemanticCompatibilityChange& change)
{
    return {
        {"diagnostic", DiagnosticJson(change.diagnostic)},
        {
            "effects",
            {
                {"cpp_api", CompatibilityDispositionName(change.effects.cppApi)},
                {"csharp_api", CompatibilityDispositionName(change.effects.csharpApi)},
                {"data_rebuild_required", change.effects.dataRebuildRequired},
                {"javascript_api", CompatibilityDispositionName(change.effects.javaScriptApi)},
                {"semantic", CompatibilityDispositionName(change.effects.semantic)},
                {"source", CompatibilityDispositionName(change.effects.source)},
                {"storage", CompatibilityDispositionName(change.effects.storage)},
                {"wire", CompatibilityDispositionName(change.effects.wire)},
            },
        },
        {"kind", CompatibilityChangeKindName(change.kind)},
        {
            "subject",
            {
                {"current_name", change.subject.currentName},
                {"kind", CompatibilitySubjectKindName(change.subject.kind)},
                {
                    "numeric_id",
                    change.subject.numericId.has_value() ? Json(*change.subject.numericId) : Json(nullptr),
                },
                {
                    "parent_numeric_id",
                    change.subject.parentNumericId.has_value() ? Json(*change.subject.parentNumericId) : Json(nullptr),
                },
                {"previous_name", change.subject.previousName},
                {"registry_scope", ScopeJson(change.subject.registryScope)},
            },
        },
    };
}

void FinalizeReport(SemanticCompatibilityReport& report)
{
    std::sort(report.changes.begin(), report.changes.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return std::tie(lhs.subject.kind, lhs.subject.registryScope, lhs.subject.parentNumericId,
                                  lhs.subject.numericId, lhs.subject.previousName, lhs.subject.currentName, lhs.kind,
                                  lhs.diagnostic.code) < std::tie(rhs.subject.kind, rhs.subject.registryScope,
                                                                  rhs.subject.parentNumericId, rhs.subject.numericId,
                                                                  rhs.subject.previousName, rhs.subject.currentName,
                                                                  rhs.kind, rhs.diagnostic.code);
              });
    Json changes = Json::array();
    for (const auto& change : report.changes) changes.push_back(ChangeJson(change));
    report.canonicalJson =
        Json{
            {"changes", std::move(changes)},
            {"current_semantic_fingerprint", report.currentSemanticFingerprint},
            {"format", kReportFormat},
            {"format_version", kReportFormatVersion},
            {"has_breaking_changes", report.hasBreakingChanges()},
            {"previous_semantic_fingerprint", report.previousSemanticFingerprint},
            {"requires_data_rebuild", report.requiresDataRebuild()},
            {"requires_storage_migration", report.requiresStorageMigration()},
        }
            .dump();
}

void CompareSources(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_sources = IndexByName(previous.sources, [](const auto& source) { return source.path; });
    const auto new_sources = IndexByName(current.sources, [](const auto& source) { return source.path; });
    for (const auto& [path, old_source] : old_sources)
    {
        const SemanticProvenance old_provenance{.sourcePath = path};
        const auto found = new_sources.find(path);
        if (found == new_sources.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Source, std::nullopt,
                      std::nullopt, path, {}, RemovalEffects(CompatibilitySubjectKind::Source), &old_provenance,
                      nullptr);
            continue;
        }
        const auto& new_source = *found->second;
        const SemanticProvenance new_provenance{.sourcePath = path};
        if (old_source->ownerCapsule != new_source.ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::Source, std::nullopt,
                      std::nullopt, path, path, OwnershipEffects(), &old_provenance, &new_provenance);
        if (std::tie(old_source->edition, old_source->schemaName, old_source->schemaVersion) !=
                std::tie(new_source.edition, new_source.schemaName, new_source.schemaVersion) ||
            ImportPaths(*old_source) != ImportPaths(new_source))
            AddChange(report, CompatibilityChangeKind::ContractChanged, CompatibilitySubjectKind::Source, std::nullopt,
                      std::nullopt, path, path, ContractEffects(false), &old_provenance, &new_provenance);
    }
    for (const auto& [path, new_source] : new_sources)
    {
        if (old_sources.contains(path)) continue;
        const SemanticProvenance provenance{.sourcePath = path};
        AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Source, std::nullopt,
                  std::nullopt, {}, path, AdditionEffects(CompatibilitySubjectKind::Source), nullptr, &provenance);
    }
}

void CompareWireTypes(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_types = IndexByNumericId(previous.wireTypes, [](const auto& type) { return type.wireTypeId.value; });
    const auto new_types = IndexByNumericId(current.wireTypes, [](const auto& type) { return type.wireTypeId.value; });
    for (const auto& [id, old_type] : old_types)
    {
        const auto found = new_types.find(id);
        if (found == new_types.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::WireType, std::nullopt, id,
                      old_type->qualifiedName, {}, RemovalEffects(CompatibilitySubjectKind::WireType),
                      &old_type->provenance, nullptr);
            continue;
        }
        const auto& new_type = *found->second;
        if (old_type->qualifiedName != new_type.qualifiedName)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::WireType, std::nullopt, id,
                      old_type->qualifiedName, new_type.qualifiedName,
                      RenameEffects(CompatibilitySubjectKind::WireType), &old_type->provenance, &new_type.provenance);
        if (old_type->ownerCapsule != new_type.ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::WireType,
                      std::nullopt, id, old_type->qualifiedName, new_type.qualifiedName, OwnershipEffects(),
                      &old_type->provenance, &new_type.provenance);
        if (std::tie(old_type->kind, old_type->bitWidth, old_type->fixedSize, old_type->byteOrder) !=
            std::tie(new_type.kind, new_type.bitWidth, new_type.fixedSize, new_type.byteOrder))
            AddChange(report, CompatibilityChangeKind::TypeChanged, CompatibilitySubjectKind::WireType, std::nullopt,
                      id, old_type->qualifiedName, new_type.qualifiedName, WireTypeEffects(), &old_type->provenance,
                      &new_type.provenance);
    }
    for (const auto& [id, new_type] : new_types)
        if (!old_types.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::WireType, std::nullopt, id,
                      {}, new_type->qualifiedName, AdditionEffects(CompatibilitySubjectKind::WireType), nullptr,
                      &new_type->provenance);
}

void CompareStorageCodecs(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_codecs =
        IndexByNumericId(previous.storageCodecs, [](const auto& codec) { return codec.storageCodecId.value; });
    const auto new_codecs =
        IndexByNumericId(current.storageCodecs, [](const auto& codec) { return codec.storageCodecId.value; });
    for (const auto& [id, old_codec] : old_codecs)
    {
        const auto found = new_codecs.find(id);
        if (found == new_codecs.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::StorageCodec, std::nullopt,
                      id, old_codec->qualifiedName, {}, RemovalEffects(CompatibilitySubjectKind::StorageCodec),
                      &old_codec->provenance, nullptr);
            continue;
        }
        const auto& new_codec = *found->second;
        if (old_codec->qualifiedName != new_codec.qualifiedName)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::StorageCodec, std::nullopt, id,
                      old_codec->qualifiedName, new_codec.qualifiedName,
                      RenameEffects(CompatibilitySubjectKind::StorageCodec), &old_codec->provenance,
                      &new_codec.provenance);
        if (old_codec->ownerCapsule != new_codec.ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::StorageCodec,
                      std::nullopt, id, old_codec->qualifiedName, new_codec.qualifiedName, OwnershipEffects(),
                      &old_codec->provenance, &new_codec.provenance);
        if (std::tie(old_codec->kind, old_codec->storageClass, old_codec->fixedSize, old_codec->exact,
                     old_codec->orderPreserving) != std::tie(new_codec.kind, new_codec.storageClass,
                                                             new_codec.fixedSize, new_codec.exact,
                                                             new_codec.orderPreserving))
        {
            const bool stored_bytes_changed = old_codec->kind != new_codec.kind ||
                                              old_codec->storageClass != new_codec.storageClass ||
                                              old_codec->fixedSize != new_codec.fixedSize;
            AddChange(report, CompatibilityChangeKind::CodecChanged, CompatibilitySubjectKind::StorageCodec,
                      std::nullopt, id, old_codec->qualifiedName, new_codec.qualifiedName,
                      CodecEffects(stored_bytes_changed), &old_codec->provenance, &new_codec.provenance);
        }
    }
    for (const auto& [id, new_codec] : new_codecs)
        if (!old_codecs.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::StorageCodec, std::nullopt,
                      id, {}, new_codec->qualifiedName, AdditionEffects(CompatibilitySubjectKind::StorageCodec),
                      nullptr, &new_codec->provenance);
}

auto SemanticTypeShape(const SemanticIr& ir, const SemanticTypeDefinition& type)
{
    return std::tuple{
        type.kind,
        type.fixedSize,
        type.precision,
        type.scale,
        type.normalization,
        type.jsonProfile,
        type.boundaryMapping,
        type.presence,
        type.defaultPolicy,
        type.narrowing,
        type.temporalRole,
        ModelId(ir, type.definingModel),
        EnumNumericId(ir, type.definingEnum),
    };
}

void CompareSemanticTypes(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_types =
        IndexByNumericId(previous.semanticTypes, [](const auto& type) { return type.semanticTypeId.value; });
    const auto new_types =
        IndexByNumericId(current.semanticTypes, [](const auto& type) { return type.semanticTypeId.value; });
    for (const auto& [id, old_type] : old_types)
    {
        const auto found = new_types.find(id);
        if (found == new_types.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::SemanticType, std::nullopt,
                      id, old_type->qualifiedName, {}, RemovalEffects(CompatibilitySubjectKind::SemanticType),
                      &old_type->provenance, nullptr);
            continue;
        }
        const auto& new_type = *found->second;
        if (old_type->qualifiedName != new_type.qualifiedName)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::SemanticType, std::nullopt, id,
                      old_type->qualifiedName, new_type.qualifiedName,
                      RenameEffects(CompatibilitySubjectKind::SemanticType), &old_type->provenance,
                      &new_type.provenance);
        if (old_type->ownerCapsule != new_type.ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::SemanticType,
                      std::nullopt, id, old_type->qualifiedName, new_type.qualifiedName, OwnershipEffects(),
                      &old_type->provenance, &new_type.provenance);
        if (WireNumericId(previous, old_type->wireType) != WireNumericId(current, new_type.wireType) ||
            SemanticTypeShape(previous, *old_type) != SemanticTypeShape(current, new_type))
            AddChange(report, CompatibilityChangeKind::TypeChanged, CompatibilitySubjectKind::SemanticType,
                      std::nullopt, id, old_type->qualifiedName, new_type.qualifiedName, TypeEffects(),
                      &old_type->provenance, &new_type.provenance);
        if (CodecNumericId(previous, old_type->storageCodec) != CodecNumericId(current, new_type.storageCodec))
            AddChange(report, CompatibilityChangeKind::CodecChanged, CompatibilitySubjectKind::SemanticType,
                      std::nullopt, id, old_type->qualifiedName, new_type.qualifiedName, CodecEffects(),
                      &old_type->provenance, &new_type.provenance);
        if (old_type->targets != new_type.targets)
            AddChange(report, CompatibilityChangeKind::TargetChanged, CompatibilitySubjectKind::SemanticType,
                      std::nullopt, id, old_type->qualifiedName, new_type.qualifiedName, TargetEffects(),
                      &old_type->provenance, &new_type.provenance);
    }
    for (const auto& [id, new_type] : new_types)
        if (!old_types.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::SemanticType, std::nullopt,
                      id, {}, new_type->qualifiedName, AdditionEffects(CompatibilitySubjectKind::SemanticType), nullptr,
                      &new_type->provenance);
}

void CompareEnumValues(const SemanticEnum& previous, const SemanticEnum& current, SemanticCompatibilityReport& report)
{
    const auto old_values = IndexByNumericId(previous.values, [](const auto& value) { return value.valueId.value; });
    const auto new_values = IndexByNumericId(current.values, [](const auto& value) { return value.valueId.value; });
    for (const auto& [id, old_value] : old_values)
    {
        const auto found = new_values.find(id);
        if (found == new_values.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::EnumValue,
                      previous.enumId.value, id, old_value->name, {},
                      RemovalEffects(CompatibilitySubjectKind::EnumValue), &old_value->provenance, nullptr);
            continue;
        }
        const auto& new_value = *found->second;
        if (old_value->name != new_value.name)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::EnumValue,
                      previous.enumId.value, id, old_value->name, new_value.name,
                      RenameEffects(CompatibilitySubjectKind::EnumValue), &old_value->provenance,
                      &new_value.provenance);
    }
    for (const auto& [id, new_value] : new_values)
        if (!old_values.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::EnumValue,
                      current.enumId.value, id, {}, new_value->name,
                      AdditionEffects(CompatibilitySubjectKind::EnumValue), nullptr, &new_value->provenance);
}

void CompareEnums(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_enums =
        IndexByNumericId(previous.enums, [](const auto& enumeration) { return enumeration.enumId.value; });
    const auto new_enums =
        IndexByNumericId(current.enums, [](const auto& enumeration) { return enumeration.enumId.value; });
    for (const auto& [id, old_enum] : old_enums)
    {
        const auto found = new_enums.find(id);
        if (found == new_enums.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Enum, std::nullopt, id,
                      old_enum->qualifiedName, {}, RemovalEffects(CompatibilitySubjectKind::Enum),
                      &old_enum->provenance, nullptr);
            continue;
        }
        const auto& new_enum = *found->second;
        if (old_enum->qualifiedName != new_enum.qualifiedName)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::Enum, std::nullopt, id,
                      old_enum->qualifiedName, new_enum.qualifiedName, RenameEffects(CompatibilitySubjectKind::Enum),
                      &old_enum->provenance, &new_enum.provenance);
        if (old_enum->ownerCapsule != new_enum.ownerCapsule ||
            old_enum->mutationAuthority != new_enum.mutationAuthority)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::Enum, std::nullopt,
                      id, old_enum->qualifiedName, new_enum.qualifiedName, OwnershipEffects(), &old_enum->provenance,
                      &new_enum.provenance);
        if (old_enum->semanticTypeId != new_enum.semanticTypeId)
            AddChange(report, CompatibilityChangeKind::TypeChanged, CompatibilitySubjectKind::Enum, std::nullopt, id,
                      old_enum->qualifiedName, new_enum.qualifiedName, TypeEffects(), &old_enum->provenance,
                      &new_enum.provenance);
        CompareEnumValues(*old_enum, new_enum, report);
    }
    for (const auto& [id, new_enum] : new_enums)
        if (!old_enums.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Enum, std::nullopt, id, {},
                      new_enum->qualifiedName, AdditionEffects(CompatibilitySubjectKind::Enum), nullptr,
                      &new_enum->provenance);
}

void CompareFields(
    const SemanticIr& previous_ir,
    const SemanticModel& previous,
    const SemanticIr& current_ir,
    const SemanticModel& current,
    SemanticCompatibilityReport& report
)
{
    const auto old_fields = IndexByNumericId(previous.fields, [](const auto& field) { return field.fieldId.value; });
    const auto new_fields = IndexByNumericId(current.fields, [](const auto& field) { return field.fieldId.value; });
    for (const auto& [id, old_field] : old_fields)
    {
        const auto found = new_fields.find(id);
        if (found == new_fields.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Field, previous.typeId.value,
                      id, old_field->name, {}, RemovalEffects(CompatibilitySubjectKind::Field), &old_field->provenance,
                      nullptr);
            continue;
        }
        const auto& new_field = *found->second;
        if (old_field->name != new_field.name)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::Field, previous.typeId.value,
                      id, old_field->name, new_field.name, RenameEffects(CompatibilitySubjectKind::Field),
                      &old_field->provenance, &new_field.provenance);
        if (old_field->type.semanticTypeId != new_field.type.semanticTypeId ||
            old_field->representation.fieldKind != new_field.representation.fieldKind ||
            old_field->representation.wireTypeId != new_field.representation.wireTypeId ||
            old_field->representation.wireSize != new_field.representation.wireSize ||
            old_field->representation.precision != new_field.representation.precision ||
            old_field->representation.scale != new_field.representation.scale)
            AddChange(report, CompatibilityChangeKind::TypeChanged, CompatibilitySubjectKind::Field,
                      previous.typeId.value, id, old_field->name, new_field.name,
                      TypeEffects(WireShapesCompatible(previous_ir, *old_field, current_ir, new_field)),
                      &old_field->provenance,
                      &new_field.provenance);
        if (old_field->cardinality != new_field.cardinality ||
            old_field->maximumItems != new_field.maximumItems)
            AddChange(report, CompatibilityChangeKind::PresenceChanged, CompatibilitySubjectKind::Field,
                      previous.typeId.value, id, old_field->name, new_field.name,
                      PresenceEffects(old_field->cardinality, new_field.cardinality),
                      &old_field->provenance, &new_field.provenance);
        if (old_field->storageName != new_field.storageName ||
            old_field->representation.storageCodecId != new_field.representation.storageCodecId ||
            old_field->representation.storageSize != new_field.representation.storageSize)
            AddChange(report, CompatibilityChangeKind::StorageChanged, CompatibilitySubjectKind::Field,
                      previous.typeId.value, id, old_field->storageName, new_field.storageName, StorageEffects(),
                      &old_field->provenance, &new_field.provenance);
        if (old_field->primaryKey != new_field.primaryKey)
            AddChange(report, CompatibilityChangeKind::KeyChanged, CompatibilitySubjectKind::Field,
                      previous.typeId.value, id, old_field->name, new_field.name, KeyEffects(), &old_field->provenance,
                      &new_field.provenance);
        const bool value_policy_changed = !FieldValuePoliciesEqual(*old_field, new_field);
        if (value_policy_changed || !FieldBoundaryPoliciesEqual(*old_field, new_field))
            AddChange(report, CompatibilityChangeKind::PolicyChanged, CompatibilitySubjectKind::Field,
                      previous.typeId.value, id, old_field->name, new_field.name, PolicyEffects(value_policy_changed),
                      &old_field->provenance, &new_field.provenance);
    }
    for (const auto& [id, new_field] : new_fields)
        if (!old_fields.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Field, current.typeId.value,
                      id, {}, new_field->name, AdditionEffects(CompatibilitySubjectKind::Field, new_field->required),
                      nullptr, &new_field->provenance);
}

void CompareKeys(const SemanticModel& previous, const SemanticModel& current, SemanticCompatibilityReport& report)
{
    const auto old_keys = IndexByName(previous.keys, [](const auto& key) { return key.name; });
    const auto new_keys = IndexByName(current.keys, [](const auto& key) { return key.name; });
    for (const auto& [name, old_key] : old_keys)
    {
        const auto found = new_keys.find(name);
        if (found == new_keys.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Key, previous.typeId.value,
                      std::nullopt, name, {}, RemovalEffects(CompatibilitySubjectKind::Key), &old_key->provenance,
                      nullptr);
        else if (!KeyEqual(*old_key, *found->second))
            AddChange(report, CompatibilityChangeKind::KeyChanged, CompatibilitySubjectKind::Key, previous.typeId.value,
                      std::nullopt, name, name, KeyEffects(), &old_key->provenance, &found->second->provenance);
    }
    for (const auto& [name, new_key] : new_keys)
        if (!old_keys.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Key, current.typeId.value,
                      std::nullopt, {}, name, AdditionEffects(CompatibilitySubjectKind::Key), nullptr,
                      &new_key->provenance);

    const auto old_indexes = IndexByName(previous.indexes, [](const auto& index) { return index.name; });
    const auto new_indexes = IndexByName(current.indexes, [](const auto& index) { return index.name; });
    for (const auto& [name, old_index] : old_indexes)
    {
        const auto found = new_indexes.find(name);
        if (found == new_indexes.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Index, previous.typeId.value,
                      std::nullopt, name, {}, RemovalEffects(CompatibilitySubjectKind::Index), &old_index->provenance,
                      nullptr);
        else if (!IndexEqual(*old_index, *found->second))
            AddChange(report, CompatibilityChangeKind::KeyChanged, CompatibilitySubjectKind::Index,
                      previous.typeId.value, std::nullopt, name, name, KeyEffects(), &old_index->provenance,
                      &found->second->provenance);
    }
    for (const auto& [name, new_index] : new_indexes)
        if (!old_indexes.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Index, current.typeId.value,
                      std::nullopt, {}, name, AdditionEffects(CompatibilitySubjectKind::Index), nullptr,
                      &new_index->provenance);
}

void CompareRelations(const SemanticIr& previous_ir, const SemanticModel& previous, const SemanticIr& current_ir,
                      const SemanticModel& current, SemanticCompatibilityReport& report)
{
    const auto old_relations = IndexByName(previous.relations, [](const auto& relation) { return relation.name; });
    const auto new_relations = IndexByName(current.relations, [](const auto& relation) { return relation.name; });
    for (const auto& [name, old_relation] : old_relations)
    {
        const auto found = new_relations.find(name);
        if (found == new_relations.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Relation,
                      previous.typeId.value, std::nullopt, name, {}, RemovalEffects(CompatibilitySubjectKind::Relation),
                      &old_relation->provenance, nullptr);
        else if (!RelationEqual(*old_relation, *found->second))
            AddChange(report, CompatibilityChangeKind::RelationChanged, CompatibilitySubjectKind::Relation,
                      previous.typeId.value, std::nullopt, name, name, RelationEffects(), &old_relation->provenance,
                      &found->second->provenance);
    }
    for (const auto& [name, new_relation] : new_relations)
        if (!old_relations.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Relation,
                      current.typeId.value, std::nullopt, {}, name, AdditionEffects(CompatibilitySubjectKind::Relation),
                      nullptr, &new_relation->provenance);

    const auto old_joins = IndexByName(previous.joins, [](const auto& join) { return join.name; });
    const auto new_joins = IndexByName(current.joins, [](const auto& join) { return join.name; });
    for (const auto& [name, old_join] : old_joins)
    {
        const auto found = new_joins.find(name);
        if (found == new_joins.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Join, previous.typeId.value,
                      std::nullopt, name, {}, RemovalEffects(CompatibilitySubjectKind::Join), &old_join->provenance,
                      nullptr);
        else if (!JoinEqual(*old_join, *found->second))
            AddChange(report, CompatibilityChangeKind::RelationChanged, CompatibilitySubjectKind::Join,
                      previous.typeId.value, std::nullopt, name, name, RelationEffects(), &old_join->provenance,
                      &found->second->provenance);
    }
    for (const auto& [name, new_join] : new_joins)
        if (!old_joins.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Join, current.typeId.value,
                      std::nullopt, {}, name, AdditionEffects(CompatibilitySubjectKind::Join), nullptr,
                      &new_join->provenance);

    const auto old_unions = IndexByName(previous.oneOfs, [](const auto& one_of) { return one_of.name; });
    const auto new_unions = IndexByName(current.oneOfs, [](const auto& one_of) { return one_of.name; });
    for (const auto& [name, old_union] : old_unions)
    {
        const auto found = new_unions.find(name);
        if (found == new_unions.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::OneOf, previous.typeId.value,
                      std::nullopt, name, {}, RemovalEffects(CompatibilitySubjectKind::OneOf), &old_union->provenance,
                      nullptr);
        else if (!OneOfEqual(previous_ir, *old_union, current_ir, *found->second))
            AddChange(report, CompatibilityChangeKind::ContractChanged, CompatibilitySubjectKind::OneOf,
                      previous.typeId.value, std::nullopt, name, name, ContractEffects(), &old_union->provenance,
                      &found->second->provenance);
    }
    for (const auto& [name, new_union] : new_unions)
        if (!old_unions.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::OneOf, current.typeId.value,
                      std::nullopt, {}, name, AdditionEffects(CompatibilitySubjectKind::OneOf), nullptr,
                      &new_union->provenance);

    const auto old_temporal =
        IndexByName(previous.temporalContracts, [](const auto& contract) { return contract.name; });
    const auto new_temporal =
        IndexByName(current.temporalContracts, [](const auto& contract) { return contract.name; });
    for (const auto& [name, old_contract] : old_temporal)
    {
        const auto found = new_temporal.find(name);
        if (found == new_temporal.end())
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::TemporalContract,
                      previous.typeId.value, std::nullopt, name, {},
                      RemovalEffects(CompatibilitySubjectKind::TemporalContract), &old_contract->provenance, nullptr);
        else if (!TemporalEqual(*old_contract, *found->second))
            AddChange(report, CompatibilityChangeKind::ContractChanged, CompatibilitySubjectKind::TemporalContract,
                      previous.typeId.value, std::nullopt, name, name, ContractEffects(), &old_contract->provenance,
                      &found->second->provenance);
    }
    for (const auto& [name, new_contract] : new_temporal)
        if (!old_temporal.contains(name))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::TemporalContract,
                      current.typeId.value, std::nullopt, {}, name,
                      AdditionEffects(CompatibilitySubjectKind::TemporalContract), nullptr, &new_contract->provenance);
}

bool ModelContractEqual(const SemanticModel& lhs, const SemanticModel& rhs)
{
    return std::tie(lhs.schemaName, lhs.schemaVersion, lhs.domain, lhs.description, lhs.kind, lhs.retention, lhs.role,
                    lhs.sourceAliases, lhs.retiredNames, lhs.nameReview, lhs.nameReviewRationale) ==
           std::tie(rhs.schemaName, rhs.schemaVersion, rhs.domain, rhs.description, rhs.kind, rhs.retention, rhs.role,
                    rhs.sourceAliases, rhs.retiredNames, rhs.nameReview, rhs.nameReviewRationale);
}

void CompareModels(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    const auto old_models = IndexByNumericId(previous.models, [](const auto& model) { return model.typeId.value; });
    const auto new_models = IndexByNumericId(current.models, [](const auto& model) { return model.typeId.value; });
    for (const auto& [id, old_model] : old_models)
    {
        const auto found = new_models.find(id);
        if (found == new_models.end())
        {
            AddChange(report, CompatibilityChangeKind::Removal, CompatibilitySubjectKind::Model, std::nullopt, id,
                      old_model->qualifiedName, {}, RemovalEffects(CompatibilitySubjectKind::Model),
                      &old_model->provenance, nullptr);
            continue;
        }
        const auto& new_model = *found->second;
        if (old_model->qualifiedName != new_model.qualifiedName)
            AddChange(report, CompatibilityChangeKind::Rename, CompatibilitySubjectKind::Model, std::nullopt, id,
                      old_model->qualifiedName, new_model.qualifiedName, RenameEffects(CompatibilitySubjectKind::Model),
                      &old_model->provenance, &new_model.provenance);
        if (old_model->ownerCapsule != new_model.ownerCapsule ||
            old_model->mutationAuthority != new_model.mutationAuthority)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::Model, std::nullopt,
                      id, old_model->qualifiedName, new_model.qualifiedName, OwnershipEffects(), &old_model->provenance,
                      &new_model.provenance);
        if (old_model->storageName != new_model.storageName)
            AddChange(report, CompatibilityChangeKind::StorageChanged, CompatibilitySubjectKind::Model, std::nullopt,
                      id, old_model->storageName, new_model.storageName, StorageEffects(), &old_model->provenance,
                      &new_model.provenance);
        if (old_model->provenance.sourcePath != new_model.provenance.sourcePath ||
            !ModelContractEqual(*old_model, new_model))
            AddChange(report, CompatibilityChangeKind::ContractChanged, CompatibilitySubjectKind::Model, std::nullopt,
                      id, old_model->qualifiedName, new_model.qualifiedName, ContractEffects(), &old_model->provenance,
                      &new_model.provenance);
        CompareFields(previous, *old_model, current, new_model, report);
        CompareKeys(*old_model, new_model, report);
        CompareRelations(previous, *old_model, current, new_model, report);
    }
    for (const auto& [id, new_model] : new_models)
        if (!old_models.contains(id))
            AddChange(report, CompatibilityChangeKind::Addition, CompatibilitySubjectKind::Model, std::nullopt, id, {},
                      new_model->qualifiedName, AdditionEffects(CompatibilitySubjectKind::Model), nullptr,
                      &new_model->provenance);
}

void CompareReservations(const SemanticIr& previous, const SemanticIr& current, SemanticCompatibilityReport& report)
{
    using IdKey = std::pair<RegistryScope, std::uint32_t>;
    std::map<IdKey, const SemanticReservedId*> old_ids;
    std::map<IdKey, const SemanticReservedId*> new_ids;
    for (const auto& value : previous.reservedIds) old_ids.emplace(IdKey{value.scope, value.value}, &value);
    for (const auto& value : current.reservedIds) new_ids.emplace(IdKey{value.scope, value.value}, &value);
    for (const auto& [key, old_value] : old_ids)
    {
        const auto found = new_ids.find(key);
        const auto parent = key.first.modelTypeId.has_value()
                                ? std::optional<std::uint32_t>{key.first.modelTypeId->value}
                            : key.first.enumId.has_value() ? std::optional<std::uint32_t>{key.first.enumId->value}
                                                           : std::nullopt;
        if (found == new_ids.end())
            AddChange(report, CompatibilityChangeKind::ReservationRemoved, CompatibilitySubjectKind::Reservation,
                      parent, key.second, std::string(RegistryKindName(key.first.kind)), {}, ReservationEffects(false),
                      &old_value->provenance, nullptr, key.first);
        else if (old_value->ownerCapsule != found->second->ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::Reservation, parent,
                      key.second, std::string(RegistryKindName(key.first.kind)),
                      std::string(RegistryKindName(key.first.kind)), OwnershipEffects(), &old_value->provenance,
                      &found->second->provenance, key.first);
    }
    for (const auto& [key, new_value] : new_ids)
    {
        if (old_ids.contains(key)) continue;
        const auto parent = key.first.modelTypeId.has_value()
                                ? std::optional<std::uint32_t>{key.first.modelTypeId->value}
                            : key.first.enumId.has_value() ? std::optional<std::uint32_t>{key.first.enumId->value}
                                                           : std::nullopt;
        AddChange(report, CompatibilityChangeKind::ReservationAdded, CompatibilitySubjectKind::Reservation, parent,
                  key.second, {}, std::string(RegistryKindName(key.first.kind)), ReservationEffects(true), nullptr,
                  &new_value->provenance, key.first);
    }

    using NameKey = std::pair<RegistryScope, std::string>;
    std::map<NameKey, const SemanticReservedName*> old_names;
    std::map<NameKey, const SemanticReservedName*> new_names;
    for (const auto& value : previous.reservedNames)
        old_names.emplace(NameKey{value.scope, value.canonicalName}, &value);
    for (const auto& value : current.reservedNames)
        new_names.emplace(NameKey{value.scope, value.canonicalName}, &value);
    for (const auto& [key, old_value] : old_names)
    {
        const auto found = new_names.find(key);
        const auto parent = key.first.modelTypeId.has_value()
                                ? std::optional<std::uint32_t>{key.first.modelTypeId->value}
                            : key.first.enumId.has_value() ? std::optional<std::uint32_t>{key.first.enumId->value}
                                                           : std::nullopt;
        if (found == new_names.end())
            AddChange(report, CompatibilityChangeKind::ReservationRemoved, CompatibilitySubjectKind::Reservation,
                      parent, std::nullopt, key.second, {}, ReservationEffects(false), &old_value->provenance, nullptr,
                      key.first);
        else if (old_value->ownerCapsule != found->second->ownerCapsule)
            AddChange(report, CompatibilityChangeKind::OwnershipChanged, CompatibilitySubjectKind::Reservation, parent,
                      std::nullopt, key.second, key.second, OwnershipEffects(), &old_value->provenance,
                      &found->second->provenance, key.first);
    }
    for (const auto& [key, new_value] : new_names)
    {
        if (old_names.contains(key)) continue;
        const auto parent = key.first.modelTypeId.has_value()
                                ? std::optional<std::uint32_t>{key.first.modelTypeId->value}
                            : key.first.enumId.has_value() ? std::optional<std::uint32_t>{key.first.enumId->value}
                                                           : std::nullopt;
        AddChange(report, CompatibilityChangeKind::ReservationAdded, CompatibilitySubjectKind::Reservation, parent,
                  std::nullopt, {}, key.second, ReservationEffects(true), nullptr, &new_value->provenance, key.first);
    }
}
}

bool SemanticCompatibilityReport::hasBreakingChanges() const
{
    return std::any_of(changes.begin(), changes.end(), [](const auto& change) { return EffectsBreak(change.effects); });
}

bool SemanticCompatibilityReport::requiresStorageMigration() const
{
    return std::any_of(changes.begin(), changes.end(),
                       [](const auto& change)
                       {
                           return change.effects.storage == CompatibilityDisposition::MigrationRequired ||
                                  change.effects.storage == CompatibilityDisposition::Breaking;
                       });
}

bool SemanticCompatibilityReport::requiresDataRebuild() const
{
    return std::any_of(changes.begin(), changes.end(),
                       [](const auto& change) { return change.effects.dataRebuildRequired; });
}

SemanticCompatibilityReport CompareSemanticManifests(const SemanticManifest& previous, const SemanticManifest& current)
{
    SemanticCompatibilityReport report{
        .previousSemanticFingerprint = previous.semanticFingerprint,
        .currentSemanticFingerprint = current.semanticFingerprint,
    };
    CompareSources(previous.ir, current.ir, report);
    CompareWireTypes(previous.ir, current.ir, report);
    CompareStorageCodecs(previous.ir, current.ir, report);
    CompareSemanticTypes(previous.ir, current.ir, report);
    CompareEnums(previous.ir, current.ir, report);
    CompareModels(previous.ir, current.ir, report);
    CompareReservations(previous.ir, current.ir, report);
    if (previous.semanticFingerprint != current.semanticFingerprint && report.changes.empty())
    {
        const auto previous_path = previous.ir.sources.at(previous.ir.entrySource.value).path;
        const auto current_path = current.ir.sources.at(current.ir.entrySource.value).path;
        const SemanticProvenance old_provenance{.sourcePath = previous_path};
        const SemanticProvenance new_provenance{.sourcePath = current_path};
        AddChange(report, CompatibilityChangeKind::ContractChanged, CompatibilitySubjectKind::Source, std::nullopt,
                  std::nullopt, previous_path, current_path, ContractEffects(), &old_provenance, &new_provenance);
    }
    FinalizeReport(report);
    return report;
}
}
