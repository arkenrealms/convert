#include "arken_schema_idl_v1/semantic.hpp"
#include "arken_schema_idl_v1/source_format.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "semantic_internal.hpp"
#include "sha256.hpp"

namespace arken::schema::idl::v1
{
namespace
{
using Json = nlohmann::json;

constexpr std::string_view kManifestFormat = "arken_schema_semantic_manifest";
constexpr std::uint32_t kManifestFormatVersion = 16;

Json CatalogMemberIdentityJson(const std::optional<SemanticCatalogMemberIdentity>& identity)
{
    if (!identity.has_value()) return nullptr;
    return {
        {"declaration_order", identity->declarationOrder},
        {"name", identity->name},
    };
}

Json CatalogFieldIdentityJson(const std::optional<SemanticCatalogFieldIdentity>& identity)
{
    if (!identity.has_value()) return nullptr;
    return {
        {"declaration_order", identity->declarationOrder},
        {"field_name", identity->fieldName},
        {"registry_key", identity->registryKey},
    };
}

Json CatalogModelIdentityJson(const std::optional<SemanticCatalogModelIdentity>& identity)
{
    if (!identity.has_value()) return nullptr;
    return {
        {"declaration_order", identity->declarationOrder},
        {"domain", identity->domain},
        {"fragment_id", identity->fragmentId},
        {"logical_name", identity->logicalName},
        {"model_id", identity->modelId},
        {"qualified_name", identity->qualifiedName},
        {"registry_key", identity->registryKey},
    };
}

Json CatalogJson(const std::optional<SemanticCatalog>& catalog)
{
    if (!catalog.has_value()) return nullptr;
    Json fragments = Json::array();
    for (const auto& fragment : catalog->fragments)
        fragments.push_back({
            {"declaration_order", fragment.declarationOrder},
            {"fragment_id", fragment.fragmentId},
            {"owner_capsule", fragment.ownerCapsule},
            {"version", fragment.version},
        });
    return {
        {"catalog_id", catalog->catalogId},
        {"fragments", std::move(fragments)},
        {"version", catalog->version},
    };
}

Json TypeJson(const SemanticTypeReference& type)
{
    return {
        {"canonical_name", type.canonicalName},
        {
            "defining_model_handle",
            type.definingModel.has_value() ? Json(type.definingModel->value) : Json(nullptr),
        },
        {
            "defining_enum_handle",
            type.definingEnum.has_value() ? Json(type.definingEnum->value) : Json(nullptr),
        },
        {"kind", SemanticTypeKindName(type.kind)},
        {"semantic_type_handle", type.semanticType.value},
        {"semantic_type_id", type.semanticTypeId.value},
    };
}

Json FieldRepresentationJson(const SemanticFieldRepresentation& representation)
{
    return {
        {"field_kind", FieldKindName(representation.fieldKind)},
        {"normalization", NormalizationKindName(representation.normalization)},
        {"precision", representation.precision},
        {"scale", representation.scale},
        {"storage_codec_handle", representation.storageCodec.value},
        {"storage_codec_id", representation.storageCodecId.value},
        {"storage_size", representation.storageSize},
        {"wire_size", representation.wireSize},
        {"wire_type_handle", representation.wireType.value},
        {"wire_type_id", representation.wireTypeId.value},
    };
}

Json WireTypeJson(const WireTypeDefinition& type)
{
    return {
        {"bit_width", type.bitWidth},
        {"byte_order", WireByteOrderName(type.byteOrder)},
        {"fixed_size", type.fixedSize},
        {"handle", type.handle.value},
        {"kind", WirePrimitiveKindName(type.kind)},
        {"name", type.name},
        {"owner_capsule", type.ownerCapsule},
        {"qualified_name", type.qualifiedName},
        {"source_handle", type.source.value},
        {"wire_type_id", type.wireTypeId.value},
    };
}

Json StorageCodecJson(const StorageCodecDefinition& codec)
{
    return {
        {"exact", codec.exact},
        {"fixed_size", codec.fixedSize},
        {"handle", codec.handle.value},
        {"kind", StorageCodecKindName(codec.kind)},
        {"name", codec.name},
        {"order_preserving", codec.orderPreserving},
        {"owner_capsule", codec.ownerCapsule},
        {"qualified_name", codec.qualifiedName},
        {"source_handle", codec.source.value},
        {"storage_class", SqliteStorageClassName(codec.storageClass)},
        {"storage_codec_id", codec.storageCodecId.value},
    };
}

Json SemanticTypeJson(const SemanticTypeDefinition& type)
{
    Json targets = Json::array();
    for (const auto target : type.targets) targets.push_back(TargetProfileName(target));
    return {
        {"boundary_mapping", BoundaryMappingPolicyName(type.boundaryMapping)},
        {"default", DefaultPolicyName(type.defaultPolicy)},
        {
            "defining_model_handle",
            type.definingModel.has_value() ? Json(type.definingModel->value) : Json(nullptr),
        },
        {
            "defining_enum_handle",
            type.definingEnum.has_value() ? Json(type.definingEnum->value) : Json(nullptr),
        },
        {"fixed_size", type.fixedSize},
        {"handle", type.handle.value},
        {"kind", SemanticTypeKindName(type.kind)},
        {"json_profile", JsonProfileName(type.jsonProfile)},
        {"name", type.name},
        {"narrowing", NarrowingPolicyName(type.narrowing)},
        {"normalization", NormalizationKindName(type.normalization)},
        {"owner_capsule", type.ownerCapsule},
        {"precision", type.precision},
        {"presence", PresencePolicyName(type.presence)},
        {"qualified_name", type.qualifiedName},
        {"registry_key", type.registryKey},
        {"scale", type.scale},
        {"semantic_name", type.semanticName},
        {"semantic_type_id", type.semanticTypeId.value},
        {"source_handle", type.source.value},
        {"storage_codec_handle", type.storageCodec.value},
        {"targets", std::move(targets)},
        {"temporal_role", TemporalRoleName(type.temporalRole)},
        {"wire_type_handle", type.wireType.value},
    };
}

Json FieldDefaultJson(const SemanticFieldDefault& value)
{
    return {
        {"boundary_source", BoundaryDefaultSourceName(value.boundarySource)},
        {"catalog_value", value.catalogValue},
        {"kind", FieldDefaultKindName(value.kind)},
        {"literal", value.literal},
    };
}

Json CheckJson(const SemanticCheck& check)
{
    Json targets = Json::array();
    for (const auto target : check.targets) targets.push_back(TargetProfileName(target));
    return {
        {"catalog_identity", CatalogMemberIdentityJson(check.catalogIdentity)},
        {"generated", check.generated},
        {"kind", CheckKindName(check.kind)},
        {"message", check.message},
        {"name", check.name},
        {"targets", std::move(targets)},
        {"unit", CheckUnitName(check.unit)},
        {"value", check.value},
    };
}

Json QueryPolicyJson(const SemanticQueryPolicy& query)
{
    Json operators = Json::array();
    for (const auto operation : query.operators) operators.push_back(QueryOperatorName(operation));
    return {
        {"cursor", query.cursor},
        {"operators", std::move(operators)},
        {"sortable", query.sortable},
    };
}

Json FieldJson(const SemanticField& field)
{
    Json checks = Json::array();
    for (const auto& check : field.checks) checks.push_back(CheckJson(check));
    Json allowed_enum_value_ids = Json::array();
    for (const auto value : field.allowedEnumValueIds)
        allowed_enum_value_ids.push_back(value.value);
    return {
        {"allowed_enum_value_ids", std::move(allowed_enum_value_ids)},
        {"cardinality", FieldCardinalityName(field.cardinality)},
        {"catalog_identity", CatalogFieldIdentityJson(field.catalogIdentity)},
        {"checks", std::move(checks)},
        {"cpp_name", field.cppName.has_value() ? Json(*field.cppName) : Json(nullptr)},
        {"csharp_name", field.csharpName.has_value() ? Json(*field.csharpName) : Json(nullptr)},
        {"default", FieldDefaultJson(field.defaultValue)},
        {"field_id", field.fieldId.value},
        {"generated_primary_id", field.generatedPrimaryId},
        {"handle", field.handle.value},
        {"immutable", field.immutable},
        {"import_transform", field.importTransform},
        {"maximum_items", field.maximumItems.has_value() ? Json(*field.maximumItems) : Json(nullptr)},
        {"name", field.name},
        {"primary_key", field.primaryKey},
        {"query", QueryPolicyJson(field.query)},
        {"redacted", field.redacted},
        {"representation", FieldRepresentationJson(field.representation)},
        {"semantic_type", TypeJson(field.type)},
        {"serialization", SerializationPolicyName(field.serialization)},
        {"source_field", field.sourceField.has_value() ? Json(*field.sourceField) : Json(nullptr)},
        {"source_provenance", FieldSourceKindName(field.sourceKind)},
        {"storage_name", field.storageName},
        {"temporal_role", TemporalRoleName(field.temporalRole)},
        {"write_once", field.writeOnce},
    };
}

Json FieldReferenceJson(const SemanticFieldReference& field)
{
    return {
        {"canonical_name", field.canonicalName},
        {"field_id", field.fieldId.value},
        {"handle", field.handle.value},
    };
}

Json FieldReferencesJson(const std::vector<SemanticFieldReference>& fields)
{
    Json values = Json::array();
    for (const auto& field : fields) values.push_back(FieldReferenceJson(field));
    return values;
}

Json KeyJson(const SemanticKey& key)
{
    return {
        {"catalog_identity", CatalogMemberIdentityJson(key.catalogIdentity)},
        {"fields", FieldReferencesJson(key.fields)},
        {"generated", key.generated},
        {"kind", KeyKindName(key.kind)},
        {"name", key.name},
    };
}

Json IndexJson(const SemanticIndex& index)
{
    return {
        {"catalog_identity", CatalogMemberIdentityJson(index.catalogIdentity)},
        {"fields", FieldReferencesJson(index.fields)},
        {"generated", index.generated},
        {"name", index.name},
        {"unique", index.unique},
    };
}

Json RelationJson(const SemanticRelation& relation)
{
    return {
        {"cardinality", RelationCardinalityName(relation.cardinality)},
        {"catalog_identity", CatalogMemberIdentityJson(relation.catalogIdentity)},
        {"name", relation.name},
        {"on_delete", ReferentialActionName(relation.onDelete)},
        {"on_update", ReferentialActionName(relation.onUpdate)},
        {"source_fields", FieldReferencesJson(relation.sourceFields)},
        {"target_fields", FieldReferencesJson(relation.targetFields)},
        {"target_model_handle", relation.targetModel.value},
        {"target_qualified_name", relation.targetQualifiedName},
        {"target_type_id", relation.targetTypeId.value},
    };
}

Json JoinJson(const SemanticJoin& join)
{
    return {
        {"catalog_identity", CatalogMemberIdentityJson(join.catalogIdentity)},
        {"name", join.name},
        {"source_relation", join.sourceRelation},
        {"target_relation", join.targetRelation},
        {"through_model_handle", join.throughModel.value},
        {"through_qualified_name", join.throughQualifiedName},
        {"through_type_id", join.throughTypeId.value},
    };
}

Json OneOfJson(const SemanticOneOf& one_of)
{
    Json variants = Json::array();
    for (const auto& variant : one_of.variants)
    {
        variants.push_back({
            {"catalog_identity", CatalogMemberIdentityJson(variant.catalogIdentity)},
            {"fields", FieldReferencesJson(variant.fields)},
            {"optional_fields", FieldReferencesJson(variant.optionalFields)},
            {"tag", variant.tag},
        });
    }
    return {
        {"catalog_identity", CatalogMemberIdentityJson(one_of.catalogIdentity)},
        {"discriminator", FieldReferenceJson(one_of.discriminator)},
        {"enum_handle", one_of.enumeration.value},
        {"name", one_of.name},
        {"variants", std::move(variants)},
    };
}

Json TemporalContractJson(const SemanticTemporalContract& contract)
{
    return {
        {"end_field", FieldReferenceJson(contract.endField)},
        {"end_inclusive", contract.endInclusive},
        {"name", contract.name},
        {"open_end", OpenEndPolicyName(contract.openEnd)},
        {"ordering", TemporalOrderingName(contract.ordering)},
        {"start_field", FieldReferenceJson(contract.startField)},
        {"start_inclusive", contract.startInclusive},
    };
}

Json ModelJson(const SemanticModel& model)
{
    Json fields = Json::array();
    for (const auto& field : model.fields) fields.push_back(FieldJson(field));
    Json keys = Json::array();
    for (const auto& key : model.keys) keys.push_back(KeyJson(key));
    Json indexes = Json::array();
    for (const auto& index : model.indexes) indexes.push_back(IndexJson(index));
    Json relations = Json::array();
    for (const auto& relation : model.relations) relations.push_back(RelationJson(relation));
    Json joins = Json::array();
    for (const auto& join : model.joins) joins.push_back(JoinJson(join));
    Json one_ofs = Json::array();
    for (const auto& one_of : model.oneOfs) one_ofs.push_back(OneOfJson(one_of));
    Json temporal_contracts = Json::array();
    for (const auto& contract : model.temporalContracts)
        temporal_contracts.push_back(TemporalContractJson(contract));
    Json source_aliases = Json::array();
    for (const auto& alias : model.sourceAliases) source_aliases.push_back(alias);
    Json retired_names = Json::array();
    for (const auto& name : model.retiredNames) retired_names.push_back(name);
    Json catalog_retired_names = Json::array();
    for (const auto& name : model.catalogRetiredNames) catalog_retired_names.push_back(name);
    return {
        {"catalog_retired_names", std::move(catalog_retired_names)},
        {"catalog_identity", CatalogModelIdentityJson(model.catalogIdentity)},
        {"description", model.description},
        {"domain", model.domain},
        {"external", model.external},
        {"fields", std::move(fields)},
        {"handle", model.handle.value},
        {"indexes", std::move(indexes)},
        {"joins", std::move(joins)},
        {"keys", std::move(keys)},
        {"kind", ModelKindName(model.kind)},
        {"mutation_authority", model.mutationAuthority},
        {"name", model.name},
        {"name_review", NameReviewStatusName(model.nameReview)},
        {"name_review_rationale", model.nameReviewRationale},
        {"one_ofs", std::move(one_ofs)},
        {"owner_capsule", model.ownerCapsule},
        {"qualified_name", model.qualifiedName},
        {"relations", std::move(relations)},
        {"retention", RetentionPolicyName(model.retention)},
        {"retired_names", std::move(retired_names)},
        {"role", ModelRoleName(model.role)},
        {"schema_name", model.schemaName},
        {"schema_version", model.schemaVersion},
        {"source_aliases", std::move(source_aliases)},
        {"source_handle", model.source.value},
        {"storage_name", model.storageName},
        {"temporal_contracts", std::move(temporal_contracts)},
        {"type_id", model.typeId.value},
    };
}

Json EnumJson(const SemanticEnum& declaration)
{
    Json values = Json::array();
    for (const auto& value : declaration.values)
        values.push_back({
            {"catalog_identity", CatalogMemberIdentityJson(value.catalogIdentity)},
            {"name", value.name},
            {"value_id", value.valueId.value},
        });
    return {
        {"enum_id", declaration.enumId.value},
        {"handle", declaration.handle.value},
        {"mutation_authority", declaration.mutationAuthority},
        {"name", declaration.name},
        {"owner_capsule", declaration.ownerCapsule},
        {"qualified_name", declaration.qualifiedName},
        {"semantic_type_handle", declaration.semanticType.value},
        {"semantic_type_id", declaration.semanticTypeId.value},
        {"source_handle", declaration.source.value},
        {"values", std::move(values)},
    };
}

Json ModelReferenceJson(const SemanticModelReference& reference)
{
    return {
        {"canonical_name", reference.canonicalName},
        {"model_handle", reference.model.value},
        {"type_id", reference.typeId.value},
    };
}

Json OptionalModelReferenceJson(const std::optional<SemanticModelReference>& reference)
{
    return reference.has_value() ? ModelReferenceJson(*reference) : Json(nullptr);
}

Json CommandJson(const SemanticCommand& command)
{
    Json errors = Json::array();
    for (const auto& error : command.errors) errors.push_back(ModelReferenceJson(error));
    Json targets = Json::array();
    for (const auto target : command.targets) targets.push_back(OperationTargetProfileName(target));
    Json fabric = nullptr;
    if (command.fabric.has_value())
    {
        fabric = {
            {"direct_eligible", command.fabric->directEligible},
            {"lane", FabricLaneName(command.fabric->lane)},
            {"maximum_encoded_bytes", command.fabric->maximumEncodedBytes},
        };
    }
    return {
        {"command_id", command.commandId.value},
        {"errors", std::move(errors)},
        {"fabric", std::move(fabric)},
        {"handle", command.handle.value},
        {"idempotency", CommandIdempotencyName(command.idempotency)},
        {"name", command.name},
        {"owner_capsule", command.ownerCapsule},
        {"qualified_name", command.qualifiedName},
        {"request", OptionalModelReferenceJson(command.request)},
        {"response", OptionalModelReferenceJson(command.response)},
        {"source_handle", command.source.value},
        {"streaming", CommandStreamingName(command.streaming)},
        {"targets", std::move(targets)},
    };
}

Json EventJson(const SemanticEvent& event)
{
    Json targets = Json::array();
    for (const auto target : event.targets) targets.push_back(OperationTargetProfileName(target));
    Json fabric = nullptr;
    if (event.fabric.has_value())
    {
        fabric = {
            {"direct_eligible", event.fabric->directEligible},
            {"lane", FabricLaneName(event.fabric->lane)},
            {"maximum_encoded_bytes", event.fabric->maximumEncodedBytes},
        };
    }
    return {
        {"delivery", EventDeliveryName(event.delivery)},
        {"event_id", event.eventId.value},
        {"fabric", std::move(fabric)},
        {"handle", event.handle.value},
        {"name", event.name},
        {"ordering", EventOrderingName(event.ordering)},
        {"owner_capsule", event.ownerCapsule},
        {"payload", ModelReferenceJson(event.payload)},
        {"qualified_name", event.qualifiedName},
        {"replay", EventReplayName(event.replay)},
        {"source_handle", event.source.value},
        {"targets", std::move(targets)},
    };
}

Json ScopeJson(const RegistryScope& scope)
{
    return {
        {"kind", RegistryKindName(scope.kind)},
        {
            "model_type_id",
            scope.modelTypeId.has_value() ? Json(scope.modelTypeId->value) : Json(nullptr),
        },
        {
            "enum_id",
            scope.enumId.has_value() ? Json(scope.enumId->value) : Json(nullptr),
        },
    };
}

Json ReservedIdJson(const SemanticReservedId& reserved)
{
    return {
        {"owner_capsule", reserved.ownerCapsule},
        {"scope", ScopeJson(reserved.scope)},
        {"value", reserved.value},
    };
}

Json ReservedNameJson(const SemanticReservedName& reserved)
{
    return {
        {"canonical_name", reserved.canonicalName},
        {"owner_capsule", reserved.ownerCapsule},
        {"scope", ScopeJson(reserved.scope)},
    };
}

Json SourceJson(const SemanticSource& source)
{
    Json imports = Json::array();
    for (const auto& import : source.imports)
    {
        imports.push_back({
            {"source_handle", import.source.value},
            {"source_path", import.sourcePath},
        });
    }
    Json models = Json::array();
    for (const auto handle : source.models) models.push_back(handle.value);
    Json wire_types = Json::array();
    for (const auto handle : source.wireTypes) wire_types.push_back(handle.value);
    Json storage_codecs = Json::array();
    for (const auto handle : source.storageCodecs) storage_codecs.push_back(handle.value);
    Json semantic_types = Json::array();
    for (const auto handle : source.semanticTypes) semantic_types.push_back(handle.value);
    Json enums = Json::array();
    for (const auto handle : source.enums) enums.push_back(handle.value);
    Json commands = Json::array();
    for (const auto handle : source.commands) commands.push_back(handle.value);
    Json events = Json::array();
    for (const auto handle : source.events) events.push_back(handle.value);
    return {
        {"command_handles", std::move(commands)},
        {"edition", source.edition},
        {"enum_handles", std::move(enums)},
        {"event_handles", std::move(events)},
        {"handle", source.handle.value},
        {"imports", std::move(imports)},
        {"model_handles", std::move(models)},
        {"owner_capsule", source.ownerCapsule},
        {"path", source.path},
        {"schema_name", source.schemaName},
        {"schema_version", source.schemaVersion},
        {"semantic_type_handles", std::move(semantic_types)},
        {"storage_codec_handles", std::move(storage_codecs)},
        {"wire_type_handles", std::move(wire_types)},
    };
}

Json IrJson(const SemanticIr& ir)
{
    Json sources = Json::array();
    for (const auto& source : ir.sources) sources.push_back(SourceJson(source));
    Json models = Json::array();
    for (const auto& model : ir.models) models.push_back(ModelJson(model));
    Json wire_types = Json::array();
    for (const auto& type : ir.wireTypes) wire_types.push_back(WireTypeJson(type));
    Json storage_codecs = Json::array();
    for (const auto& codec : ir.storageCodecs) storage_codecs.push_back(StorageCodecJson(codec));
    Json semantic_types = Json::array();
    for (const auto& type : ir.semanticTypes) semantic_types.push_back(SemanticTypeJson(type));
    Json enums = Json::array();
    for (const auto& declaration : ir.enums) enums.push_back(EnumJson(declaration));
    Json commands = Json::array();
    for (const auto& command : ir.commands) commands.push_back(CommandJson(command));
    Json events = Json::array();
    for (const auto& event : ir.events) events.push_back(EventJson(event));
    Json reserved_ids = Json::array();
    for (const auto& reserved : ir.reservedIds) reserved_ids.push_back(ReservedIdJson(reserved));
    Json reserved_names = Json::array();
    for (const auto& reserved : ir.reservedNames) reserved_names.push_back(ReservedNameJson(reserved));
    return {
        {"catalog", CatalogJson(ir.catalog)},
        {"commands", std::move(commands)},
        {"entry_source_handle", ir.entrySource.value},
        {"enums", std::move(enums)},
        {"events", std::move(events)},
        {"format_version", ir.formatVersion},
        {"models", std::move(models)},
        {"reserved_ids", std::move(reserved_ids)},
        {"reserved_names", std::move(reserved_names)},
        {"semantic_types", std::move(semantic_types)},
        {"sources", std::move(sources)},
        {"storage_codecs", std::move(storage_codecs)},
        {"wire_types", std::move(wire_types)},
    };
}

bool IsLowerHexSha256(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
    });
}

bool ManifestCanonicalInteger(std::string_view value, bool allow_negative)
{
    if (value.empty() || value.front() == '+') return false;
    std::size_t offset = 0;
    if (value.front() == '-')
    {
        if (!allow_negative || value.size() == 1) return false;
        offset = 1;
    }
    if (value[offset] == '0' && value.size() - offset > 1) return false;
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(offset), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

template <typename Integer>
bool ManifestParseInteger(std::string_view value, Integer& output)
{
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool ManifestIntegerFits(SemanticTypeKind kind, std::string_view value)
{
    const bool signed_kind = kind == SemanticTypeKind::I8 || kind == SemanticTypeKind::I16 ||
        kind == SemanticTypeKind::I32 || kind == SemanticTypeKind::I64 || kind == SemanticTypeKind::Timestamp;
    if (!ManifestCanonicalInteger(value, signed_kind)) return false;
    if (signed_kind)
    {
        std::int64_t parsed = 0;
        if (!ManifestParseInteger(value, parsed)) return false;
        if (kind == SemanticTypeKind::I8)
            return parsed >= std::numeric_limits<std::int8_t>::min() && parsed <= std::numeric_limits<std::int8_t>::max();
        if (kind == SemanticTypeKind::I16)
            return parsed >= std::numeric_limits<std::int16_t>::min() && parsed <= std::numeric_limits<std::int16_t>::max();
        if (kind == SemanticTypeKind::I32)
            return parsed >= std::numeric_limits<std::int32_t>::min() && parsed <= std::numeric_limits<std::int32_t>::max();
        return true;
    }
    std::uint64_t parsed = 0;
    if (!ManifestParseInteger(value, parsed)) return false;
    if (kind == SemanticTypeKind::U8) return parsed <= std::numeric_limits<std::uint8_t>::max();
    if (kind == SemanticTypeKind::U16) return parsed <= std::numeric_limits<std::uint16_t>::max();
    if (kind == SemanticTypeKind::U32) return parsed <= std::numeric_limits<std::uint32_t>::max();
    return kind == SemanticTypeKind::U64;
}

bool ManifestRealValid(std::string_view value)
{
    const auto parsed = Json::parse(value.begin(), value.end(), nullptr, false, false);
    return !parsed.is_discarded() && parsed.is_number() && std::isfinite(parsed.get<double>());
}

bool ManifestDecimalValid(std::string_view value, std::uint32_t precision, std::uint32_t scale)
{
    if (value.empty() || value.front() == '+') return false;
    std::size_t offset = value.front() == '-' ? 1 : 0;
    if (offset == value.size()) return false;
    const auto point = value.find('.', offset);
    const auto integer_end = point == std::string_view::npos ? value.size() : point;
    const auto digit = [](char character) { return character >= '0' && character <= '9'; };
    if (integer_end == offset || (value[offset] == '0' && integer_end - offset > 1) ||
        !std::all_of(
            value.begin() + static_cast<std::ptrdiff_t>(offset),
            value.begin() + static_cast<std::ptrdiff_t>(integer_end),
            digit
        ))
        return false;
    std::size_t fraction = 0;
    if (point != std::string_view::npos)
    {
        fraction = value.size() - point - 1;
        if (fraction == 0 ||
            !std::all_of(value.begin() + static_cast<std::ptrdiff_t>(point + 1), value.end(), digit))
            return false;
    }
    return integer_end - offset + fraction <= precision && fraction <= scale;
}

bool ManifestAllTargets(const std::vector<TargetProfile>& targets)
{
    const auto includes = [&targets](std::initializer_list<TargetProfile> required) {
        return std::all_of(required.begin(), required.end(), [&targets](TargetProfile target) {
            return std::find(targets.begin(), targets.end(), target) != targets.end();
        });
    };
    return includes({
               TargetProfile::Cpp,
               TargetProfile::Sqlite,
               TargetProfile::CSharp,
               TargetProfile::JavaScript,
           }) ||
        includes({
            TargetProfile::Cpp,
            TargetProfile::Fabric,
            TargetProfile::HttpJson,
        });
}

bool ManifestExplicitCheckCompatible(
    CheckKind check,
    SemanticTypeKind type,
    WirePrimitiveKind wire
)
{
    const bool integer = type == SemanticTypeKind::U8 || type == SemanticTypeKind::U16 ||
        type == SemanticTypeKind::U32 || type == SemanticTypeKind::U64 || type == SemanticTypeKind::I8 ||
        type == SemanticTypeKind::I16 || type == SemanticTypeKind::I32 || type == SemanticTypeKind::I64 ||
        type == SemanticTypeKind::Timestamp;
    if (check == CheckKind::MinimumInteger || check == CheckKind::MaximumInteger) return integer;
    if (check == CheckKind::MinimumReal || check == CheckKind::MaximumReal)
        return type == SemanticTypeKind::F32 || type == SemanticTypeKind::F64 || type == SemanticTypeKind::Decimal;
    if (check == CheckKind::MinimumLength || check == CheckKind::MaximumLength || check == CheckKind::FixedLength)
        return wire == WirePrimitiveKind::Utf8 || wire == WirePrimitiveKind::Bytes ||
            wire == WirePrimitiveKind::FixedBytes;
    return false;
}

bool ManifestFieldKindCompatible(FieldKind field, SemanticTypeKind semantic)
{
    switch (field)
    {
    case FieldKind::ObjectId:
        return semantic == SemanticTypeKind::FixedId || semantic == SemanticTypeKind::Text;
    case FieldKind::InternalId:
    case FieldKind::Integer:
        return semantic == SemanticTypeKind::U8 || semantic == SemanticTypeKind::U16 ||
            semantic == SemanticTypeKind::U32 || semantic == SemanticTypeKind::U64 ||
            semantic == SemanticTypeKind::I8 || semantic == SemanticTypeKind::I16 ||
            semantic == SemanticTypeKind::I32 || semantic == SemanticTypeKind::I64;
    case FieldKind::Text:
    case FieldKind::Slug:
    case FieldKind::DisplayName:
    case FieldKind::Url:
    case FieldKind::Email:
    case FieldKind::Secret:
    case FieldKind::KeyReference:
        return semantic == SemanticTypeKind::Text;
    case FieldKind::Enum: return semantic == SemanticTypeKind::Enum;
    case FieldKind::Bool: return semantic == SemanticTypeKind::Bool;
    case FieldKind::TimestampMs: return semantic == SemanticTypeKind::Timestamp;
    case FieldKind::JsonText: return semantic == SemanticTypeKind::JsonText;
    case FieldKind::EthereumAddress:
    case FieldKind::HashHex:
    case FieldKind::TokenHash:
        return semantic == SemanticTypeKind::Blob || semantic == SemanticTypeKind::Text;
    case FieldKind::Real:
        return semantic == SemanticTypeKind::F32 || semantic == SemanticTypeKind::F64;
    case FieldKind::Decimal: return semantic == SemanticTypeKind::Decimal;
    case FieldKind::Blob:
        return semantic == SemanticTypeKind::Blob || semantic == SemanticTypeKind::FixedId;
    }
    return false;
}

bool ManifestFieldBindingCompatible(
    SemanticTypeKind semantic,
    WirePrimitiveKind wire,
    StorageCodecKind storage
)
{
    switch (semantic)
    {
    case SemanticTypeKind::Bool:
        return wire == WirePrimitiveKind::Bool && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U8:
        return wire == WirePrimitiveKind::U8 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U16:
        return wire == WirePrimitiveKind::U16 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U32:
        return wire == WirePrimitiveKind::U32 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U64:
        return wire == WirePrimitiveKind::U64 && storage == StorageCodecKind::SqliteOrderedU64;
    case SemanticTypeKind::I8:
        return wire == WirePrimitiveKind::I8 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I16:
        return wire == WirePrimitiveKind::I16 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I32:
        return wire == WirePrimitiveKind::I32 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I64:
    case SemanticTypeKind::Timestamp:
        return wire == WirePrimitiveKind::I64 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::F32:
        return wire == WirePrimitiveKind::F32 && storage == StorageCodecKind::SqliteReal;
    case SemanticTypeKind::F64:
        return wire == WirePrimitiveKind::F64 && storage == StorageCodecKind::SqliteReal;
    case SemanticTypeKind::Text:
    case SemanticTypeKind::JsonText:
        return wire == WirePrimitiveKind::Utf8 && storage == StorageCodecKind::SqliteText;
    case SemanticTypeKind::Enum:
        return ((wire == WirePrimitiveKind::U8 || wire == WirePrimitiveKind::U16 ||
                 wire == WirePrimitiveKind::U32) && storage == StorageCodecKind::SqliteInteger) ||
            (wire == WirePrimitiveKind::Utf8 && storage == StorageCodecKind::SqliteText);
    case SemanticTypeKind::Blob:
        return (wire == WirePrimitiveKind::Bytes && storage == StorageCodecKind::SqliteBlob) ||
            (wire == WirePrimitiveKind::FixedBytes &&
             (storage == StorageCodecKind::SqliteFixedBlob || storage == StorageCodecKind::SqliteText));
    case SemanticTypeKind::FixedId:
        return wire == WirePrimitiveKind::FixedBytes &&
            (storage == StorageCodecKind::SqliteFixedBlob || storage == StorageCodecKind::SqliteText);
    case SemanticTypeKind::Decimal:
        return wire == WirePrimitiveKind::Decimal128 &&
            storage == StorageCodecKind::SqliteOrderedDecimal128;
    }
    return false;
}

bool IsCanonicalSourcePath(std::string_view value)
{
    if (!HasSchemaSourceExtension(value) ||
        value.front() == '/' || value.find("//") != std::string_view::npos ||
        value.find("/./") != std::string_view::npos ||
        value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos)
    {
        return false;
    }
    if (!std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '_' || character == '-' || character == '.' || character == '/';
    }))
    {
        return false;
    }
    std::size_t segment_begin = 0;
    while (segment_begin <= value.size())
    {
        const auto slash = value.find('/', segment_begin);
        const auto segment_end = slash == std::string_view::npos ? value.size() : slash;
        const auto segment = value.substr(segment_begin, segment_end - segment_begin);
        if (segment == "." || segment == "..") return false;
        if (slash == std::string_view::npos) break;
        segment_begin = slash + 1;
    }
    return true;
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

bool ManifestBindingCompatible(SemanticTypeKind semantic, WirePrimitiveKind wire, StorageCodecKind storage)
{
    switch (semantic)
    {
    case SemanticTypeKind::Bool:
        return wire == WirePrimitiveKind::Bool && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U8:
        return wire == WirePrimitiveKind::U8 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U16:
        return wire == WirePrimitiveKind::U16 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U32:
        return wire == WirePrimitiveKind::U32 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::U64:
        return wire == WirePrimitiveKind::U64 && storage == StorageCodecKind::SqliteOrderedU64;
    case SemanticTypeKind::I8:
        return wire == WirePrimitiveKind::I8 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I16:
        return wire == WirePrimitiveKind::I16 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I32:
        return wire == WirePrimitiveKind::I32 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::I64:
    case SemanticTypeKind::Timestamp:
        return wire == WirePrimitiveKind::I64 && storage == StorageCodecKind::SqliteInteger;
    case SemanticTypeKind::F32:
        return wire == WirePrimitiveKind::F32 && storage == StorageCodecKind::SqliteReal;
    case SemanticTypeKind::F64:
        return wire == WirePrimitiveKind::F64 && storage == StorageCodecKind::SqliteReal;
    case SemanticTypeKind::Text:
    case SemanticTypeKind::JsonText:
        return wire == WirePrimitiveKind::Utf8 && storage == StorageCodecKind::SqliteText;
    case SemanticTypeKind::Enum:
        return ((wire == WirePrimitiveKind::U8 || wire == WirePrimitiveKind::U16 ||
                 wire == WirePrimitiveKind::U32) && storage == StorageCodecKind::SqliteInteger) ||
            (wire == WirePrimitiveKind::Utf8 && storage == StorageCodecKind::SqliteText);
    case SemanticTypeKind::Blob:
        return (wire == WirePrimitiveKind::Bytes && storage == StorageCodecKind::SqliteBlob) ||
            (wire == WirePrimitiveKind::FixedBytes &&
             (storage == StorageCodecKind::SqliteFixedBlob || storage == StorageCodecKind::SqliteText));
    case SemanticTypeKind::FixedId:
        return wire == WirePrimitiveKind::FixedBytes &&
            (storage == StorageCodecKind::SqliteFixedBlob || storage == StorageCodecKind::SqliteText);
    case SemanticTypeKind::Decimal:
        return wire == WirePrimitiveKind::Decimal128 && storage == StorageCodecKind::SqliteOrderedDecimal128;
    }
    return false;
}

bool ManifestWireShapeValid(const WireTypeDefinition& type)
{
    if (type.wireTypeId.value != static_cast<std::uint32_t>(type.kind)) return false;
    switch (type.kind)
    {
    case WirePrimitiveKind::Bool:
        return type.bitWidth == 1 && type.fixedSize == 1 && type.byteOrder == WireByteOrder::None;
    case WirePrimitiveKind::U8:
    case WirePrimitiveKind::I8:
        return type.bitWidth == 8 && type.fixedSize == 1 && type.byteOrder == WireByteOrder::None;
    case WirePrimitiveKind::U16:
    case WirePrimitiveKind::I16:
        return type.bitWidth == 16 && type.fixedSize == 2 && type.byteOrder == WireByteOrder::BigEndian;
    case WirePrimitiveKind::U32:
    case WirePrimitiveKind::I32:
    case WirePrimitiveKind::F32:
        return type.bitWidth == 32 && type.fixedSize == 4 && type.byteOrder == WireByteOrder::BigEndian;
    case WirePrimitiveKind::U64:
    case WirePrimitiveKind::I64:
    case WirePrimitiveKind::F64:
        return type.bitWidth == 64 && type.fixedSize == 8 && type.byteOrder == WireByteOrder::BigEndian;
    case WirePrimitiveKind::Decimal128:
        return type.bitWidth == 128 && type.fixedSize == 16 && type.byteOrder == WireByteOrder::BigEndian;
    case WirePrimitiveKind::Utf8:
    case WirePrimitiveKind::Bytes:
    case WirePrimitiveKind::FixedBytes:
        return type.bitWidth == 0 && type.fixedSize == 0 && type.byteOrder == WireByteOrder::None;
    }
    return false;
}

bool ManifestStorageShapeValid(const StorageCodecDefinition& codec)
{
    if (!codec.exact || codec.storageCodecId.value != static_cast<std::uint32_t>(codec.kind)) return false;
    switch (codec.kind)
    {
    case StorageCodecKind::SqliteInteger:
        return codec.storageClass == SqliteStorageClass::Integer && codec.fixedSize == 0 && codec.orderPreserving;
    case StorageCodecKind::SqliteReal:
        return codec.storageClass == SqliteStorageClass::Real && codec.fixedSize == 0;
    case StorageCodecKind::SqliteText:
        return codec.storageClass == SqliteStorageClass::Text && codec.fixedSize == 0;
    case StorageCodecKind::SqliteBlob:
    case StorageCodecKind::SqliteFixedBlob:
        return codec.storageClass == SqliteStorageClass::Blob && codec.fixedSize == 0;
    case StorageCodecKind::SqliteOrderedU64:
        return codec.storageClass == SqliteStorageClass::Blob && codec.fixedSize == 8 && codec.orderPreserving;
    case StorageCodecKind::SqliteOrderedDecimal128:
        return codec.storageClass == SqliteStorageClass::Blob && codec.fixedSize == 16 && codec.orderPreserving;
    }
    return false;
}

bool ManifestSemanticShapeValid(const SemanticTypeDefinition& type, const WireTypeDefinition& wire)
{
    if (!detail::JsonContractValid(type.kind, type.jsonProfile, type.boundaryMapping)) return false;
    switch (type.kind)
    {
    case SemanticTypeKind::FixedId:
        return type.fixedSize > 0 && type.precision == 0 && type.scale == 0 &&
               type.normalization == NormalizationKind::None;
    case SemanticTypeKind::Decimal:
        return type.fixedSize == 16 && type.precision >= 1 && type.precision <= 38 && type.scale <= type.precision &&
               type.normalization == NormalizationKind::None;
    case SemanticTypeKind::Text:
        return type.fixedSize == 0 && type.precision == 0 && type.scale == 0;
    case SemanticTypeKind::Enum:
        return type.precision == 0 && type.scale == 0 &&
            ((wire.kind == WirePrimitiveKind::Utf8 && type.fixedSize == 0) ||
             (wire.kind != WirePrimitiveKind::Utf8 && type.fixedSize == wire.fixedSize &&
              type.normalization == NormalizationKind::None));
    case SemanticTypeKind::Blob:
        return type.precision == 0 && type.scale == 0 &&
            type.normalization == NormalizationKind::None &&
            ((wire.kind == WirePrimitiveKind::Bytes && type.fixedSize == 0) ||
             (wire.kind == WirePrimitiveKind::FixedBytes && type.fixedSize > 0));
    case SemanticTypeKind::JsonText:
        return type.fixedSize == 0 && type.precision == 0 && type.scale == 0 &&
               type.normalization == NormalizationKind::None;
    default:
        return type.fixedSize == wire.fixedSize && type.precision == 0 && type.scale == 0 &&
               type.normalization == NormalizationKind::None;
    }
}

SourceSpan EmptySpan()
{
    return {};
}

class ManifestReader
{
public:
    ManifestReader(std::string_view source_name, const ParseLimits& limits)
        : sourceName_(source_name), limits_(limits)
    {
    }

    SemanticManifestResult Read(std::string_view manifest_json)
    {
        if (limits_.maximumDiagnostics == 0)
        {
            diagnostics_.push_back({
                .code = "idl.invalid_parse_limits",
                .severity = DiagnosticSeverity::Error,
                .message = "The diagnostic limit must be greater than zero.",
                .sourceName = sourceName_,
                .span = EmptySpan(),
            });
            return Finish();
        }
        if (manifest_json.size() > limits_.maximumManifestBytes)
        {
            Add("idl.manifest_too_large", "Semantic manifest exceeds the configured byte limit.", "/");
            return Finish();
        }

        const Json root = Json::parse(manifest_json.begin(), manifest_json.end(), nullptr, false, false);
        if (root.is_discarded())
        {
            Add("idl.manifest_json_invalid", "Semantic manifest is not valid JSON.", "/");
            return Finish();
        }
        if (!ExactObject(root, "/", {"format", "format_version", "semantic_fingerprint", "semantic_ir"}))
            return Finish();

        const auto format = String(root, "format", "/format");
        const auto format_version = U32(root, "format_version", "/format_version");
        const auto fingerprint = String(root, "semantic_fingerprint", "/semantic_fingerprint");
        if (!format.has_value() || !format_version.has_value() || !fingerprint.has_value())
            return Finish();
        if (*format != kManifestFormat)
        {
            Add("idl.manifest_format_invalid", "Semantic manifest format identifier is not supported.", "/format");
            return Finish();
        }
        if (*format_version != kManifestFormatVersion)
        {
            Add("idl.manifest_version_unsupported", "Semantic manifest format version is not supported.", "/format_version");
            return Finish();
        }
        if (!IsLowerHexSha256(*fingerprint))
        {
            Add("idl.manifest_fingerprint_invalid", "Semantic fingerprint must be lowercase SHA-256 hex.", "/semantic_fingerprint");
            return Finish();
        }

        const auto ir_found = root.find("semantic_ir");
        auto ir = ReadIr(*ir_found, "/semantic_ir");
        if (!ir.has_value()) return Finish();
        if (!ValidateIr(*ir)) return Finish();

        const std::string semantic_bytes = IrJson(*ir).dump();
        const std::string expected_fingerprint = detail::Sha256Hex(semantic_bytes);
        if (*fingerprint != expected_fingerprint)
        {
            Add("idl.manifest_fingerprint_mismatch", "Semantic fingerprint does not match the canonical IR payload.", "/semantic_fingerprint");
            return Finish();
        }
        manifest_ = detail::CreateSemanticManifest(std::move(*ir));
        return Finish();
    }

private:
    bool ExactObject(
        const Json& value,
        std::string_view path,
        std::initializer_list<std::string_view> expected
    )
    {
        if (!value.is_object())
        {
            Add("idl.manifest_type_invalid", "Expected JSON object at " + std::string(path) + ".", path);
            return false;
        }
        for (const auto key : expected)
        {
            if (!value.contains(std::string(key)))
            {
                Add(
                    "idl.manifest_member_missing",
                    "Required manifest member is missing: " + std::string(path) + std::string(key) + ".",
                    path
                );
                return false;
            }
        }
        for (auto member = value.begin(); member != value.end(); ++member)
        {
            const bool known = std::any_of(expected.begin(), expected.end(), [&member](std::string_view key) {
                return member.key() == key;
            });
            if (!known)
            {
                Add(
                    "idl.manifest_member_unknown",
                    "Unknown manifest member: " + std::string(path) + member.key() + ".",
                    path
                );
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> String(
        const Json& object,
        std::string_view key,
        std::string_view path
    )
    {
        const auto found = object.find(std::string(key));
        if (found == object.end() || !found->is_string())
        {
            Add("idl.manifest_type_invalid", "Expected JSON string at " + std::string(path) + ".", path);
            return std::nullopt;
        }
        return found->get<std::string>();
    }

    std::optional<std::uint64_t> Unsigned(
        const Json& value,
        std::string_view path
    )
    {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (value.is_number_integer())
        {
            const auto signed_value = value.get<std::int64_t>();
            if (signed_value >= 0) return static_cast<std::uint64_t>(signed_value);
        }
        Add("idl.manifest_type_invalid", "Expected unsigned integer at " + std::string(path) + ".", path);
        return std::nullopt;
    }

    std::optional<std::uint32_t> U32(
        const Json& object,
        std::string_view key,
        std::string_view path
    )
    {
        const auto found = object.find(std::string(key));
        if (found == object.end())
        {
            Add("idl.manifest_member_missing", "Required integer member is missing at " + std::string(path) + ".", path);
            return std::nullopt;
        }
        const auto value = Unsigned(*found, path);
        if (!value.has_value()) return std::nullopt;
        if (*value > std::numeric_limits<std::uint32_t>::max())
        {
            Add("idl.manifest_integer_out_of_range", "Manifest integer must fit in u32 at " + std::string(path) + ".", path);
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value);
    }

    std::optional<bool> Boolean(
        const Json& object,
        std::string_view key,
        std::string_view path
    )
    {
        const auto found = object.find(std::string(key));
        if (found == object.end() || !found->is_boolean())
        {
            Add("idl.manifest_type_invalid", "Expected boolean at " + std::string(path) + ".", path);
            return std::nullopt;
        }
        return found->get<bool>();
    }

    std::optional<RegistryScope> ReadScope(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"enum_id", "kind", "model_type_id"}))
            return std::nullopt;
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto model_type_id = value.find("model_type_id");
        const auto enum_id = value.find("enum_id");
        if (!kind_name.has_value()) return std::nullopt;

        RegistryKind kind;
        if (*kind_name == "model") kind = RegistryKind::Model;
        else if (*kind_name == "field") kind = RegistryKind::Field;
        else if (*kind_name == "semantic_type") kind = RegistryKind::SemanticType;
        else if (*kind_name == "wire_type") kind = RegistryKind::WireType;
        else if (*kind_name == "storage_codec") kind = RegistryKind::StorageCodec;
        else if (*kind_name == "enum") kind = RegistryKind::Enum;
        else if (*kind_name == "enum_value") kind = RegistryKind::EnumValue;
        else if (*kind_name == "command") kind = RegistryKind::Command;
        else if (*kind_name == "event") kind = RegistryKind::Event;
        else
        {
            Add("idl.manifest_enum_invalid", "Unknown registry kind.", path + "/kind");
            return std::nullopt;
        }

        std::optional<ModelTypeId> model;
        std::optional<EnumId> enumeration;
        if (!model_type_id->is_null())
        {
            const auto value_at = Unsigned(*model_type_id, path + "/model_type_id");
            if (!value_at.has_value() || *value_at > std::numeric_limits<std::uint32_t>::max())
            {
                if (value_at.has_value())
                    Add("idl.manifest_integer_out_of_range", "Model type ID must fit in u32.", path);
                return std::nullopt;
            }
            model = ModelTypeId{static_cast<std::uint32_t>(*value_at)};
        }
        if (!enum_id->is_null())
        {
            const auto value_at = Unsigned(*enum_id, path + "/enum_id");
            if (!value_at.has_value() || *value_at > std::numeric_limits<std::uint32_t>::max())
            {
                if (value_at.has_value())
                    Add("idl.manifest_integer_out_of_range", "Enum ID must fit in u32.", path);
                return std::nullopt;
            }
            enumeration = EnumId{static_cast<std::uint32_t>(*value_at)};
        }
        RegistryScope scope{.kind = kind, .modelTypeId = model, .enumId = enumeration};
        if (!scope.valid())
        {
            Add("idl.manifest_semantic_invalid", "Registry scope shape is invalid.", path);
            return std::nullopt;
        }
        return scope;
    }

    std::optional<SemanticReservedId> ReadReservedId(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"owner_capsule", "scope", "value"}))
            return std::nullopt;
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto numeric = U32(value, "value", path + "/value");
        const auto scope_value = value.find("scope");
        if (!owner.has_value() || !numeric.has_value()) return std::nullopt;
        auto scope = ReadScope(*scope_value, path + "/scope");
        if (!scope.has_value()) return std::nullopt;
        return SemanticReservedId{
            .scope = *scope,
            .value = *numeric,
            .ownerCapsule = *owner,
        };
    }

    std::optional<SemanticReservedName> ReadReservedName(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "owner_capsule", "scope"}))
            return std::nullopt;
        const auto canonical_name = String(value, "canonical_name", path + "/canonical_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto scope_value = value.find("scope");
        if (!canonical_name.has_value() || !owner.has_value()) return std::nullopt;
        auto scope = ReadScope(*scope_value, path + "/scope");
        if (!scope.has_value()) return std::nullopt;
        return SemanticReservedName{
            .scope = *scope,
            .canonicalName = *canonical_name,
            .ownerCapsule = *owner,
        };
    }

    std::optional<SemanticCatalog> ReadCatalog(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"catalog_id", "fragments", "version"}))
            return std::nullopt;
        const auto catalog_id = String(value, "catalog_id", path + "/catalog_id");
        const auto version = U32(value, "version", path + "/version");
        const auto fragments = value.find("fragments");
        if (!catalog_id || !version || !fragments->is_array()) return std::nullopt;
        if (fragments->size() > limits_.maximumCatalogFragments)
        {
            Add("idl.manifest_limit_exceeded", "Catalog fragment count exceeds the configured limit.", path);
            return std::nullopt;
        }
        SemanticCatalog catalog{.catalogId = *catalog_id, .version = *version};
        for (std::size_t index = 0; index < fragments->size(); ++index)
        {
            const auto fragment_path = path + "/fragments/" + std::to_string(index);
            const auto& fragment = (*fragments)[index];
            if (!ExactObject(
                    fragment,
                    fragment_path + "/",
                    {"declaration_order", "fragment_id", "owner_capsule", "version"}
                ))
                return std::nullopt;
            const auto fragment_id = String(fragment, "fragment_id", fragment_path + "/fragment_id");
            const auto owner = String(fragment, "owner_capsule", fragment_path + "/owner_capsule");
            const auto fragment_version = U32(fragment, "version", fragment_path + "/version");
            const auto order = U32(fragment, "declaration_order", fragment_path + "/declaration_order");
            if (!fragment_id || !owner || !fragment_version || !order) return std::nullopt;
            catalog.fragments.push_back({
                .fragmentId = *fragment_id,
                .ownerCapsule = *owner,
                .version = *fragment_version,
                .declarationOrder = *order,
            });
        }
        return catalog;
    }

    std::optional<SemanticCatalogModelIdentity> ReadCatalogModelIdentity(
        const Json& value,
        const std::string& path
    )
    {
        if (!ExactObject(
                value,
                path + "/",
            {"declaration_order", "domain", "fragment_id", "logical_name", "model_id", "qualified_name", "registry_key"}
            ))
            return std::nullopt;
        const auto fragment_id = String(value, "fragment_id", path + "/fragment_id");
        const auto registry_key = String(value, "registry_key", path + "/registry_key");
        const auto model_id = String(value, "model_id", path + "/model_id");
        const auto logical_name = String(value, "logical_name", path + "/logical_name");
        const auto qualified_name = String(value, "qualified_name", path + "/qualified_name");
        const auto domain = String(value, "domain", path + "/domain");
        const auto order = U32(value, "declaration_order", path + "/declaration_order");
        if (!fragment_id || !registry_key || !model_id || !logical_name || !qualified_name || !domain || !order)
            return std::nullopt;
        return SemanticCatalogModelIdentity{
            .fragmentId = *fragment_id,
            .registryKey = *registry_key,
            .modelId = *model_id,
            .logicalName = *logical_name,
            .qualifiedName = *qualified_name,
            .domain = *domain,
            .declarationOrder = *order,
        };
    }

    std::optional<SemanticCatalogFieldIdentity> ReadCatalogFieldIdentity(
        const Json& value,
        const std::string& path
    )
    {
        if (!ExactObject(value, path + "/", {"declaration_order", "field_name", "registry_key"}))
            return std::nullopt;
        const auto registry_key = String(value, "registry_key", path + "/registry_key");
        const auto field_name = String(value, "field_name", path + "/field_name");
        const auto order = U32(value, "declaration_order", path + "/declaration_order");
        if (!registry_key || !field_name || !order) return std::nullopt;
        return SemanticCatalogFieldIdentity{
            .registryKey = *registry_key,
            .fieldName = *field_name,
            .declarationOrder = *order,
        };
    }

    std::optional<SemanticCatalogMemberIdentity> ReadCatalogMemberIdentity(
        const Json& value,
        const std::string& path
    )
    {
        if (!ExactObject(value, path + "/", {"declaration_order", "name"}))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto order = U32(value, "declaration_order", path + "/declaration_order");
        if (!name || !order) return std::nullopt;
        return SemanticCatalogMemberIdentity{
            .name = *name,
            .declarationOrder = *order,
        };
    }

    std::optional<SemanticIr> ReadIr(const Json& value, std::string_view path)
    {
        if (!ExactObject(
                value,
                path,
                {"catalog", "commands", "entry_source_handle", "enums", "events", "format_version", "models",
                 "reserved_ids", "reserved_names", "semantic_types", "sources",
                 "storage_codecs",
                 "wire_types"}
            ))
            return std::nullopt;
        const auto format_version = U32(value, "format_version", "/semantic_ir/format_version");
        const auto entry = U32(value, "entry_source_handle", "/semantic_ir/entry_source_handle");
        const auto catalog = value.find("catalog");
        const auto sources = value.find("sources");
        const auto wire_types = value.find("wire_types");
        const auto storage_codecs = value.find("storage_codecs");
        const auto semantic_types = value.find("semantic_types");
        const auto enums = value.find("enums");
        const auto models = value.find("models");
        const auto commands = value.find("commands");
        const auto events = value.find("events");
        const auto reserved_ids = value.find("reserved_ids");
        const auto reserved_names = value.find("reserved_names");
        if (!format_version.has_value() || !entry.has_value()) return std::nullopt;
        if (!sources->is_array() || !wire_types->is_array() || !storage_codecs->is_array() ||
            !semantic_types->is_array() || !enums->is_array() || !models->is_array() ||
            !commands->is_array() || !events->is_array() ||
            !reserved_ids->is_array() || !reserved_names->is_array())
        {
            Add("idl.manifest_type_invalid", "Semantic sources, models, and reservations must be JSON arrays.", path);
            return std::nullopt;
        }
        if (sources->size() > limits_.maximumSources || wire_types->size() > limits_.maximumWireTypes ||
            storage_codecs->size() > limits_.maximumStorageCodecs ||
            semantic_types->size() > limits_.maximumSemanticTypes ||
            enums->size() > limits_.maximumEnums || models->size() > limits_.maximumModels ||
            commands->size() > limits_.maximumCommands || events->size() > limits_.maximumEvents ||
            reserved_ids->size() + reserved_names->size() > limits_.maximumReservations)
        {
            Add("idl.manifest_limit_exceeded", "Semantic source count exceeds the configured limit.", "/semantic_ir/sources");
            return std::nullopt;
        }

        SemanticIr ir{.formatVersion = *format_version, .entrySource = SourceHandle{*entry}};
        if (!catalog->is_null())
        {
            auto parsed_catalog = ReadCatalog(*catalog, "/semantic_ir/catalog");
            if (!parsed_catalog.has_value()) return std::nullopt;
            ir.catalog = std::move(*parsed_catalog);
        }
        ir.sources.reserve(sources->size());
        for (std::size_t index = 0; index < sources->size(); ++index)
        {
            auto source = ReadSource((*sources)[index], "/semantic_ir/sources/" + std::to_string(index));
            if (!source.has_value()) return std::nullopt;
            ir.sources.push_back(std::move(*source));
        }
        ir.wireTypes.reserve(wire_types->size());
        for (std::size_t index = 0; index < wire_types->size(); ++index)
        {
            auto type = ReadWireType((*wire_types)[index], "/semantic_ir/wire_types/" + std::to_string(index));
            if (!type.has_value()) return std::nullopt;
            ir.wireTypes.push_back(std::move(*type));
        }
        ir.storageCodecs.reserve(storage_codecs->size());
        for (std::size_t index = 0; index < storage_codecs->size(); ++index)
        {
            auto codec =
                ReadStorageCodec((*storage_codecs)[index], "/semantic_ir/storage_codecs/" + std::to_string(index));
            if (!codec.has_value()) return std::nullopt;
            ir.storageCodecs.push_back(std::move(*codec));
        }
        ir.semanticTypes.reserve(semantic_types->size());
        for (std::size_t index = 0; index < semantic_types->size(); ++index)
        {
            auto type =
                ReadSemanticType((*semantic_types)[index], "/semantic_ir/semantic_types/" + std::to_string(index));
            if (!type.has_value()) return std::nullopt;
            ir.semanticTypes.push_back(std::move(*type));
        }
        ir.enums.reserve(enums->size());
        for (std::size_t index = 0; index < enums->size(); ++index)
        {
            auto declaration = ReadEnum((*enums)[index], "/semantic_ir/enums/" + std::to_string(index));
            if (!declaration.has_value()) return std::nullopt;
            ir.enums.push_back(std::move(*declaration));
        }
        ir.models.reserve(models->size());
        for (std::size_t index = 0; index < models->size(); ++index)
        {
            auto model = ReadModel((*models)[index], "/semantic_ir/models/" + std::to_string(index));
            if (!model.has_value()) return std::nullopt;
            ir.models.push_back(std::move(*model));
        }
        ir.commands.reserve(commands->size());
        for (std::size_t index = 0; index < commands->size(); ++index)
        {
            auto command = ReadCommand((*commands)[index], "/semantic_ir/commands/" + std::to_string(index));
            if (!command.has_value()) return std::nullopt;
            ir.commands.push_back(std::move(*command));
        }
        ir.events.reserve(events->size());
        for (std::size_t index = 0; index < events->size(); ++index)
        {
            auto event = ReadEvent((*events)[index], "/semantic_ir/events/" + std::to_string(index));
            if (!event.has_value()) return std::nullopt;
            ir.events.push_back(std::move(*event));
        }
        ir.reservedIds.reserve(reserved_ids->size());
        for (std::size_t index = 0; index < reserved_ids->size(); ++index)
        {
            auto reserved = ReadReservedId((*reserved_ids)[index],
                "/semantic_ir/reserved_ids/" + std::to_string(index));
            if (!reserved.has_value()) return std::nullopt;
            ir.reservedIds.push_back(std::move(*reserved));
        }
        ir.reservedNames.reserve(reserved_names->size());
        for (std::size_t index = 0; index < reserved_names->size(); ++index)
        {
            auto reserved = ReadReservedName((*reserved_names)[index],
                "/semantic_ir/reserved_names/" + std::to_string(index));
            if (!reserved.has_value()) return std::nullopt;
            ir.reservedNames.push_back(std::move(*reserved));
        }
        return ir;
    }

    std::optional<SemanticSource> ReadSource(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"command_handles", "edition", "enum_handles", "event_handles", "handle", "imports",
                 "model_handles", "owner_capsule", "path", "schema_name", "schema_version", "semantic_type_handles",
                 "storage_codec_handles",
                 "wire_type_handles"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto edition = U32(value, "edition", path + "/edition");
        const auto schema_version = U32(value, "schema_version", path + "/schema_version");
        const auto source_path = String(value, "path", path + "/path");
        const auto schema_name = String(value, "schema_name", path + "/schema_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto imports = value.find("imports");
        const auto model_handles = value.find("model_handles");
        const auto enum_handles = value.find("enum_handles");
        const auto wire_type_handles = value.find("wire_type_handles");
        const auto storage_codec_handles = value.find("storage_codec_handles");
        const auto semantic_type_handles = value.find("semantic_type_handles");
        const auto command_handles = value.find("command_handles");
        const auto event_handles = value.find("event_handles");
        if (!handle.has_value() || !edition.has_value() || !schema_version.has_value() ||
            !source_path.has_value() || !schema_name.has_value() || !owner.has_value())
        {
            return std::nullopt;
        }
        if (!imports->is_array() || !model_handles->is_array() || !enum_handles->is_array() ||
            !wire_type_handles->is_array() ||
            !storage_codec_handles->is_array() || !semantic_type_handles->is_array() ||
            !command_handles->is_array() || !event_handles->is_array())
        {
            Add("idl.manifest_type_invalid", "Source imports and model_handles must be arrays.", path);
            return std::nullopt;
        }
        if (imports->size() > limits_.maximumImportsPerSource ||
            model_handles->size() > limits_.maximumModels ||
            enum_handles->size() > limits_.maximumEnums ||
            wire_type_handles->size() > limits_.maximumWireTypes ||
            storage_codec_handles->size() > limits_.maximumStorageCodecs ||
            semantic_type_handles->size() > limits_.maximumSemanticTypes ||
            command_handles->size() > limits_.maximumCommands ||
            event_handles->size() > limits_.maximumEvents)
        {
            Add("idl.manifest_limit_exceeded", "Source member count exceeds the configured limit.", path);
            return std::nullopt;
        }

        SemanticSource source{
            .handle = SourceHandle{*handle},
            .path = *source_path,
            .edition = *edition,
            .schemaName = *schema_name,
            .schemaVersion = *schema_version,
            .ownerCapsule = *owner,
        };
        for (std::size_t index = 0; index < imports->size(); ++index)
        {
            const auto& import = (*imports)[index];
            const std::string import_path = path + "/imports/" + std::to_string(index);
            if (!ExactObject(import, import_path + "/", {"source_handle", "source_path"}))
                return std::nullopt;
            const auto target = U32(import, "source_handle", import_path + "/source_handle");
            const auto target_path = String(import, "source_path", import_path + "/source_path");
            if (!target.has_value() || !target_path.has_value()) return std::nullopt;
            source.imports.push_back({
                .source = SourceHandle{*target},
                .sourcePath = *target_path,
                .provenance = {.sourcePath = source.path, .span = EmptySpan()},
            });
        }
        for (std::size_t index = 0; index < model_handles->size(); ++index)
        {
            const auto handle_value = Unsigned((*model_handles)[index],
                path + "/model_handles/" + std::to_string(index));
            if (!handle_value.has_value() || *handle_value > std::numeric_limits<std::uint32_t>::max())
            {
                if (handle_value.has_value())
                    Add("idl.manifest_integer_out_of_range", "Model handle must fit in u32.", path);
                return std::nullopt;
            }
            source.models.push_back(ModelHandle{static_cast<std::uint32_t>(*handle_value)});
        }
        for (std::size_t index = 0; index < enum_handles->size(); ++index)
        {
            const auto handle_value = Unsigned((*enum_handles)[index],
                path + "/enum_handles/" + std::to_string(index));
            if (!handle_value.has_value() || *handle_value > std::numeric_limits<std::uint32_t>::max())
            {
                if (handle_value.has_value())
                    Add("idl.manifest_integer_out_of_range", "Enum handle must fit in u32.", path);
                return std::nullopt;
            }
            source.enums.push_back(EnumHandle{static_cast<std::uint32_t>(*handle_value)});
        }
        const auto read_handles = [this,
                                   &path](const Json& values, std::string_view member, auto make_handle, auto& output) {
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                const auto value_at =
                    Unsigned(values[index], path + "/" + std::string(member) + "/" + std::to_string(index));
                if (!value_at.has_value() || *value_at > std::numeric_limits<std::uint32_t>::max())
                {
                    if (value_at.has_value())
                        Add("idl.manifest_integer_out_of_range", "Type handle must fit in u32.", path);
                    return false;
                }
                output.push_back(make_handle(static_cast<std::uint32_t>(*value_at)));
            }
            return true;
        };
        if (!read_handles(
                *wire_type_handles,
                "wire_type_handles",
                [](std::uint32_t value_at) { return WireTypeHandle{value_at}; },
                source.wireTypes
            ) ||
            !read_handles(
                *storage_codec_handles,
                "storage_codec_handles",
                [](std::uint32_t value_at) { return StorageCodecHandle{value_at}; },
                source.storageCodecs
            ) ||
            !read_handles(
                *semantic_type_handles,
                "semantic_type_handles",
                [](std::uint32_t value_at) { return SemanticTypeHandle{value_at}; },
                source.semanticTypes
            ) ||
            !read_handles(
                *command_handles,
                "command_handles",
                [](std::uint32_t value_at) { return CommandHandle{value_at}; },
                source.commands
            ) ||
            !read_handles(
                *event_handles,
                "event_handles",
                [](std::uint32_t value_at) { return EventHandle{value_at}; },
                source.events
            ))
        {
            return std::nullopt;
        }
        return source;
    }

    std::optional<WireTypeDefinition> ReadWireType(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"bit_width",
                 "byte_order",
                 "fixed_size",
                 "handle",
                 "kind",
                 "name",
                 "owner_capsule",
                 "qualified_name",
                 "source_handle",
                 "wire_type_id"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto type_id = U32(value, "wire_type_id", path + "/wire_type_id");
        const auto bit_width = U32(value, "bit_width", path + "/bit_width");
        const auto fixed_size = U32(value, "fixed_size", path + "/fixed_size");
        const auto name = String(value, "name", path + "/name");
        const auto qualified = String(value, "qualified_name", path + "/qualified_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto order_name = String(value, "byte_order", path + "/byte_order");
        if (!handle || !source || !type_id || !bit_width || !fixed_size || !name || !qualified || !owner ||
            !kind_name || !order_name)
            return std::nullopt;

        std::optional<WirePrimitiveKind> kind;
        for (const auto candidate : {
                 WirePrimitiveKind::Bool,
                 WirePrimitiveKind::U8,
                 WirePrimitiveKind::U16,
                 WirePrimitiveKind::U32,
                 WirePrimitiveKind::U64,
                 WirePrimitiveKind::I8,
                 WirePrimitiveKind::I16,
                 WirePrimitiveKind::I32,
                 WirePrimitiveKind::I64,
                 WirePrimitiveKind::F32,
                 WirePrimitiveKind::F64,
                 WirePrimitiveKind::Utf8,
                 WirePrimitiveKind::Bytes,
                 WirePrimitiveKind::FixedBytes,
                 WirePrimitiveKind::Decimal128,
             })
        {
            if (*kind_name == WirePrimitiveKindName(candidate)) kind = candidate;
        }
        std::optional<WireByteOrder> order;
        for (const auto candidate : {WireByteOrder::None, WireByteOrder::BigEndian})
            if (*order_name == WireByteOrderName(candidate)) order = candidate;
        if (!kind || !order)
        {
            Add("idl.manifest_enum_invalid", "Unknown wire type kind or byte order.", path);
            return std::nullopt;
        }
        return WireTypeDefinition{
            .handle = WireTypeHandle{*handle},
            .source = SourceHandle{*source},
            .wireTypeId = WireTypeId{*type_id},
            .name = *name,
            .qualifiedName = *qualified,
            .ownerCapsule = *owner,
            .kind = *kind,
            .bitWidth = *bit_width,
            .fixedSize = *fixed_size,
            .byteOrder = *order,
        };
    }

    std::optional<StorageCodecDefinition> ReadStorageCodec(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"exact",
                 "fixed_size",
                 "handle",
                 "kind",
                 "name",
                 "order_preserving",
                 "owner_capsule",
                 "qualified_name",
                 "source_handle",
                 "storage_class",
                 "storage_codec_id"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto codec_id = U32(value, "storage_codec_id", path + "/storage_codec_id");
        const auto fixed_size = U32(value, "fixed_size", path + "/fixed_size");
        const auto exact = Boolean(value, "exact", path + "/exact");
        const auto order = Boolean(value, "order_preserving", path + "/order_preserving");
        const auto name = String(value, "name", path + "/name");
        const auto qualified = String(value, "qualified_name", path + "/qualified_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto class_name = String(value, "storage_class", path + "/storage_class");
        if (!handle || !source || !codec_id || !fixed_size || !exact || !order || !name || !qualified || !owner ||
            !kind_name || !class_name)
            return std::nullopt;

        std::optional<StorageCodecKind> kind;
        for (const auto candidate : {
                 StorageCodecKind::SqliteInteger,
                 StorageCodecKind::SqliteReal,
                 StorageCodecKind::SqliteText,
                 StorageCodecKind::SqliteBlob,
                 StorageCodecKind::SqliteFixedBlob,
                 StorageCodecKind::SqliteOrderedU64,
                 StorageCodecKind::SqliteOrderedDecimal128,
             })
            if (*kind_name == StorageCodecKindName(candidate)) kind = candidate;
        std::optional<SqliteStorageClass> storage_class;
        for (const auto candidate : {
                 SqliteStorageClass::Integer,
                 SqliteStorageClass::Real,
                 SqliteStorageClass::Text,
                 SqliteStorageClass::Blob,
             })
            if (*class_name == SqliteStorageClassName(candidate)) storage_class = candidate;
        if (!kind || !storage_class)
        {
            Add("idl.manifest_enum_invalid", "Unknown storage codec kind or SQLite storage class.", path);
            return std::nullopt;
        }
        return StorageCodecDefinition{
            .handle = StorageCodecHandle{*handle},
            .source = SourceHandle{*source},
            .storageCodecId = StorageCodecId{*codec_id},
            .name = *name,
            .qualifiedName = *qualified,
            .ownerCapsule = *owner,
            .kind = *kind,
            .storageClass = *storage_class,
            .fixedSize = *fixed_size,
            .exact = *exact,
            .orderPreserving = *order,
        };
    }

    std::optional<SemanticTypeDefinition> ReadSemanticType(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"boundary_mapping",
                 "default",
                 "defining_enum_handle",
                 "defining_model_handle",
                 "fixed_size",
                 "handle",
                 "kind",
                 "json_profile",
                 "name",
                 "narrowing",
                 "normalization",
                 "owner_capsule",
                 "precision",
                 "presence",
                 "qualified_name",
                 "registry_key",
                 "scale",
                 "semantic_name",
                 "semantic_type_id",
                 "source_handle",
                 "storage_codec_handle",
                 "targets",
                 "temporal_role",
                 "wire_type_handle"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto type_id = U32(value, "semantic_type_id", path + "/semantic_type_id");
        const auto wire = U32(value, "wire_type_handle", path + "/wire_type_handle");
        const auto storage = U32(value, "storage_codec_handle", path + "/storage_codec_handle");
        const auto fixed_size = U32(value, "fixed_size", path + "/fixed_size");
        const auto precision = U32(value, "precision", path + "/precision");
        const auto scale = U32(value, "scale", path + "/scale");
        const auto name = String(value, "name", path + "/name");
        const auto qualified = String(value, "qualified_name", path + "/qualified_name");
        const auto registry_key = String(value, "registry_key", path + "/registry_key");
        const auto semantic_name = String(value, "semantic_name", path + "/semantic_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto json_profile_name = String(value, "json_profile", path + "/json_profile");
        const auto boundary_mapping_name = String(value, "boundary_mapping", path + "/boundary_mapping");
        const auto normalization_name = String(value, "normalization", path + "/normalization");
        const auto presence_name = String(value, "presence", path + "/presence");
        const auto default_name = String(value, "default", path + "/default");
        const auto narrowing_name = String(value, "narrowing", path + "/narrowing");
        const auto temporal_role_name = String(value, "temporal_role", path + "/temporal_role");
        const auto defining = value.find("defining_model_handle");
        const auto defining_enum = value.find("defining_enum_handle");
        const auto targets = value.find("targets");
        if (!handle || !source || !type_id || !wire || !storage || !fixed_size || !precision || !scale || !name ||
            !qualified || !registry_key || !semantic_name || !owner || !kind_name ||
            !json_profile_name || !boundary_mapping_name ||
            !normalization_name || !presence_name || !default_name || !narrowing_name ||
            !temporal_role_name || !targets->is_array())
            return std::nullopt;

        std::optional<SemanticTypeKind> kind;
        for (const auto candidate : {
                 SemanticTypeKind::Bool,
                 SemanticTypeKind::U8,
                 SemanticTypeKind::U16,
                 SemanticTypeKind::U32,
                 SemanticTypeKind::U64,
                 SemanticTypeKind::I8,
                 SemanticTypeKind::I16,
                 SemanticTypeKind::I32,
                 SemanticTypeKind::I64,
                 SemanticTypeKind::F32,
                 SemanticTypeKind::F64,
                 SemanticTypeKind::Text,
                 SemanticTypeKind::Enum,
                 SemanticTypeKind::Blob,
                 SemanticTypeKind::FixedId,
                 SemanticTypeKind::Decimal,
                 SemanticTypeKind::Timestamp,
                 SemanticTypeKind::JsonText,
             })
            if (*kind_name == SemanticTypeKindName(candidate)) kind = candidate;
        std::optional<NormalizationKind> normalization;
        for (const auto candidate : {
                 NormalizationKind::None,
                 NormalizationKind::Trim,
                 NormalizationKind::Lowercase,
                 NormalizationKind::LowercaseTrim,
                 NormalizationKind::EthereumAddress,
                 NormalizationKind::Url,
             })
            if (*normalization_name == NormalizationKindName(candidate)) normalization = candidate;
        const auto json_profile = detail::ParseJsonProfile(*json_profile_name);
        const auto boundary_mapping = detail::ParseBoundaryMappingPolicy(*boundary_mapping_name);
        std::optional<TemporalRole> temporal_role;
        for (const auto candidate : {
                 TemporalRole::None,
                 TemporalRole::ValidTime,
                 TemporalRole::TransactionTime,
                 TemporalRole::OccurrenceTime,
                 TemporalRole::ObservationTime,
                 TemporalRole::PublicationTime,
                 TemporalRole::BoundaryCreationTime,
             })
            if (*temporal_role_name == TemporalRoleName(candidate)) temporal_role = candidate;
        if (!kind || !json_profile || !boundary_mapping || !normalization || !temporal_role ||
            *presence_name != "explicit" || *default_name != "none" || *narrowing_name != "reject")
        {
            Add("idl.manifest_enum_invalid", "Unknown semantic type policy enum.", path);
            return std::nullopt;
        }
        std::vector<TargetProfile> target_profiles;
        for (std::size_t index = 0; index < targets->size(); ++index)
        {
            if (!(*targets)[index].is_string())
            {
                Add("idl.manifest_type_invalid", "Semantic target profile must be a string.", path);
                return std::nullopt;
            }
            const auto target_name = (*targets)[index].get<std::string>();
            std::optional<TargetProfile> target;
            for (const auto candidate : {
                     TargetProfile::Cpp,
                     TargetProfile::Sqlite,
                     TargetProfile::CSharp,
                     TargetProfile::JavaScript,
                     TargetProfile::Fabric,
                     TargetProfile::HttpJson,
                 })
                if (target_name == TargetProfileName(candidate)) target = candidate;
            if (!target)
            {
                Add("idl.manifest_enum_invalid", "Unknown target profile.", path);
                return std::nullopt;
            }
            target_profiles.push_back(*target);
        }
        std::optional<ModelHandle> defining_model;
        if (!defining->is_null())
        {
            const auto value_at = Unsigned(*defining, path + "/defining_model_handle");
            if (!value_at || *value_at > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
            defining_model = ModelHandle{static_cast<std::uint32_t>(*value_at)};
        }
        std::optional<EnumHandle> enumeration;
        if (!defining_enum->is_null())
        {
            const auto value_at = Unsigned(*defining_enum, path + "/defining_enum_handle");
            if (!value_at || *value_at > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
            enumeration = EnumHandle{static_cast<std::uint32_t>(*value_at)};
        }
        return SemanticTypeDefinition{
            .handle = SemanticTypeHandle{*handle},
            .source = SourceHandle{*source},
            .semanticTypeId = SemanticTypeId{*type_id},
            .name = *name,
            .qualifiedName = *qualified,
            .semanticName = *semantic_name,
            .registryKey = *registry_key,
            .ownerCapsule = *owner,
            .kind = *kind,
            .wireType = WireTypeHandle{*wire},
            .storageCodec = StorageCodecHandle{*storage},
            .fixedSize = *fixed_size,
            .precision = *precision,
            .scale = *scale,
            .normalization = *normalization,
            .jsonProfile = *json_profile,
            .boundaryMapping = *boundary_mapping,
            .temporalRole = *temporal_role,
            .targets = std::move(target_profiles),
            .definingModel = defining_model,
            .definingEnum = enumeration,
        };
    }

    std::optional<SemanticEnum> ReadEnum(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"enum_id", "handle", "mutation_authority", "name", "owner_capsule", "qualified_name",
                 "semantic_type_handle", "semantic_type_id", "source_handle", "values"}
            ))
            return std::nullopt;
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto enum_id = U32(value, "enum_id", path + "/enum_id");
        const auto type_handle = U32(value, "semantic_type_handle", path + "/semantic_type_handle");
        const auto type_id = U32(value, "semantic_type_id", path + "/semantic_type_id");
        const auto name = String(value, "name", path + "/name");
        const auto qualified = String(value, "qualified_name", path + "/qualified_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto authority = String(value, "mutation_authority", path + "/mutation_authority");
        const auto values = value.find("values");
        if (!handle || !source || !enum_id || !type_handle || !type_id || !name || !qualified || !owner ||
            !authority || !values->is_array() || values->size() > limits_.maximumEnumValuesPerEnum)
            return std::nullopt;
        SemanticEnum declaration{
            .handle = EnumHandle{*handle},
            .source = SourceHandle{*source},
            .enumId = EnumId{*enum_id},
            .name = *name,
            .qualifiedName = *qualified,
            .ownerCapsule = *owner,
            .mutationAuthority = *authority,
            .semanticType = SemanticTypeHandle{*type_handle},
            .semanticTypeId = SemanticTypeId{*type_id},
        };
        for (std::size_t index = 0; index < values->size(); ++index)
        {
            const auto& entry = (*values)[index];
            const auto entry_path = path + "/values/" + std::to_string(index);
            if (!ExactObject(entry, entry_path + "/", {"catalog_identity", "name", "value_id"}))
                return std::nullopt;
            const auto value_name = String(entry, "name", entry_path + "/name");
            const auto value_id = U32(entry, "value_id", entry_path + "/value_id");
            if (!value_name || !value_id) return std::nullopt;
            SemanticEnumValue parsed_value{
                .valueId = EnumValueId{*value_id},
                .name = *value_name,
            };
            const auto identity = entry.find("catalog_identity");
            if (!identity->is_null())
            {
                auto parsed = ReadCatalogMemberIdentity(*identity, entry_path + "/catalog_identity");
                if (!parsed) return std::nullopt;
                parsed_value.catalogIdentity = std::move(*parsed);
            }
            declaration.values.push_back(std::move(parsed_value));
        }
        return declaration;
    }

    std::optional<SemanticFieldReference> ReadFieldReference(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "field_id", "handle"}))
            return std::nullopt;
        const auto handle = U32(value, "handle", path + "/handle");
        const auto field_id = U32(value, "field_id", path + "/field_id");
        const auto canonical_name = String(value, "canonical_name", path + "/canonical_name");
        if (!handle || !field_id || !canonical_name) return std::nullopt;
        return SemanticFieldReference{
            .handle = FieldHandle{*handle},
            .fieldId = FieldId{*field_id},
            .canonicalName = *canonical_name,
        };
    }

    std::optional<std::vector<SemanticFieldReference>> ReadFieldReferences(
        const Json& value,
        const std::string& path
    )
    {
        if (!value.is_array())
        {
            Add("idl.manifest_type_invalid", "Structural fields must be an array.", path);
            return std::nullopt;
        }
        if (value.size() > limits_.maximumStructuralFields)
        {
            Add("idl.manifest_limit_exceeded", "Structural field count exceeds the configured limit.", path);
            return std::nullopt;
        }
        std::vector<SemanticFieldReference> fields;
        fields.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            auto field = ReadFieldReference(value[index], path + "/" + std::to_string(index));
            if (!field) return std::nullopt;
            fields.push_back(std::move(*field));
        }
        return fields;
    }

    std::optional<SemanticKey> ReadKey(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"catalog_identity", "fields", "generated", "kind", "name"}))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto generated = Boolean(value, "generated", path + "/generated");
        const auto fields = ReadFieldReferences(*value.find("fields"), path + "/fields");
        if (!name || !kind_name || !generated || !fields) return std::nullopt;
        std::optional<KeyKind> kind;
        for (const auto candidate : {KeyKind::Primary, KeyKind::Alternate})
            if (*kind_name == KeyKindName(candidate)) kind = candidate;
        if (!kind)
        {
            Add("idl.manifest_enum_invalid", "Unknown key kind.", path + "/kind");
            return std::nullopt;
        }
        SemanticKey key{
            .name = *name,
            .kind = *kind,
            .fields = std::move(*fields),
            .generated = *generated,
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            key.catalogIdentity = std::move(*parsed);
        }
        return key;
    }

    std::optional<SemanticIndex> ReadIndex(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"catalog_identity", "fields", "generated", "name", "unique"}))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto generated = Boolean(value, "generated", path + "/generated");
        const auto unique = Boolean(value, "unique", path + "/unique");
        const auto fields = ReadFieldReferences(*value.find("fields"), path + "/fields");
        if (!name || !generated || !unique || !fields) return std::nullopt;
        SemanticIndex index{
            .name = *name,
            .fields = std::move(*fields),
            .unique = *unique,
            .generated = *generated,
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            index.catalogIdentity = std::move(*parsed);
        }
        return index;
    }

    std::optional<SemanticRelation> ReadRelation(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"cardinality", "catalog_identity", "name", "on_delete", "on_update", "source_fields", "target_fields",
                 "target_model_handle", "target_qualified_name", "target_type_id"}
            ))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto target_model = U32(value, "target_model_handle", path + "/target_model_handle");
        const auto target_type_id = U32(value, "target_type_id", path + "/target_type_id");
        const auto target_qualified = String(value, "target_qualified_name", path + "/target_qualified_name");
        const auto cardinality_name = String(value, "cardinality", path + "/cardinality");
        const auto on_delete_name = String(value, "on_delete", path + "/on_delete");
        const auto on_update_name = String(value, "on_update", path + "/on_update");
        const auto source_fields = ReadFieldReferences(*value.find("source_fields"), path + "/source_fields");
        const auto target_fields = ReadFieldReferences(*value.find("target_fields"), path + "/target_fields");
        if (!name || !target_model || !target_type_id || !target_qualified || !cardinality_name ||
            !on_delete_name || !on_update_name || !source_fields || !target_fields)
            return std::nullopt;
        std::optional<RelationCardinality> cardinality;
        for (const auto candidate : {
                 RelationCardinality::One,
                 RelationCardinality::OptionalOne,
                 RelationCardinality::Many,
             })
            if (*cardinality_name == RelationCardinalityName(candidate)) cardinality = candidate;
        const auto read_action = [](std::string_view action_name) -> std::optional<ReferentialAction> {
            for (const auto candidate : {
                     ReferentialAction::Restrict,
                     ReferentialAction::Cascade,
                     ReferentialAction::SetNull,
                 })
                if (action_name == ReferentialActionName(candidate)) return candidate;
            return std::nullopt;
        };
        const auto on_delete = read_action(*on_delete_name);
        const auto on_update = read_action(*on_update_name);
        if (!cardinality || !on_delete || !on_update)
        {
            Add("idl.manifest_enum_invalid", "Unknown relation cardinality or action.", path);
            return std::nullopt;
        }
        SemanticRelation relation{
            .name = *name,
            .targetModel = ModelHandle{*target_model},
            .targetTypeId = ModelTypeId{*target_type_id},
            .targetQualifiedName = *target_qualified,
            .sourceFields = std::move(*source_fields),
            .targetFields = std::move(*target_fields),
            .cardinality = *cardinality,
            .onDelete = *on_delete,
            .onUpdate = *on_update,
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            relation.catalogIdentity = std::move(*parsed);
        }
        return relation;
    }

    std::optional<SemanticJoin> ReadJoin(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"catalog_identity", "name", "source_relation", "target_relation", "through_model_handle",
                 "through_qualified_name", "through_type_id"}
            ))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto through_model = U32(value, "through_model_handle", path + "/through_model_handle");
        const auto through_type_id = U32(value, "through_type_id", path + "/through_type_id");
        const auto through_qualified = String(value, "through_qualified_name", path + "/through_qualified_name");
        const auto source_relation = String(value, "source_relation", path + "/source_relation");
        const auto target_relation = String(value, "target_relation", path + "/target_relation");
        if (!name || !through_model || !through_type_id || !through_qualified ||
            !source_relation || !target_relation)
            return std::nullopt;
        SemanticJoin join{
            .name = *name,
            .throughModel = ModelHandle{*through_model},
            .throughTypeId = ModelTypeId{*through_type_id},
            .throughQualifiedName = *through_qualified,
            .sourceRelation = *source_relation,
            .targetRelation = *target_relation,
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            join.catalogIdentity = std::move(*parsed);
        }
        return join;
    }

    std::optional<SemanticOneOf> ReadOneOf(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"catalog_identity", "discriminator", "enum_handle", "name", "variants"}))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto enumeration = U32(value, "enum_handle", path + "/enum_handle");
        const auto discriminator = ReadFieldReference(*value.find("discriminator"), path + "/discriminator");
        const auto variants = value.find("variants");
        if (!name || !enumeration || !discriminator || !variants->is_array()) return std::nullopt;
        if (variants->size() > limits_.maximumOneOfVariants)
        {
            Add("idl.manifest_limit_exceeded", "One_of variant count exceeds the configured limit.", path);
            return std::nullopt;
        }
        SemanticOneOf one_of{
            .name = *name,
            .discriminator = *discriminator,
            .enumeration = EnumHandle{*enumeration},
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            one_of.catalogIdentity = std::move(*parsed);
        }
        for (std::size_t index = 0; index < variants->size(); ++index)
        {
            const auto& variant = (*variants)[index];
            const auto variant_path = path + "/variants/" + std::to_string(index);
            if (!ExactObject(variant, variant_path + "/", {"catalog_identity", "fields", "optional_fields", "tag"}))
                return std::nullopt;
            const auto tag = String(variant, "tag", variant_path + "/tag");
            const auto fields = ReadFieldReferences(*variant.find("fields"), variant_path + "/fields");
            const auto optional_fields =
                ReadFieldReferences(*variant.find("optional_fields"), variant_path + "/optional_fields");
            if (!tag || !fields || !optional_fields) return std::nullopt;
            SemanticOneOfVariant parsed_variant{
                .tag = *tag,
                .fields = std::move(*fields),
                .optionalFields = std::move(*optional_fields),
            };
            const auto variant_identity = variant.find("catalog_identity");
            if (!variant_identity->is_null())
            {
                auto parsed = ReadCatalogMemberIdentity(*variant_identity, variant_path + "/catalog_identity");
                if (!parsed) return std::nullopt;
                parsed_variant.catalogIdentity = std::move(*parsed);
            }
            one_of.variants.push_back(std::move(parsed_variant));
        }
        return one_of;
    }

    std::optional<SemanticTemporalContract> ReadTemporalContract(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"end_field", "end_inclusive", "name", "open_end", "ordering", "start_field", "start_inclusive"}
            ))
            return std::nullopt;
        const auto name = String(value, "name", path + "/name");
        const auto ordering_name = String(value, "ordering", path + "/ordering");
        const auto open_end_name = String(value, "open_end", path + "/open_end");
        const auto start_inclusive = Boolean(value, "start_inclusive", path + "/start_inclusive");
        const auto end_inclusive = Boolean(value, "end_inclusive", path + "/end_inclusive");
        const auto start = ReadFieldReference(*value.find("start_field"), path + "/start_field");
        const auto end = ReadFieldReference(*value.find("end_field"), path + "/end_field");
        if (!name || !ordering_name || !open_end_name || !start_inclusive || !end_inclusive || !start || !end)
            return std::nullopt;
        std::optional<TemporalOrdering> ordering;
        for (const auto candidate : {TemporalOrdering::Strict, TemporalOrdering::Nondecreasing})
            if (*ordering_name == TemporalOrderingName(candidate)) ordering = candidate;
        std::optional<OpenEndPolicy> open_end;
        for (const auto candidate : {OpenEndPolicy::Forbidden, OpenEndPolicy::Allowed})
            if (*open_end_name == OpenEndPolicyName(candidate)) open_end = candidate;
        if (!ordering || !open_end)
        {
            Add("idl.manifest_enum_invalid", "Temporal ordering or open-end policy is unknown.", path);
            return std::nullopt;
        }
        return SemanticTemporalContract{
            .name = *name,
            .startField = *start,
            .endField = *end,
            .ordering = *ordering,
            .openEnd = *open_end,
            .startInclusive = *start_inclusive,
            .endInclusive = *end_inclusive,
        };
    }

    std::optional<SemanticModel> ReadModel(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"catalog_identity", "catalog_retired_names", "description", "domain", "external", "fields", "handle", "indexes", "joins", "keys", "kind",
                 "mutation_authority", "name", "name_review", "name_review_rationale", "one_ofs",
                 "owner_capsule", "qualified_name", "relations", "retention",
                 "retired_names", "role", "schema_name", "schema_version", "source_aliases", "source_handle",
                 "storage_name", "temporal_contracts", "type_id"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto type_id = U32(value, "type_id", path + "/type_id");
        const auto schema_version = U32(value, "schema_version", path + "/schema_version");
        const auto name = String(value, "name", path + "/name");
        const auto storage_name = String(value, "storage_name", path + "/storage_name");
        const auto qualified_name = String(value, "qualified_name", path + "/qualified_name");
        const auto schema_name = String(value, "schema_name", path + "/schema_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto domain = String(value, "domain", path + "/domain");
        const auto external = Boolean(value, "external", path + "/external");
        const auto description = String(value, "description", path + "/description");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto retention_name = String(value, "retention", path + "/retention");
        const auto authority = String(value, "mutation_authority", path + "/mutation_authority");
        const auto role_name = String(value, "role", path + "/role");
        const auto review_name = String(value, "name_review", path + "/name_review");
        const auto review_rationale = String(value, "name_review_rationale", path + "/name_review_rationale");
        const auto fields = value.find("fields");
        const auto keys = value.find("keys");
        const auto indexes = value.find("indexes");
        const auto relations = value.find("relations");
        const auto joins = value.find("joins");
        const auto one_ofs = value.find("one_ofs");
        const auto temporal_contracts = value.find("temporal_contracts");
        const auto aliases = value.find("source_aliases");
        const auto retired_names = value.find("retired_names");
        const auto catalog_retired_names = value.find("catalog_retired_names");
        const auto catalog_identity = value.find("catalog_identity");
        if (!handle.has_value() || !source.has_value() || !type_id.has_value() ||
            !schema_version.has_value() || !name.has_value() || !qualified_name.has_value() ||
            !schema_name.has_value() || !owner.has_value() || !storage_name.has_value() || !domain.has_value() ||
            !external.has_value() ||
            !description.has_value() || !kind_name.has_value() || !retention_name.has_value() ||
            !authority.has_value() || !role_name.has_value() || !review_name.has_value() ||
            !review_rationale.has_value())
        {
            return std::nullopt;
        }
        if (!fields->is_array() || !keys->is_array() || !indexes->is_array() || !relations->is_array() ||
            !joins->is_array() || !one_ofs->is_array() || !temporal_contracts->is_array() ||
            !aliases->is_array() || !retired_names->is_array() || !catalog_retired_names->is_array())
        {
            Add("idl.manifest_type_invalid", "Model fields must be an array.", path);
            return std::nullopt;
        }
        if (fields->size() > limits_.maximumFieldsPerModel || keys->size() > limits_.maximumKeysPerModel ||
            indexes->size() > limits_.maximumIndexesPerModel ||
            relations->size() > limits_.maximumRelationsPerModel ||
            joins->size() > limits_.maximumJoinsPerModel || one_ofs->size() > limits_.maximumOneOfsPerModel ||
            temporal_contracts->size() > limits_.maximumTemporalContractsPerModel)
        {
            Add("idl.manifest_limit_exceeded", "Model member count exceeds the configured limit.", path);
            return std::nullopt;
        }

        std::optional<ModelKind> kind;
        for (const auto candidate : {ModelKind::Entity, ModelKind::Join, ModelKind::Credential,
                                     ModelKind::Ephemeral, ModelKind::AuditLedger, ModelKind::Provenance,
                                     ModelKind::ImportLedger})
            if (*kind_name == ModelKindName(candidate)) kind = candidate;
        std::optional<RetentionPolicy> retention;
        for (const auto candidate : {RetentionPolicy::Durable, RetentionPolicy::UntilExpiry,
                                     RetentionPolicy::Immutable, RetentionPolicy::UntilPromotion})
            if (*retention_name == RetentionPolicyName(candidate)) retention = candidate;
        std::optional<ModelRole> role;
        for (const auto candidate : {ModelRole::Unspecified, ModelRole::UniversalSpine, ModelRole::DomainFacet,
                                     ModelRole::Definition, ModelRole::Revision, ModelRole::Instance,
                                     ModelRole::Occurrence, ModelRole::Manifestation, ModelRole::Representation,
                                     ModelRole::Assertion, ModelRole::Observation, ModelRole::AuthoritativeAggregate,
                                     ModelRole::Ledger, ModelRole::Registry, ModelRole::Projection,
                                     ModelRole::ImportRecord, ModelRole::Receipt, ModelRole::Membership,
                                     ModelRole::ValueObject})
            if (*role_name == ModelRoleName(candidate)) role = candidate;
        std::optional<NameReviewStatus> review;
        for (const auto candidate : {NameReviewStatus::Canonical, NameReviewStatus::ReviewRequired,
                                     NameReviewStatus::ApprovedException})
            if (*review_name == NameReviewStatusName(candidate)) review = candidate;
        if (!kind || !retention || !role || !review)
        {
            Add("idl.manifest_enum_invalid", "Unknown model governance enum.", path);
            return std::nullopt;
        }

        SemanticModel model{
            .handle = ModelHandle{*handle},
            .source = SourceHandle{*source},
            .typeId = ModelTypeId{*type_id},
            .external = *external,
            .schemaName = *schema_name,
            .schemaVersion = *schema_version,
            .name = *name,
            .storageName = *storage_name,
            .qualifiedName = *qualified_name,
            .ownerCapsule = *owner,
            .domain = *domain,
            .description = *description,
            .kind = *kind,
            .retention = *retention,
            .mutationAuthority = *authority,
            .role = *role,
            .nameReview = *review,
            .nameReviewRationale = *review_rationale,
        };
        if (!catalog_identity->is_null())
        {
            auto identity = ReadCatalogModelIdentity(*catalog_identity, path + "/catalog_identity");
            if (!identity.has_value()) return std::nullopt;
            model.catalogIdentity = std::move(*identity);
        }
        const auto append_strings = [this, &path](const Json& input, auto& output) {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                if (!input[index].is_string())
                {
                    Add("idl.manifest_type_invalid", "Model name list values must be strings.", path);
                    return false;
                }
                output.push_back(input[index].get<std::string>());
            }
            return true;
        };
        if (!append_strings(*aliases, model.sourceAliases) ||
            !append_strings(*retired_names, model.retiredNames) ||
            !append_strings(*catalog_retired_names, model.catalogRetiredNames))
            return std::nullopt;
        for (std::size_t index = 0; index < fields->size(); ++index)
        {
            auto field = ReadField((*fields)[index], path + "/fields/" + std::to_string(index));
            if (!field.has_value()) return std::nullopt;
            model.fields.push_back(std::move(*field));
        }
        for (std::size_t index = 0; index < keys->size(); ++index)
        {
            auto key = ReadKey((*keys)[index], path + "/keys/" + std::to_string(index));
            if (!key) return std::nullopt;
            model.keys.push_back(std::move(*key));
        }
        for (std::size_t index = 0; index < indexes->size(); ++index)
        {
            auto entry = ReadIndex((*indexes)[index], path + "/indexes/" + std::to_string(index));
            if (!entry) return std::nullopt;
            model.indexes.push_back(std::move(*entry));
        }
        for (std::size_t index = 0; index < relations->size(); ++index)
        {
            auto relation = ReadRelation((*relations)[index], path + "/relations/" + std::to_string(index));
            if (!relation) return std::nullopt;
            model.relations.push_back(std::move(*relation));
        }
        for (std::size_t index = 0; index < joins->size(); ++index)
        {
            auto join = ReadJoin((*joins)[index], path + "/joins/" + std::to_string(index));
            if (!join) return std::nullopt;
            model.joins.push_back(std::move(*join));
        }
        for (std::size_t index = 0; index < one_ofs->size(); ++index)
        {
            auto one_of = ReadOneOf((*one_ofs)[index], path + "/one_ofs/" + std::to_string(index));
            if (!one_of) return std::nullopt;
            model.oneOfs.push_back(std::move(*one_of));
        }
        for (std::size_t index = 0; index < temporal_contracts->size(); ++index)
        {
            auto contract = ReadTemporalContract(
                (*temporal_contracts)[index],
                path + "/temporal_contracts/" + std::to_string(index)
            );
            if (!contract) return std::nullopt;
            model.temporalContracts.push_back(std::move(*contract));
        }
        return model;
    }

    std::optional<SemanticFieldDefault> ReadFieldDefault(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"boundary_source", "catalog_value", "kind", "literal"}))
            return std::nullopt;
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto source_name = String(value, "boundary_source", path + "/boundary_source");
        const auto catalog_value = String(value, "catalog_value", path + "/catalog_value");
        const auto literal = String(value, "literal", path + "/literal");
        if (!kind_name || !source_name || !catalog_value || !literal) return std::nullopt;
        std::optional<FieldDefaultKind> kind;
        for (const auto candidate : {FieldDefaultKind::None, FieldDefaultKind::Literal, FieldDefaultKind::Boundary})
            if (*kind_name == FieldDefaultKindName(candidate)) kind = candidate;
        std::optional<BoundaryDefaultSource> source;
        for (const auto candidate : {
                 BoundaryDefaultSource::None,
                 BoundaryDefaultSource::RequestTime,
                 BoundaryDefaultSource::GeneratedId,
                 BoundaryDefaultSource::GeneratedToken,
             })
            if (*source_name == BoundaryDefaultSourceName(candidate)) source = candidate;
        if (!kind || !source ||
            (*kind == FieldDefaultKind::None &&
             (!literal->empty() || !catalog_value->empty() || *source != BoundaryDefaultSource::None)) ||
            (*kind == FieldDefaultKind::Literal &&
             (!catalog_value->empty() || *source != BoundaryDefaultSource::None)) ||
            (*kind == FieldDefaultKind::Boundary && (!literal->empty() || *source == BoundaryDefaultSource::None)))
        {
            Add("idl.manifest_semantic_invalid", "Field default shape is invalid.", path);
            return std::nullopt;
        }
        return SemanticFieldDefault{
            .kind = *kind,
            .literal = *literal,
            .boundarySource = *source,
            .catalogValue = *catalog_value,
        };
    }

    std::optional<SemanticCheck> ReadCheck(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"catalog_identity", "generated", "kind", "message", "name", "targets", "unit", "value"}))
            return std::nullopt;
        const auto generated = Boolean(value, "generated", path + "/generated");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto message = String(value, "message", path + "/message");
        const auto name = String(value, "name", path + "/name");
        const auto unit_name = String(value, "unit", path + "/unit");
        const auto check_value = String(value, "value", path + "/value");
        const auto targets = value.find("targets");
        if (!generated || !kind_name || !message || !name || !unit_name || !check_value || !targets->is_array())
            return std::nullopt;
        std::optional<CheckKind> kind;
        for (const auto candidate : {
                 CheckKind::MinimumInteger,
                 CheckKind::MaximumInteger,
                 CheckKind::MinimumReal,
                 CheckKind::MaximumReal,
                 CheckKind::MinimumLength,
                 CheckKind::MaximumLength,
                 CheckKind::FixedLength,
                 CheckKind::EnumDomain,
                 CheckKind::ValidJson,
                 CheckKind::DecimalPrecision,
                 CheckKind::DecimalScale,
             })
            if (*kind_name == CheckKindName(candidate)) kind = candidate;
        if (!kind)
        {
            Add("idl.manifest_enum_invalid", "Unknown field check kind.", path + "/kind");
            return std::nullopt;
        }
        std::optional<CheckUnit> unit;
        for (const auto candidate : {CheckUnit::None, CheckUnit::Utf8Bytes, CheckUnit::Bytes})
            if (*unit_name == CheckUnitName(candidate)) unit = candidate;
        if (!unit)
        {
            Add("idl.manifest_enum_invalid", "Unknown field check unit.", path + "/unit");
            return std::nullopt;
        }
        SemanticCheck check{
            .name = *name,
            .kind = *kind,
            .unit = *unit,
            .value = *check_value,
            .message = *message,
            .generated = *generated,
        };
        const auto identity = value.find("catalog_identity");
        if (!identity->is_null())
        {
            auto parsed = ReadCatalogMemberIdentity(*identity, path + "/catalog_identity");
            if (!parsed) return std::nullopt;
            check.catalogIdentity = std::move(*parsed);
        }
        if (targets->size() > 6)
        {
            Add("idl.manifest_limit_exceeded", "Check target count exceeds the supported profile count.", path);
            return std::nullopt;
        }
        std::set<TargetProfile> seen;
        for (std::size_t index = 0; index < targets->size(); ++index)
        {
            if (!(*targets)[index].is_string())
            {
                Add("idl.manifest_type_invalid", "Check target must be a string.", path + "/targets");
                return std::nullopt;
            }
            const auto target_name = (*targets)[index].get<std::string>();
            std::optional<TargetProfile> target;
            for (const auto candidate : {
                     TargetProfile::Cpp,
                     TargetProfile::Sqlite,
                     TargetProfile::CSharp,
                     TargetProfile::JavaScript,
                     TargetProfile::Fabric,
                     TargetProfile::HttpJson,
                 })
                if (target_name == TargetProfileName(candidate)) target = candidate;
            if (!target || !seen.insert(*target).second)
            {
                Add("idl.manifest_enum_invalid", "Check target is unknown or duplicated.", path + "/targets");
                return std::nullopt;
            }
            check.targets.push_back(*target);
        }
        return check;
    }

    std::optional<SemanticQueryPolicy> ReadQueryPolicy(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"cursor", "operators", "sortable"}))
            return std::nullopt;
        const auto cursor = Boolean(value, "cursor", path + "/cursor");
        const auto sortable = Boolean(value, "sortable", path + "/sortable");
        const auto operators = value.find("operators");
        if (!cursor || !sortable || !operators->is_array() || operators->size() > 12) return std::nullopt;
        SemanticQueryPolicy query{.sortable = *sortable, .cursor = *cursor};
        std::set<QueryOperator> seen;
        for (std::size_t index = 0; index < operators->size(); ++index)
        {
            if (!(*operators)[index].is_string())
            {
                Add("idl.manifest_type_invalid", "Query operator must be a string.", path + "/operators");
                return std::nullopt;
            }
            const auto operation_name = (*operators)[index].get<std::string>();
            std::optional<QueryOperator> operation;
            for (const auto candidate : {
                     QueryOperator::Equal,
                     QueryOperator::NotEqual,
                     QueryOperator::In,
                     QueryOperator::NotIn,
                     QueryOperator::LessThan,
                     QueryOperator::LessThanOrEqual,
                     QueryOperator::GreaterThan,
                     QueryOperator::GreaterThanOrEqual,
                     QueryOperator::Prefix,
                     QueryOperator::Suffix,
                     QueryOperator::Contains,
                     QueryOperator::IsNull,
                 })
                if (operation_name == QueryOperatorName(candidate)) operation = candidate;
            if (!operation || !seen.insert(*operation).second)
            {
                Add("idl.manifest_enum_invalid", "Query operator is unknown or duplicated.", path + "/operators");
                return std::nullopt;
            }
            query.operators.push_back(*operation);
        }
        return query;
    }

    std::optional<SemanticModelReference> ReadModelReference(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "model_handle", "type_id"}))
            return std::nullopt;
        const auto handle = U32(value, "model_handle", path + "/model_handle");
        const auto type_id = U32(value, "type_id", path + "/type_id");
        const auto canonical_name = String(value, "canonical_name", path + "/canonical_name");
        if (!handle.has_value() || !type_id.has_value() || !canonical_name.has_value())
            return std::nullopt;
        return SemanticModelReference{
            .model = ModelHandle{*handle},
            .typeId = ModelTypeId{*type_id},
            .canonicalName = *canonical_name,
        };
    }

    std::optional<std::vector<OperationTargetProfile>> ReadOperationTargets(
        const Json& value,
        const std::string& path
    )
    {
        if (!value.is_array() || value.empty() || value.size() > 3)
        {
            Add("idl.manifest_type_invalid", "Operation targets must be a nonempty bounded array.", path);
            return std::nullopt;
        }
        std::vector<OperationTargetProfile> targets;
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (!value[index].is_string())
            {
                Add("idl.manifest_type_invalid", "Operation target must be a string.", path);
                return std::nullopt;
            }
            const auto name = value[index].get<std::string>();
            std::optional<OperationTargetProfile> target;
            for (const auto candidate : {
                     OperationTargetProfile::Cpp,
                     OperationTargetProfile::Fabric,
                     OperationTargetProfile::HttpJson,
                 })
                if (name == OperationTargetProfileName(candidate)) target = candidate;
            if (!target.has_value())
            {
                Add("idl.manifest_enum_invalid", "Unknown operation target profile.", path);
                return std::nullopt;
            }
            targets.push_back(*target);
        }
        return targets;
    }

    std::optional<FabricOperationProfile> ReadFabricProfile(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"direct_eligible", "lane", "maximum_encoded_bytes"}))
            return std::nullopt;
        const auto direct = Boolean(value, "direct_eligible", path + "/direct_eligible");
        const auto lane_name = String(value, "lane", path + "/lane");
        const auto maximum = U32(value, "maximum_encoded_bytes", path + "/maximum_encoded_bytes");
        if (!direct || !lane_name || !maximum) return std::nullopt;
        std::optional<FabricLane> lane;
        for (const auto candidate : {
                 FabricLane::ControlReliable,
                 FabricLane::StateReliable,
                 FabricLane::BulkReliable,
                 FabricLane::RealtimeUnreliable,
                 FabricLane::Compatibility,
             })
            if (*lane_name == FabricLaneName(candidate)) lane = candidate;
        if (!lane.has_value())
        {
            Add("idl.manifest_enum_invalid", "Unknown Fabric lane.", path);
            return std::nullopt;
        }
        return FabricOperationProfile{
            .lane = *lane,
            .maximumEncodedBytes = *maximum,
            .directEligible = *direct,
        };
    }

    std::optional<SemanticCommand> ReadCommand(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"command_id", "errors", "fabric", "handle", "idempotency", "name", "owner_capsule",
                 "qualified_name", "request", "response", "source_handle", "streaming", "targets"}
            ))
            return std::nullopt;
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto command_id = U32(value, "command_id", path + "/command_id");
        const auto name = String(value, "name", path + "/name");
        const auto qualified_name = String(value, "qualified_name", path + "/qualified_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto idempotency_name = String(value, "idempotency", path + "/idempotency");
        const auto streaming_name = String(value, "streaming", path + "/streaming");
        const auto request = value.find("request");
        const auto response = value.find("response");
        const auto errors = value.find("errors");
        const auto targets_value = value.find("targets");
        const auto fabric_value = value.find("fabric");
        if (!handle || !source || !command_id || !name || !qualified_name || !owner ||
            !idempotency_name || !streaming_name || !errors->is_array())
            return std::nullopt;
        auto targets = ReadOperationTargets(*targets_value, path + "/targets");
        if (!targets.has_value()) return std::nullopt;
        std::optional<FabricOperationProfile> fabric;
        if (!fabric_value->is_null())
        {
            fabric = ReadFabricProfile(*fabric_value, path + "/fabric");
            if (!fabric.has_value()) return std::nullopt;
        }
        if (errors->size() > limits_.maximumModels)
        {
            Add("idl.manifest_limit_exceeded", "Command error model count exceeds the configured limit.", path);
            return std::nullopt;
        }
        std::optional<CommandIdempotency> idempotency;
        for (const auto candidate : {
                 CommandIdempotency::None,
                 CommandIdempotency::Idempotent,
                 CommandIdempotency::Safe,
             })
            if (*idempotency_name == CommandIdempotencyName(candidate)) idempotency = candidate;
        std::optional<CommandStreaming> streaming;
        for (const auto candidate : {
                 CommandStreaming::Unary,
                 CommandStreaming::ServerStream,
                 CommandStreaming::ClientStream,
                 CommandStreaming::Bidirectional,
             })
            if (*streaming_name == CommandStreamingName(candidate)) streaming = candidate;
        if (!idempotency || !streaming)
        {
            Add("idl.manifest_enum_invalid", "Unknown command idempotency or streaming policy.", path);
            return std::nullopt;
        }

        SemanticCommand command{
            .handle = CommandHandle{*handle},
            .source = SourceHandle{*source},
            .commandId = CommandId{*command_id},
            .name = *name,
            .qualifiedName = *qualified_name,
            .ownerCapsule = *owner,
            .idempotency = *idempotency,
            .streaming = *streaming,
            .targets = std::move(*targets),
            .fabric = fabric,
        };
        if (!request->is_null())
        {
            auto reference = ReadModelReference(*request, path + "/request");
            if (!reference.has_value()) return std::nullopt;
            command.request = std::move(*reference);
        }
        if (!response->is_null())
        {
            auto reference = ReadModelReference(*response, path + "/response");
            if (!reference.has_value()) return std::nullopt;
            command.response = std::move(*reference);
        }
        for (std::size_t index = 0; index < errors->size(); ++index)
        {
            auto reference = ReadModelReference((*errors)[index], path + "/errors/" + std::to_string(index));
            if (!reference.has_value()) return std::nullopt;
            command.errors.push_back(std::move(*reference));
        }
        return command;
    }

    std::optional<SemanticEvent> ReadEvent(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"delivery", "event_id", "fabric", "handle", "name", "ordering", "owner_capsule", "payload",
                 "qualified_name", "replay", "source_handle", "targets"}
            ))
            return std::nullopt;
        const auto handle = U32(value, "handle", path + "/handle");
        const auto source = U32(value, "source_handle", path + "/source_handle");
        const auto event_id = U32(value, "event_id", path + "/event_id");
        const auto name = String(value, "name", path + "/name");
        const auto qualified_name = String(value, "qualified_name", path + "/qualified_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        const auto delivery_name = String(value, "delivery", path + "/delivery");
        const auto ordering_name = String(value, "ordering", path + "/ordering");
        const auto replay_name = String(value, "replay", path + "/replay");
        const auto targets_value = value.find("targets");
        const auto fabric_value = value.find("fabric");
        if (!handle || !source || !event_id || !name || !qualified_name || !owner ||
            !delivery_name || !ordering_name || !replay_name)
            return std::nullopt;
        auto payload = ReadModelReference(value.at("payload"), path + "/payload");
        if (!payload.has_value()) return std::nullopt;
        auto targets = ReadOperationTargets(*targets_value, path + "/targets");
        if (!targets.has_value()) return std::nullopt;
        std::optional<FabricOperationProfile> fabric;
        if (!fabric_value->is_null())
        {
            fabric = ReadFabricProfile(*fabric_value, path + "/fabric");
            if (!fabric.has_value()) return std::nullopt;
        }
        std::optional<EventDelivery> delivery;
        for (const auto candidate : {EventDelivery::BestEffort, EventDelivery::AtLeastOnce})
            if (*delivery_name == EventDeliveryName(candidate)) delivery = candidate;
        std::optional<EventOrdering> ordering;
        for (const auto candidate : {
                 EventOrdering::None,
                 EventOrdering::Source,
                 EventOrdering::Aggregate,
                 EventOrdering::Global,
             })
            if (*ordering_name == EventOrderingName(candidate)) ordering = candidate;
        std::optional<EventReplay> replay;
        for (const auto candidate : {EventReplay::None, EventReplay::Retained, EventReplay::Resumable})
            if (*replay_name == EventReplayName(candidate)) replay = candidate;
        if (!delivery || !ordering || !replay)
        {
            Add("idl.manifest_enum_invalid", "Unknown event delivery, ordering, or replay policy.", path);
            return std::nullopt;
        }
        return SemanticEvent{
            .handle = EventHandle{*handle},
            .source = SourceHandle{*source},
            .eventId = EventId{*event_id},
            .name = *name,
            .qualifiedName = *qualified_name,
            .ownerCapsule = *owner,
            .payload = std::move(*payload),
            .delivery = *delivery,
            .ordering = *ordering,
            .replay = *replay,
            .targets = std::move(*targets),
            .fabric = fabric,
        };
    }

    std::optional<SemanticFieldRepresentation> ReadFieldRepresentation(
        const Json& value,
        const std::string& path
    )
    {
        if (!ExactObject(
                value,
                path + "/",
                {"field_kind", "normalization", "precision", "scale", "storage_codec_handle",
                 "storage_codec_id", "storage_size", "wire_size", "wire_type_handle", "wire_type_id"}
            ))
        {
            return std::nullopt;
        }
        const auto field_kind_name = String(value, "field_kind", path + "/field_kind");
        const auto normalization_name = String(value, "normalization", path + "/normalization");
        const auto precision = U32(value, "precision", path + "/precision");
        const auto scale = U32(value, "scale", path + "/scale");
        const auto storage_handle = U32(value, "storage_codec_handle", path + "/storage_codec_handle");
        const auto storage_id = U32(value, "storage_codec_id", path + "/storage_codec_id");
        const auto storage_size = U32(value, "storage_size", path + "/storage_size");
        const auto wire_size = U32(value, "wire_size", path + "/wire_size");
        const auto wire_handle = U32(value, "wire_type_handle", path + "/wire_type_handle");
        const auto wire_id = U32(value, "wire_type_id", path + "/wire_type_id");
        if (!field_kind_name || !normalization_name || !precision || !scale || !storage_handle ||
            !storage_id || !storage_size || !wire_size || !wire_handle || !wire_id)
        {
            return std::nullopt;
        }
        std::optional<FieldKind> field_kind;
        for (const auto candidate : {
                 FieldKind::ObjectId, FieldKind::InternalId, FieldKind::Text, FieldKind::Slug,
                 FieldKind::DisplayName, FieldKind::Url, FieldKind::Email, FieldKind::Enum,
                 FieldKind::Bool, FieldKind::TimestampMs, FieldKind::JsonText, FieldKind::Secret,
                 FieldKind::EthereumAddress, FieldKind::HashHex, FieldKind::TokenHash,
                 FieldKind::KeyReference, FieldKind::Integer, FieldKind::Real, FieldKind::Decimal,
                 FieldKind::Blob,
             })
            if (*field_kind_name == FieldKindName(candidate)) field_kind = candidate;
        std::optional<NormalizationKind> normalization;
        for (const auto candidate : {
                 NormalizationKind::None, NormalizationKind::Trim, NormalizationKind::Lowercase,
                 NormalizationKind::LowercaseTrim, NormalizationKind::EthereumAddress,
                 NormalizationKind::Url,
             })
            if (*normalization_name == NormalizationKindName(candidate)) normalization = candidate;
        if (!field_kind || !normalization)
        {
            Add("idl.manifest_enum_invalid", "Field representation enum is unknown.", path);
            return std::nullopt;
        }
        return SemanticFieldRepresentation{
            .fieldKind = *field_kind,
            .wireType = WireTypeHandle{*wire_handle},
            .wireTypeId = WireTypeId{*wire_id},
            .wireSize = *wire_size,
            .storageCodec = StorageCodecHandle{*storage_handle},
            .storageCodecId = StorageCodecId{*storage_id},
            .storageSize = *storage_size,
            .precision = *precision,
            .scale = *scale,
            .normalization = *normalization,
        };
    }

    std::optional<SemanticField> ReadField(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"allowed_enum_value_ids", "cardinality", "catalog_identity", "checks", "cpp_name", "csharp_name", "default", "field_id", "generated_primary_id", "handle", "immutable", "import_transform",
                 "maximum_items", "name", "primary_key", "query", "redacted", "representation", "semantic_type",
                 "serialization", "source_field", "source_provenance", "storage_name", "temporal_role", "write_once"}
            ))
        {
            return std::nullopt;
        }
        const auto handle = U32(value, "handle", path + "/handle");
        const auto field_id = U32(value, "field_id", path + "/field_id");
        const auto name = String(value, "name", path + "/name");
        const auto cpp_name = value.find("cpp_name");
        const auto csharp_name = value.find("csharp_name");
        const auto cardinality_name = String(value, "cardinality", path + "/cardinality");
        const auto storage_name = String(value, "storage_name", path + "/storage_name");
        const auto import_transform = String(value, "import_transform", path + "/import_transform");
        const auto source_kind_name = String(value, "source_provenance", path + "/source_provenance");
        const auto serialization_name = String(value, "serialization", path + "/serialization");
        const auto temporal_role_name = String(value, "temporal_role", path + "/temporal_role");
        const auto redacted = Boolean(value, "redacted", path + "/redacted");
        const auto immutable = Boolean(value, "immutable", path + "/immutable");
        const auto write_once = Boolean(value, "write_once", path + "/write_once");
        const auto primary = Boolean(value, "primary_key", path + "/primary_key");
        const auto generated_primary_id = Boolean(value, "generated_primary_id", path + "/generated_primary_id");
        const auto allowed_enum_value_ids = value.find("allowed_enum_value_ids");
        const auto type = value.find("semantic_type");
        const auto representation = value.find("representation");
        const auto checks = value.find("checks");
        const auto source_field = value.find("source_field");
        const auto maximum_items = value.find("maximum_items");
        const auto catalog_identity = value.find("catalog_identity");
        if (!handle.has_value() || !field_id.has_value() || !name.has_value() ||
            !cardinality_name.has_value() ||
            !storage_name.has_value() || !import_transform.has_value() || !source_kind_name.has_value() ||
            !redacted.has_value() || !immutable.has_value() ||
            !write_once.has_value() || !primary.has_value() || !generated_primary_id.has_value() ||
            !serialization_name.has_value() || !temporal_role_name.has_value() ||
            cpp_name == value.end() ||
            csharp_name == value.end() ||
            allowed_enum_value_ids == value.end() || !allowed_enum_value_ids->is_array() ||
            allowed_enum_value_ids->size() > limits_.maximumEnumValuesPerEnum ||
            !checks->is_array() || checks->size() > limits_.maximumChecksPerField)
        {
            return std::nullopt;
        }
        auto semantic_type = ReadType(*type, path + "/semantic_type");
        auto field_representation = ReadFieldRepresentation(*representation, path + "/representation");
        if (!semantic_type.has_value() || !field_representation.has_value()) return std::nullopt;

        std::optional<FieldCardinality> cardinality;
        for (const auto candidate : {
                 FieldCardinality::RequiredOne,
                 FieldCardinality::OptionalOne,
                 FieldCardinality::Repeated,
             })
        {
            if (*cardinality_name == FieldCardinalityName(candidate)) cardinality = candidate;
        }
        if (!cardinality.has_value())
        {
            Add("idl.manifest_enum_invalid", "Unknown field cardinality.", path + "/cardinality");
            return std::nullopt;
        }

        auto default_value = ReadFieldDefault(*value.find("default"), path + "/default");
        auto query = ReadQueryPolicy(*value.find("query"), path + "/query");
        if (!default_value || !query) return std::nullopt;
        std::optional<std::string> source_field_value;
        if (!source_field->is_null())
        {
            if (!source_field->is_string())
            {
                Add("idl.manifest_type_invalid", "Field source_field must be a string or null.", path);
                return std::nullopt;
            }
            source_field_value = source_field->get<std::string>();
        }
        std::optional<std::string> cpp_name_value;
        if (!cpp_name->is_null())
        {
            if (!cpp_name->is_string())
            {
                Add("idl.manifest_type_invalid", "Field cpp_name must be a string or null.", path);
                return std::nullopt;
            }
            cpp_name_value = cpp_name->get<std::string>();
        }
        std::optional<std::string> csharp_name_value;
        if (!csharp_name->is_null())
        {
            if (!csharp_name->is_string())
            {
                Add("idl.manifest_type_invalid", "Field csharp_name must be a string or null.", path);
                return std::nullopt;
            }
            csharp_name_value = csharp_name->get<std::string>();
        }
        std::optional<FieldSourceKind> source_kind;
        for (const auto candidate : {FieldSourceKind::Canonical, FieldSourceKind::Imported,
                                     FieldSourceKind::Derived, FieldSourceKind::BoundaryGenerated})
            if (*source_kind_name == FieldSourceKindName(candidate)) source_kind = candidate;
        if (!source_kind)
        {
            Add("idl.manifest_enum_invalid", "Unknown field source provenance.", path);
            return std::nullopt;
        }
        std::optional<SerializationPolicy> serialization;
        for (const auto candidate : {SerializationPolicy::Include, SerializationPolicy::Omit})
            if (*serialization_name == SerializationPolicyName(candidate)) serialization = candidate;
        if (!serialization)
        {
            Add("idl.manifest_enum_invalid", "Field serialization policy is unknown.", path);
            return std::nullopt;
        }
        std::optional<TemporalRole> temporal_role;
        for (const auto candidate : {
                 TemporalRole::None, TemporalRole::ValidTime, TemporalRole::TransactionTime,
                 TemporalRole::OccurrenceTime, TemporalRole::ObservationTime,
                 TemporalRole::PublicationTime, TemporalRole::BoundaryCreationTime,
             })
            if (*temporal_role_name == TemporalRoleName(candidate)) temporal_role = candidate;
        if (!temporal_role)
        {
            Add("idl.manifest_enum_invalid", "Field temporal role is unknown.", path);
            return std::nullopt;
        }
        std::optional<std::uint32_t> maximum_items_value;
        if (!maximum_items->is_null())
        {
            const auto value_at = Unsigned(*maximum_items, path + "/maximum_items");
            if (!value_at.has_value()) return std::nullopt;
            if (*value_at > std::numeric_limits<std::uint32_t>::max())
            {
                Add(
                    "idl.manifest_integer_out_of_range",
                    "Field maximum_items must fit in u32.",
                    path + "/maximum_items"
                );
                return std::nullopt;
            }
            maximum_items_value = static_cast<std::uint32_t>(*value_at);
        }
        SemanticField field{
            .handle = FieldHandle{*handle},
            .fieldId = FieldId{*field_id},
            .name = *name,
            .cppName = std::move(cpp_name_value),
            .csharpName = std::move(csharp_name_value),
            .storageName = *storage_name,
            .type = std::move(*semantic_type),
            .representation = std::move(*field_representation),
            .temporalRole = *temporal_role,
            .defaultValue = std::move(*default_value),
            .query = std::move(*query),
            .sourceField = std::move(source_field_value),
            .importTransform = *import_transform,
            .sourceKind = *source_kind,
            .serialization = *serialization,
            .cardinality = *cardinality,
            .maximumItems = maximum_items_value,
            .required = *cardinality == FieldCardinality::RequiredOne,
            .redacted = *redacted,
            .immutable = *immutable,
            .writeOnce = *write_once,
            .primaryKey = *primary,
            .generatedPrimaryId = *generated_primary_id,
            .equalityComparable = true,
        };
        std::set<std::uint32_t> allowed_ids;
        for (std::size_t index = 0; index < allowed_enum_value_ids->size(); ++index)
        {
            const auto id = Unsigned(
                (*allowed_enum_value_ids)[index],
                path + "/allowed_enum_value_ids/" + std::to_string(index)
            );
            if (!id.has_value() || *id == 0 || *id > std::numeric_limits<std::uint32_t>::max() ||
                !allowed_ids.insert(static_cast<std::uint32_t>(*id)).second)
                return std::nullopt;
            field.allowedEnumValueIds.push_back(EnumValueId{static_cast<std::uint32_t>(*id)});
        }
        if (!catalog_identity->is_null())
        {
            auto identity = ReadCatalogFieldIdentity(*catalog_identity, path + "/catalog_identity");
            if (!identity.has_value()) return std::nullopt;
            field.catalogIdentity = std::move(*identity);
        }
        for (std::size_t index = 0; index < checks->size(); ++index)
        {
            auto check = ReadCheck((*checks)[index], path + "/checks/" + std::to_string(index));
            if (!check) return std::nullopt;
            field.checks.push_back(std::move(*check));
        }
        return field;
    }

    std::optional<SemanticTypeReference> ReadType(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "defining_enum_handle", "defining_model_handle", "kind", "semantic_type_handle", "semantic_type_id"}
            ))
            return std::nullopt;
        const auto canonical_name = String(value, "canonical_name", path + "/canonical_name");
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto type_handle = U32(value, "semantic_type_handle", path + "/semantic_type_handle");
        const auto type_id = U32(value, "semantic_type_id", path + "/semantic_type_id");
        const auto defining = value.find("defining_model_handle");
        const auto defining_enum = value.find("defining_enum_handle");
        if (!canonical_name.has_value() || !kind_name.has_value() || !type_handle || !type_id) return std::nullopt;

        std::optional<

        SemanticTypeKind> kind;
        for (const auto candidate : {
                 SemanticTypeKind::Bool,
                 SemanticTypeKind::U8,
                 SemanticTypeKind::U16,
                 SemanticTypeKind::U32,
                 SemanticTypeKind::U64,
                 SemanticTypeKind::I8,
                 SemanticTypeKind::I16,
                 SemanticTypeKind::I32,
                 SemanticTypeKind::I64,
                 SemanticTypeKind::F32,
                 SemanticTypeKind::F64,
                 SemanticTypeKind::Text,
                 SemanticTypeKind::Enum,
                 SemanticTypeKind::Blob,
                 SemanticTypeKind::FixedId,
                 SemanticTypeKind::Decimal,
                 SemanticTypeKind::Timestamp,
                 SemanticTypeKind::JsonText,
             })
        if (*kind_name == SemanticTypeKindName(candidate)) kind = candidate; if (!kind)
        {
            Add("idl.manifest_enum_invalid", "Unknown semantic type kind.", path + "/kind");
            return std::nullopt;
        }
        std::optional<ModelHandle> defining_model;
        if (!defining->is_null())
        {
            const auto value_at = Unsigned(*defining, path + "/defining_model_handle");
            if (!value_at.has_value() || *value_at > std::numeric_limits<std::uint32_t>::max())
            {
                if (value_at.has_value())
                    Add("idl.manifest_integer_out_of_range", "Defining model handle must fit in u32.", path);
                return std::nullopt;
            }
            defining_model = ModelHandle{static_cast<std::uint32_t>(*value_at)};
        }
        std::optional<EnumHandle> enumeration;
        if (!defining_enum->is_null())
        {
            const auto value_at = Unsigned(*defining_enum, path + "/defining_enum_handle");
            if (!value_at.has_value() || *value_at > std::numeric_limits<std::uint32_t>::max())
            {
                if (value_at.has_value())
                    Add("idl.manifest_integer_out_of_range", "Defining enum handle must fit in u32.", path);
                return std::nullopt;
            }
            enumeration = EnumHandle{static_cast<std::uint32_t>(*value_at)};
        }
        return SemanticTypeReference{
            .kind = * kind,
            .semanticType = SemanticTypeHandle{*type_handle},
            .semanticTypeId = SemanticTypeId{*type_id},
            .canonicalName = *canonical_name,
            .definingModel = defining_model,
            .definingEnum = enumeration,
        };
    }

    static const SemanticField* FindReferencedField(
        const SemanticModel& model,
        const SemanticFieldReference& reference
    )
    {
        const auto found = std::find_if(model.fields.begin(), model.fields.end(), [&reference](const auto& field) {
            return field.handle == reference.handle;
        });
        if (found == model.fields.end() || found->fieldId != reference.fieldId ||
            found->name != reference.canonicalName)
            return nullptr;
        return &*found;
    }

    static bool ReferenceFieldsEqual(
        const std::vector<SemanticFieldReference>& lhs,
        const std::vector<SemanticFieldReference>& rhs
    )
    {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t index = 0; index < lhs.size(); ++index)
            if (lhs[index].handle != rhs[index].handle) return false;
        return true;
    }

    static bool ReferenceFieldsPrefix(
        const std::vector<SemanticFieldReference>& prefix,
        const std::vector<SemanticFieldReference>& values
    )
    {
        if (prefix.size() > values.size()) return false;
        for (std::size_t index = 0; index < prefix.size(); ++index)
            if (prefix[index].handle != values[index].handle) return false;
        return true;
    }

    static bool HasStructuralPrefix(
        const SemanticModel& model,
        const std::vector<SemanticFieldReference>& fields,
        bool include_generated = true
    )
    {
        return std::any_of(model.keys.begin(), model.keys.end(), [&fields](const auto& key) {
                   return ReferenceFieldsPrefix(fields, key.fields);
               }) ||
            std::any_of(model.indexes.begin(), model.indexes.end(), [&fields, include_generated](const auto& index) {
                return (include_generated || !index.generated) && ReferenceFieldsPrefix(fields, index.fields);
            });
    }

    static bool IsStructuralUnique(
        const SemanticModel& model,
        const std::vector<SemanticFieldReference>& fields
    )
    {
        return std::any_of(model.keys.begin(), model.keys.end(), [&fields](const auto& key) {
                   return ReferenceFieldsEqual(fields, key.fields);
               }) ||
            std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const auto& index) {
                return index.unique && ReferenceFieldsEqual(fields, index.fields);
            });
    }

    bool ValidateReferenceList(
        const SemanticModel& model,
        const std::vector<SemanticFieldReference>& fields,
        bool allow_empty
    )
    {
        if ((!allow_empty && fields.empty()) || fields.size() > limits_.maximumStructuralFields)
            return false;
        std::set<FieldHandle> handles;
        for (const auto& field : fields)
            if (FindReferencedField(model, field) == nullptr || !handles.insert(field.handle).second)
                return false;
        return true;
    }

    bool ValidateStructures(SemanticIr& ir)
    {
        for (auto& model : ir.models)
        {
            const auto source_path = ir.sources[model.source.value].path;
            if (model.external && (
                    std::any_of(model.fields.begin(), model.fields.end(), [](const auto& field) {
                        return field.generatedPrimaryId;
                    }) ||
                    !model.relations.empty() || !model.joins.empty() || !model.oneOfs.empty() ||
                    !model.temporalContracts.empty()
                ))
                return SemanticInvalid("External model behavior or generated identity is invalid.");
            std::string previous_name;
            std::size_t primary_count = 0;
            const auto primary_field_count = static_cast<std::size_t>(std::count_if(
                model.fields.begin(), model.fields.end(), [](const auto& field) { return field.primaryKey; }
            ));
            const auto generated_primary_field = std::find_if(
                model.fields.begin(), model.fields.end(), [](const auto& field) {
                    return field.generatedPrimaryId;
                }
            );
            for (auto& key : model.keys)
            {
                if (!IsCanonicalSnakeCase(key.name) || (!previous_name.empty() && previous_name >= key.name) ||
                    !ValidateReferenceList(model, key.fields, false) || KeyKindName(key.kind) == "unknown")
                    return SemanticInvalid("Key identity, field, or ordering invariant is invalid.");
                previous_name = key.name;
                if (key.kind == KeyKind::Primary)
                {
                    ++primary_count;
                    if (key.fields.size() != primary_field_count ||
                        std::any_of(key.fields.begin(), key.fields.end(), [&model](const auto& reference) {
                            const auto* field = FindReferencedField(model, reference);
                            return field == nullptr || !field->primaryKey;
                        }))
                        return SemanticInvalid("Primary-key field membership does not match the structural key.");
                    if (generated_primary_field != model.fields.end() &&
                        (key.fields.size() != 1 || key.fields.front().handle != generated_primary_field->handle))
                        return SemanticInvalid("Generated primary ID is not the exact structural primary key.");
                }
                if (key.generated &&
                    (key.kind != KeyKind::Primary || key.name != model.name + "_pk" ||
                     generated_primary_field == model.fields.end()))
                    return SemanticInvalid("Generated key shape is invalid.");
                key.provenance.sourcePath = source_path;
            }
            if (primary_count != 1)
                return SemanticInvalid("Every semantic model requires exactly one structural primary key.");

            previous_name.clear();
            for (auto& index : model.indexes)
            {
                if (!IsCanonicalSnakeCase(index.name) || (!previous_name.empty() && previous_name >= index.name) ||
                    !ValidateReferenceList(model, index.fields, false) || (index.generated && index.unique))
                    return SemanticInvalid("Index identity, field, generation, or ordering invariant is invalid.");
                previous_name = index.name;
                index.provenance.sourcePath = source_path;
            }
            for (const auto& field : model.fields)
            {
                const auto& type = ir.semanticTypes[field.type.semanticType.value];
                const auto& storage = ir.storageCodecs[type.storageCodec.value];
                if ((field.query.sortable || field.query.cursor) &&
                    (!storage.orderPreserving || !HasStructuralPrefix(model, {{
                        .handle = field.handle,
                        .fieldId = field.fieldId,
                        .canonicalName = field.name,
                    }})))
                    return SemanticInvalid("Field query sort or cursor index-prefix invariant is invalid.");
                for (const auto operation : field.query.operators)
                {
                    if (QueryOperatorName(operation) == "unknown" ||
                        (operation == QueryOperator::IsNull &&
                         field.cardinality != FieldCardinality::OptionalOne) ||
                        (type.kind == SemanticTypeKind::JsonText && operation != QueryOperator::IsNull) ||
                        ((operation == QueryOperator::Prefix || operation == QueryOperator::Suffix ||
                          operation == QueryOperator::Contains) && type.kind != SemanticTypeKind::Text) ||
                        ((operation == QueryOperator::LessThan || operation == QueryOperator::LessThanOrEqual ||
                          operation == QueryOperator::GreaterThan ||
                          operation == QueryOperator::GreaterThanOrEqual) &&
                         (type.kind == SemanticTypeKind::Bool || type.kind == SemanticTypeKind::FixedId ||
                          type.kind == SemanticTypeKind::Blob)))
                        return SemanticInvalid("Field query operator is incompatible with its semantic type.");
                }
            }

            previous_name.clear();
            std::set<std::string> include_names;
            for (auto& relation : model.relations)
            {
                if (!IsCanonicalSnakeCase(relation.name) ||
                    (!previous_name.empty() && previous_name >= relation.name) ||
                    !include_names.insert(relation.name).second || relation.targetModel.value >= ir.models.size() ||
                    RelationCardinalityName(relation.cardinality) == "unknown" ||
                    ReferentialActionName(relation.onDelete) == "unknown" ||
                    ReferentialActionName(relation.onUpdate) == "unknown" ||
                    !ValidateReferenceList(model, relation.sourceFields, false))
                    return SemanticInvalid("Relation identity, source fields, policy, or ordering invariant is invalid.");
                previous_name = relation.name;
                const auto& target = ir.models[relation.targetModel.value];
                if (relation.targetTypeId != target.typeId || relation.targetQualifiedName != target.qualifiedName ||
                    !ValidateReferenceList(target, relation.targetFields, false) ||
                    relation.sourceFields.size() != relation.targetFields.size())
                    return SemanticInvalid("Relation target identity, fields, or arity invariant is invalid.");
                for (std::size_t field_index = 0; field_index < relation.sourceFields.size(); ++field_index)
                {
                    const auto* source_field = FindReferencedField(model, relation.sourceFields[field_index]);
                    const auto* target_field = FindReferencedField(target, relation.targetFields[field_index]);
                    if (source_field == nullptr || target_field == nullptr ||
                        source_field->type.semanticTypeId != target_field->type.semanticTypeId)
                        return SemanticInvalid("Relation semantic field types do not agree in exact order.");
                }
                if (!HasStructuralPrefix(target, relation.targetFields) ||
                    (relation.cardinality != RelationCardinality::Many &&
                     !IsStructuralUnique(target, relation.targetFields)) ||
                    !HasStructuralPrefix(model, relation.sourceFields))
                    return SemanticInvalid("Relation key or index coverage invariant is invalid.");
                bool has_optional = false;
                for (const auto& reference : relation.sourceFields)
                {
                    const auto* field = FindReferencedField(model, reference);
                    has_optional = has_optional || field->cardinality == FieldCardinality::OptionalOne;
                    if (field->cardinality == FieldCardinality::Repeated ||
                        (relation.cardinality == RelationCardinality::One &&
                         field->cardinality != FieldCardinality::RequiredOne) ||
                        ((relation.onDelete == ReferentialAction::SetNull ||
                          relation.onUpdate == ReferentialAction::SetNull) &&
                         field->cardinality != FieldCardinality::OptionalOne) ||
                        ((relation.onDelete == ReferentialAction::SetNull ||
                          relation.onUpdate != ReferentialAction::Restrict) &&
                         (field->immutable || field->writeOnce)))
                        return SemanticInvalid("Relation nullability or mutability action invariant is invalid.");
                }
                if (relation.cardinality == RelationCardinality::OptionalOne && !has_optional)
                    return SemanticInvalid("Optional relation requires an optional source field.");
                if (model.retention == RetentionPolicy::Immutable &&
                    (relation.onDelete != ReferentialAction::Restrict ||
                     relation.onUpdate != ReferentialAction::Restrict))
                    return SemanticInvalid("Immutable model relation actions must restrict mutation.");
                relation.provenance.sourcePath = source_path;
            }

            previous_name.clear();
            for (auto& join : model.joins)
            {
                if (!IsCanonicalSnakeCase(join.name) || (!previous_name.empty() && previous_name >= join.name) ||
                    !include_names.insert(join.name).second || join.throughModel.value >= ir.models.size() ||
                    join.sourceRelation == join.targetRelation)
                    return SemanticInvalid("Join identity, include namespace, or ordering invariant is invalid.");
                previous_name = join.name;
                const auto& through = ir.models[join.throughModel.value];
                if (join.throughTypeId != through.typeId || join.throughQualifiedName != through.qualifiedName)
                    return SemanticInvalid("Join through-model identity invariant is invalid.");
                const auto source_relation = std::find_if(
                    through.relations.begin(),
                    through.relations.end(),
                    [&join](const auto& relation) { return relation.name == join.sourceRelation; }
                );
                const auto target_relation = std::find_if(
                    through.relations.begin(),
                    through.relations.end(),
                    [&join](const auto& relation) { return relation.name == join.targetRelation; }
                );
                if (source_relation == through.relations.end() || target_relation == through.relations.end() ||
                    source_relation->targetModel != model.handle)
                    return SemanticInvalid("Join relation resolution invariant is invalid.");
                join.provenance.sourcePath = source_path;
            }

            std::map<std::string, std::vector<SemanticFieldReference>> expected_generated_indexes;
            const auto has_reconstructed_prefix = [&model, &expected_generated_indexes](const auto& fields) {
                if (std::any_of(model.keys.begin(), model.keys.end(), [&fields](const auto& key) {
                        return ReferenceFieldsPrefix(fields, key.fields);
                    }) ||
                    std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const auto& index) {
                        return !index.generated && ReferenceFieldsPrefix(fields, index.fields);
                    }))
                    return true;
                return std::any_of(
                    expected_generated_indexes.begin(),
                    expected_generated_indexes.end(),
                    [&fields](const auto& entry) { return ReferenceFieldsPrefix(fields, entry.second); }
                );
            };
            for (const auto& relation : model.relations)
            {
                if (has_reconstructed_prefix(relation.sourceFields)) continue;
                const auto name = model.name + "_relation_" + relation.name;
                if (!expected_generated_indexes.emplace(name, relation.sourceFields).second)
                    return SemanticInvalid("Generated relation index name is not unique.");
            }
            std::size_t generated_index_count = 0;
            for (const auto& index : model.indexes)
            {
                if (!index.generated) continue;
                ++generated_index_count;
                const auto expected = expected_generated_indexes.find(index.name);
                if (expected == expected_generated_indexes.end() ||
                    !ReferenceFieldsEqual(index.fields, expected->second))
                    return SemanticInvalid("Generated relation index does not match deterministic reconstruction.");
            }
            if (generated_index_count != expected_generated_indexes.size())
                return SemanticInvalid("Generated relation index set is incomplete.");

            previous_name.clear();
            for (auto& one_of : model.oneOfs)
            {
                if (!IsCanonicalSnakeCase(one_of.name) || (!previous_name.empty() && previous_name >= one_of.name) ||
                    FindReferencedField(model, one_of.discriminator) == nullptr ||
                    one_of.enumeration.value >= ir.enums.size())
                    return SemanticInvalid("One_of identity, discriminator, enum, or ordering invariant is invalid.");
                previous_name = one_of.name;
                const auto* discriminator = FindReferencedField(model, one_of.discriminator);
                const auto& enumeration = ir.enums[one_of.enumeration.value];
                if (discriminator->type.kind != SemanticTypeKind::Enum ||
                    discriminator->type.definingEnum != enumeration.handle)
                    return SemanticInvalid("One_of discriminator does not bind its governed enum.");
                std::set<std::string> expected_tags;
                for (const auto& value : enumeration.values) expected_tags.insert(value.name);
                std::set<std::string> actual_tags;
                std::string previous_tag;
                for (auto& variant : one_of.variants)
                {
                    if (!IsCanonicalSnakeCase(variant.tag) ||
                        (!previous_tag.empty() && previous_tag >= variant.tag) ||
                        !expected_tags.contains(variant.tag) || !actual_tags.insert(variant.tag).second ||
                        !ValidateReferenceList(model, variant.fields, true) ||
                        !ValidateReferenceList(model, variant.optionalFields, true))
                        return SemanticInvalid("One_of variant tag, payload, or ordering invariant is invalid.");
                    previous_tag = variant.tag;
                    std::set<FieldHandle> variant_payload;
                    for (const auto& field : variant.fields) variant_payload.insert(field.handle);
                    for (const auto& field : variant.optionalFields)
                        if (!variant_payload.insert(field.handle).second)
                            return SemanticInvalid("One_of required and optional payloads overlap.");
                    for (const auto handle : variant_payload)
                    {
                        if (handle == discriminator->handle)
                            return SemanticInvalid("One_of discriminator overlaps an active variant payload.");
                        const auto field = std::find_if(
                            model.fields.begin(),
                            model.fields.end(),
                            [handle](const auto& value) { return value.handle == handle; }
                        );
                        if (field == model.fields.end() ||
                            field->cardinality != FieldCardinality::OptionalOne)
                            return SemanticInvalid("One_of payload fields must be optional outside their variant.");
                    }
                    variant.provenance.sourcePath = source_path;
                }
                if (actual_tags != expected_tags)
                    return SemanticInvalid("One_of variants do not exactly cover the discriminator enum.");
                one_of.provenance.sourcePath = source_path;
            }
            previous_name.clear();
            for (auto& contract : model.temporalContracts)
            {
                const auto* start = FindReferencedField(model, contract.startField);
                const auto* end = FindReferencedField(model, contract.endField);
                if (!IsCanonicalSnakeCase(contract.name) ||
                    (!previous_name.empty() && previous_name >= contract.name) || start == nullptr || end == nullptr ||
                    start == end || TemporalOrderingName(contract.ordering) == "unknown" ||
                    OpenEndPolicyName(contract.openEnd) == "unknown")
                    return SemanticInvalid("Temporal contract identity, fields, policy, or ordering is invalid.");
                previous_name = contract.name;
                const auto& start_type = ir.semanticTypes[start->type.semanticType.value];
                const auto& end_type = ir.semanticTypes[end->type.semanticType.value];
                if (start_type.kind != SemanticTypeKind::Timestamp ||
                    end_type.kind != SemanticTypeKind::Timestamp ||
                    start_type.semanticTypeId != end_type.semanticTypeId ||
                    start->temporalRole == TemporalRole::None ||
                    start->temporalRole != end->temporalRole ||
                    start->cardinality != FieldCardinality::RequiredOne ||
                    end->cardinality == FieldCardinality::Repeated ||
                    ((contract.openEnd == OpenEndPolicy::Allowed) ==
                     (end->cardinality == FieldCardinality::RequiredOne)))
                    return SemanticInvalid("Temporal contract type, role, or open-end semantics are invalid.");
                if (!ManifestAllTargets(start_type.targets) || !ManifestAllTargets(end_type.targets))
                    return SemanticInvalid("Temporal contract target profile set is incomplete.");
                contract.provenance.sourcePath = source_path;
            }
        }
        return true;
    }

    bool ValidateCatalogMetadata(SemanticIr& ir)
    {
        if (!ir.catalog.has_value())
        {
            for (const auto& model : ir.models)
            {
                if (model.catalogIdentity.has_value())
                    return SemanticInvalid("Model catalog identity exists without an IR catalog declaration.");
                for (const auto& field : model.fields)
                    if (field.catalogIdentity.has_value())
                        return SemanticInvalid("Field catalog identity exists without an IR catalog declaration.");
            }
            return true;
        }

        auto& catalog = *ir.catalog;
        if (catalog.catalogId.empty() || catalog.version == 0)
            return SemanticInvalid("Catalog identity or version is invalid.");
        catalog.provenance.sourcePath = ir.sources[ir.entrySource.value].path;
        std::map<std::string, std::string> fragment_owners;
        for (std::size_t index = 0; index < catalog.fragments.size(); ++index)
        {
            auto& fragment = catalog.fragments[index];
            if (fragment.fragmentId.empty() || !IsCanonicalSnakeCase(fragment.ownerCapsule) ||
                fragment.version == 0 || fragment.declarationOrder != index ||
                !fragment_owners.emplace(fragment.fragmentId, fragment.ownerCapsule).second)
                return SemanticInvalid("Catalog fragment identity, owner, version, or ordering is invalid.");
            fragment.provenance.sourcePath = catalog.provenance.sourcePath;
        }

        std::set<std::string> registry_keys;
        std::set<std::string> model_ids;
        std::set<std::string> qualified_names;
        std::map<std::string, std::set<std::uint32_t>> model_orders;
        for (const auto& model : ir.models)
        {
            if (model.external)
            {
                if (model.source.value >= ir.sources.size() || model.catalogIdentity.has_value() ||
                    std::any_of(model.fields.begin(), model.fields.end(), [](const auto& field) {
                        return field.catalogIdentity.has_value();
                    }))
                    return SemanticInvalid("External model cannot claim catalog identity.");
                continue;
            }
            if (!model.catalogIdentity.has_value() || model.source.value >= ir.sources.size())
                return SemanticInvalid("Catalog-backed model identity is missing or source-invalid.");
            const auto& identity = *model.catalogIdentity;
            const auto fragment = fragment_owners.find(identity.fragmentId);
            if (fragment == fragment_owners.end() || fragment->second != ir.sources[model.source.value].ownerCapsule ||
                identity.registryKey.empty() || identity.modelId.empty() || identity.logicalName.empty() ||
                identity.qualifiedName.empty() || !registry_keys.insert(identity.registryKey).second ||
                !model_ids.insert(identity.modelId).second || !qualified_names.insert(identity.qualifiedName).second ||
                !model_orders[identity.fragmentId].insert(identity.declarationOrder).second)
                return SemanticInvalid("Catalog model identity, ownership, uniqueness, or ordering is invalid.");

            std::set<std::string> field_names;
            for (std::size_t field_index = 0; field_index < model.fields.size(); ++field_index)
            {
                const auto& field = model.fields[field_index];
                if (!field.catalogIdentity.has_value() || field.catalogIdentity->registryKey.empty() ||
                    field.catalogIdentity->fieldName.empty() ||
                    field.catalogIdentity->declarationOrder != field_index ||
                    !registry_keys.insert(field.catalogIdentity->registryKey).second ||
                    !field_names.insert(field.catalogIdentity->fieldName).second)
                    return SemanticInvalid("Catalog field identity, uniqueness, or declaration order is invalid.");
            }
        }
        for (const auto& entry : model_orders)
        {
            const auto& orders = entry.second;
            std::uint32_t expected = 0;
            for (const auto order : orders)
                if (order != expected++)
                    return SemanticInvalid("Catalog model declaration order is not contiguous within its fragment.");
        }
        return true;
    }

    bool ValidateIr(SemanticIr& ir)
    {
        if (ir.formatVersion != kManifestFormatVersion || ir.sources.empty() ||
            ir.entrySource.value >= ir.sources.size())
            return SemanticInvalid("IR format, source set, or entry handle is invalid.");

        std::string previous_path;
        for (std::size_t index = 0; index < ir.sources.size(); ++index)
        {
            auto& source = ir.sources[index];
            if (source.handle.value != index || !IsCanonicalSourcePath(source.path) ||
                (!previous_path.empty() && previous_path >= source.path) || source.edition != 1 ||
                source.schemaVersion == 0 || !IsCanonicalSnakeCase(source.schemaName) ||
                !IsCanonicalSnakeCase(source.ownerCapsule))
            {
                return SemanticInvalid("Source ordering or identity invariant is invalid.");
            }
            previous_path = source.path;
            std::string previous_import;
            for (auto& import : source.imports)
            {
                if (import.source.value >= ir.sources.size() ||
                    ir.sources[import.source.value].path != import.sourcePath ||
                    (!previous_import.empty() && previous_import >= import.sourcePath))
                {
                    return SemanticInvalid("Import handle, path, or ordering invariant is invalid.");
                }
                previous_import = import.sourcePath;
                import.provenance.sourcePath = source.path;
            }
        }
        if (!ValidateCatalogMetadata(ir)) return false;

        std::vector<std::vector<WireTypeHandle>> source_wire_types(ir.sources.size());
        std::set<std::uint32_t> wire_ids;
        std::set<std::string> wire_names;
        std::set<WirePrimitiveKind> wire_kinds;
        std::optional<std::pair<std::uint32_t, std::string>> previous_wire;
        for (std::size_t index = 0; index < ir.wireTypes.size(); ++index)
        {
            auto& type = ir.wireTypes[index];
            const std::pair current{type.wireTypeId.value, type.qualifiedName};
            if (type.handle.value != index || type.source.value >= ir.sources.size() || !type.wireTypeId.valid() ||
                !IsCanonicalSnakeCase(type.name) || !IsCanonicalQualifiedName(type.qualifiedName) ||
                type.qualifiedName != ir.sources[type.source.value].schemaName + ".v" +
                                          std::to_string(ir.sources[type.source.value].schemaVersion) + "." +
                                          type.name ||
                type.ownerCapsule != ir.sources[type.source.value].ownerCapsule || !ManifestWireShapeValid(type) ||
                !wire_ids.insert(type.wireTypeId.value).second || !wire_names.insert(type.name).second ||
                !wire_kinds.insert(type.kind).second || (previous_wire.has_value() && *previous_wire >= current))
            {
                return SemanticInvalid("Wire type handle, identity, ownership, or "
                                       "ordering invariant is invalid.");
            }
            previous_wire = current;
            type.provenance.sourcePath = ir.sources[type.source.value].path;
            source_wire_types[type.source.value].push_back(type.handle);
        }

        std::vector<std::vector<StorageCodecHandle>> source_storage_codecs(ir.sources.size());
        std::set<std::uint32_t> storage_ids;
        std::set<std::string> storage_names;
        std::set<StorageCodecKind> storage_kinds;
        std::optional<std::pair<std::uint32_t, std::string>> previous_storage;
        for (std::size_t index = 0; index < ir.storageCodecs.size(); ++index)
        {
            auto& codec = ir.storageCodecs[index];
            const std::pair current{codec.storageCodecId.value, codec.qualifiedName};
            if (codec.handle.value != index || codec.source.value >= ir.sources.size() ||
                !codec.storageCodecId.valid() || !IsCanonicalSnakeCase(codec.name) ||
                !IsCanonicalQualifiedName(codec.qualifiedName) ||
                codec.qualifiedName != ir.sources[codec.source.value].schemaName + ".v" +
                                           std::to_string(ir.sources[codec.source.value].schemaVersion) + "." +
                                           codec.name ||
                codec.ownerCapsule != ir.sources[codec.source.value].ownerCapsule ||
                !ManifestStorageShapeValid(codec) || !storage_ids.insert(codec.storageCodecId.value).second ||
                !storage_names.insert(codec.name).second || !storage_kinds.insert(codec.kind).second ||
                (previous_storage.has_value() && *previous_storage >= current))
            {
                return SemanticInvalid("Storage codec handle, identity, exactness, "
                                       "ownership, or ordering invariant is invalid.");
            }
            previous_storage = current;
            codec.provenance.sourcePath = ir.sources[codec.source.value].path;
            source_storage_codecs[codec.source.value].push_back(codec.handle);
        }

        std::vector<std::vector<SemanticTypeHandle>> source_semantic_types(ir.sources.size());
        std::set<std::uint32_t> semantic_ids;
        std::set<std::string> semantic_names;
        std::set<std::string> semantic_registry_keys;
        std::optional<std::pair<std::uint32_t, std::string>> previous_semantic;
        for (std::size_t index = 0; index < ir.semanticTypes.size(); ++index)
        {
            auto& type = ir.semanticTypes[index];
            const std::pair current{type.semanticTypeId.value, type.qualifiedName};
            const auto& source = ir.sources[type.source.value < ir.sources.size() ? type.source.value : 0];
            if (type.handle.value != index || type.source.value >= ir.sources.size() || !type.semanticTypeId.valid() ||
                !IsCanonicalSnakeCase(type.name) || !IsCanonicalQualifiedName(type.qualifiedName) ||
                type.qualifiedName !=
                    source.schemaName + ".v" + std::to_string(source.schemaVersion) + "." + type.name ||
                type.ownerCapsule != ir.sources[type.source.value].ownerCapsule ||
                type.wireType.value >= ir.wireTypes.size() || type.storageCodec.value >= ir.storageCodecs.size() ||
                type.semanticName.empty() != type.registryKey.empty() ||
                (!type.registryKey.empty() && !semantic_registry_keys.insert(type.registryKey).second) ||
                !semantic_ids.insert(type.semanticTypeId.value).second || !semantic_names.insert(type.name).second ||
                type.targets.empty() || !std::is_sorted(type.targets.begin(), type.targets.end()) ||
                std::adjacent_find(type.targets.begin(), type.targets.end()) != type.targets.end() ||
                (previous_semantic.has_value() && *previous_semantic >= current) ||
                !ManifestBindingCompatible(
                    type.kind, ir.wireTypes[type.wireType.value].kind, ir.storageCodecs[type.storageCodec.value].kind
                ) ||
                !ManifestSemanticShapeValid(type, ir.wireTypes[type.wireType.value]))
            {
                return SemanticInvalid("Semantic type handle, identity, binding, target, ownership, or "
                                       "ordering invariant is invalid.");
            }
            if (type.kind != SemanticTypeKind::Timestamp && type.temporalRole != TemporalRole::None)
                return SemanticInvalid("Semantic temporal role does not match its type kind.");
            previous_semantic = current;
            type.provenance.sourcePath = ir.sources[type.source.value].path;
            source_semantic_types[type.source.value].push_back(type.handle);
        }

        for (std::size_t index = 0; index < ir.sources.size(); ++index)
        {
            if (ir.sources[index].wireTypes != source_wire_types[index] ||
                ir.sources[index].storageCodecs != source_storage_codecs[index] ||
                ir.sources[index].semanticTypes != source_semantic_types[index])
            {
                return SemanticInvalid("Source type-handle index is inconsistent with "
                                       "semantic type registries.");
            }
        }

        std::vector<std::vector<EnumHandle>> source_enums(ir.sources.size());
        std::set<std::uint32_t> enum_ids;
        std::set<std::string> enum_names;
        std::optional<std::pair<std::uint32_t, std::string>> previous_enum;
        for (std::size_t index = 0; index < ir.enums.size(); ++index)
        {
            auto& declaration = ir.enums[index];
            if (declaration.handle.value != index || declaration.source.value >= ir.sources.size() ||
                !declaration.enumId.valid() || !IsCanonicalSnakeCase(declaration.name) ||
                !IsCanonicalQualifiedName(declaration.qualifiedName) ||
                !IsCanonicalSnakeCase(declaration.ownerCapsule) ||
                !IsCanonicalSnakeCase(declaration.mutationAuthority) ||
                declaration.semanticType.value >= ir.semanticTypes.size())
                return SemanticInvalid("Enum handle, source, identity, ownership, or type invariant is invalid.");
            const auto& source = ir.sources[declaration.source.value];
            const auto expected_qualified = source.schemaName + ".v" +
                std::to_string(source.schemaVersion) + "." + declaration.name;
            const auto& type = ir.semanticTypes[declaration.semanticType.value];
            if (declaration.ownerCapsule != source.ownerCapsule ||
                declaration.mutationAuthority != source.ownerCapsule ||
                declaration.qualifiedName != expected_qualified || declaration.semanticTypeId != type.semanticTypeId ||
                type.kind != SemanticTypeKind::Enum || type.source != declaration.source ||
                type.definingEnum != declaration.handle || type.definingModel.has_value() ||
                !enum_ids.insert(declaration.enumId.value).second ||
                !enum_names.insert(declaration.qualifiedName).second)
                return SemanticInvalid("Enum namespace, semantic type binding, or uniqueness invariant is invalid.");
            const std::pair current{declaration.enumId.value, declaration.qualifiedName};
            if (previous_enum.has_value() && *previous_enum >= current)
                return SemanticInvalid("Enum canonical ordering invariant is invalid.");
            previous_enum = current;
            source_enums[declaration.source.value].push_back(declaration.handle);

            std::uint32_t previous_value_id = 0;
            std::set<std::string> value_names;
            const auto& wire = ir.wireTypes[type.wireType.value];
            const std::uint64_t maximum_value = wire.kind == WirePrimitiveKind::U8
                ? std::numeric_limits<std::uint8_t>::max()
                : wire.kind == WirePrimitiveKind::U16
                    ? std::numeric_limits<std::uint16_t>::max()
                    : std::numeric_limits<std::uint32_t>::max();
            for (auto& enum_value : declaration.values)
            {
                if (!enum_value.valueId.valid() || enum_value.valueId.value <= previous_value_id ||
                    enum_value.valueId.value > maximum_value || !IsCanonicalSnakeCase(enum_value.name) ||
                    !value_names.insert(enum_value.name).second)
                    return SemanticInvalid("Enum value identity, name, or ordering invariant is invalid.");
                previous_value_id = enum_value.valueId.value;
                enum_value.provenance.sourcePath = source.path;
            }
            if (declaration.values.empty()) return SemanticInvalid("Enum must contain at least one value.");
            declaration.provenance.sourcePath = source.path;
        }
        for (std::size_t index = 0; index < ir.sources.size(); ++index)
            if (ir.sources[index].enums != source_enums[index])
                return SemanticInvalid("Source enum-handle index is inconsistent with semantic enums.");

        std::set<std::uint32_t> type_ids;
        std::set<std::string> qualified_names;
        std::set<std::string> model_storage_names;
        std::set<std::string> model_resolution_names;
        std::set<std::string> model_source_aliases;
        for (const auto& model : ir.models) model_resolution_names.insert(model.qualifiedName);
        std::uint32_t next_field_handle = 0;
        std::optional<std::pair<std::uint32_t, std::string>> previous_model;
        std::vector<std::vector<ModelHandle>> source_models(ir.sources.size());
        for (std::size_t index = 0; index < ir.models.size(); ++index)
        {
            auto& model = ir.models[index];
            if (model.handle.value != index || model.source.value >= ir.sources.size() || !model.typeId.valid() ||
                !IsCanonicalSnakeCase(model.schemaName) || !IsCanonicalSnakeCase(model.name) ||
                !IsCanonicalSnakeCase(model.storageName) || !IsCanonicalSnakeCase(model.ownerCapsule) ||
                !IsCanonicalSnakeCase(model.domain) || !IsCanonicalSnakeCase(model.mutationAuthority) ||
                ModelKindName(model.kind) == "unknown" || RetentionPolicyName(model.retention) == "unknown" ||
                ModelRoleName(model.role) == "unknown" || NameReviewStatusName(model.nameReview) == "unknown")
            {
                return SemanticInvalid("Model handle, source, identity, or name invariant is invalid.");
            }
            const auto& source = ir.sources[model.source.value];
            const std::string expected_qualified = model.schemaName + ".v" +
                std::to_string(model.schemaVersion) + "." + model.name;
            if (source.schemaName != model.schemaName || source.schemaVersion != model.schemaVersion ||
                source.ownerCapsule != model.ownerCapsule || model.qualifiedName != expected_qualified ||
                !type_ids.insert(model.typeId.value).second || !qualified_names.insert(model.qualifiedName).second ||
                !model_storage_names.insert(model.storageName).second ||
                (model.nameReview == NameReviewStatus::Canonical && !model.nameReviewRationale.empty()) ||
                (model.nameReview != NameReviewStatus::Canonical && model.nameReviewRationale.empty()) ||
                !std::is_sorted(model.sourceAliases.begin(), model.sourceAliases.end()) ||
                std::adjacent_find(model.sourceAliases.begin(), model.sourceAliases.end()) != model.sourceAliases.end() ||
                !std::is_sorted(model.retiredNames.begin(), model.retiredNames.end()) ||
                std::adjacent_find(model.retiredNames.begin(), model.retiredNames.end()) != model.retiredNames.end() ||
                !std::is_sorted(model.catalogRetiredNames.begin(), model.catalogRetiredNames.end()) ||
                std::adjacent_find(model.catalogRetiredNames.begin(), model.catalogRetiredNames.end()) !=
                    model.catalogRetiredNames.end() ||
                std::any_of(model.catalogRetiredNames.begin(), model.catalogRetiredNames.end(), [](const auto& name) {
                    return name.empty();
                }))
            {
                return SemanticInvalid("Model namespace, qualified name, ownership, or "
                                       "uniqueness invariant is invalid.");
            }
            const std::pair current_model{model.typeId.value, model.qualifiedName};
            if (previous_model.has_value() && *previous_model >= current_model)
                return SemanticInvalid("Model canonical ordering invariant is invalid.");
            previous_model = current_model;
            source_models[model.source.value].push_back(model.handle);
            for (const auto& alias : model.sourceAliases)
                if (alias.empty() || !model_source_aliases.insert(alias).second)
                    return SemanticInvalid("Model source alias uniqueness invariant is invalid.");
            for (const auto& retired_name : model.retiredNames)
            {
                if (!IsCanonicalSnakeCase(retired_name) || retired_name == model.name ||
                    !model_resolution_names.insert(
                        model.schemaName + ".v" + std::to_string(model.schemaVersion) + "." + retired_name
                    ).second)
                    return SemanticInvalid("Model retired-name resolution invariant is invalid.");
            }

            std::uint32_t previous_field_id = 0;
            std::size_t primary_count = 0;
            std::set<std::uint32_t> field_ids;
            std::set<std::string> field_names;
            std::set<std::string> field_storage_names;
            for (auto& field : model.fields)
            {
                if (field.handle.value != next_field_handle++ || !field.fieldId.valid() ||
                    (!model.catalogIdentity.has_value() && field.fieldId.value <= previous_field_id) ||
                    !field_ids.insert(field.fieldId.value).second ||
                    !IsCanonicalSnakeCase(field.name) || !field_names.insert(field.name).second ||
                    !IsCanonicalSnakeCase(field.storageName) ||
                    !field_storage_names.insert(field.storageName).second ||
                    FieldCardinalityName(field.cardinality) == "unknown" ||
                    FieldSourceKindName(field.sourceKind) == "unknown" ||
                    TemporalRoleName(field.temporalRole) == "unknown" ||
                    SerializationPolicyName(field.serialization) == "unknown" ||
                    (!field.importTransform.empty() && !IsCanonicalSnakeCase(field.importTransform)) ||
                    ((field.sourceKind == FieldSourceKind::Imported) != field.sourceField.has_value()) ||
                    (field.sourceField.has_value() && field.sourceField->empty()) ||
                    (!field.importTransform.empty() && field.sourceKind != FieldSourceKind::Imported) ||
                    field.type.semanticType.value >= ir.semanticTypes.size())
                {
                    return SemanticInvalid("Field handle, identity, ordering, naming, or "
                                           "required semantics are invalid.");
                }
                previous_field_id = field.fieldId.value;
                field.provenance.sourcePath = source.path;
                const auto& semantic_type = ir.semanticTypes[field.type.semanticType.value];
                if (field.type.kind != semantic_type.kind ||
                    field.type.semanticTypeId != semantic_type.semanticTypeId ||
                    field.type.canonicalName != semantic_type.qualifiedName ||
                    field.type.definingModel != semantic_type.definingModel ||
                    field.type.definingEnum != semantic_type.definingEnum)
                {
                    return SemanticInvalid("Field semantic type reference does not match "
                                           "its governed definition.");
                }
                if (!field.allowedEnumValueIds.empty())
                {
                    if (semantic_type.kind != SemanticTypeKind::Enum ||
                        !semantic_type.definingEnum.has_value() ||
                        semantic_type.definingEnum->value >= ir.enums.size())
                        return SemanticInvalid("Field enum narrowing does not resolve to a governed enum.");
                    const auto& enumeration = ir.enums[semantic_type.definingEnum->value];
                    for (const auto allowed : field.allowedEnumValueIds)
                        if (std::none_of(
                                enumeration.values.begin(), enumeration.values.end(),
                                [allowed](const auto& value) { return value.valueId == allowed; }
                            ))
                            return SemanticInvalid("Field enum narrowing contains an unknown enum value ID.");
                }
                if (!field.representation.wireType.valid() ||
                    field.representation.wireType.value >= ir.wireTypes.size() ||
                    !field.representation.storageCodec.valid() ||
                    field.representation.storageCodec.value >= ir.storageCodecs.size() ||
                    field.representation.wireType != semantic_type.wireType ||
                    FieldKindName(field.representation.fieldKind) == "unknown" ||
                    NormalizationKindName(field.representation.normalization) == "unknown")
                    return SemanticInvalid("Field representation handle, kind, or normalization is invalid.");
                const auto& field_wire = ir.wireTypes[field.representation.wireType.value];
                const auto& field_storage = ir.storageCodecs[field.representation.storageCodec.value];
                if (field.representation.wireTypeId != field_wire.wireTypeId ||
                    field.representation.storageCodecId != field_storage.storageCodecId ||
                    !ManifestFieldKindCompatible(field.representation.fieldKind, semantic_type.kind) ||
                    !ManifestFieldBindingCompatible(semantic_type.kind, field_wire.kind, field_storage.kind))
                    return SemanticInvalid("Field representation does not match its exact governed binding.");
                const bool wire_size_valid = field_wire.kind == WirePrimitiveKind::FixedBytes
                    ? field.representation.wireSize > 0U &&
                        field.representation.wireSize == semantic_type.fixedSize
                    : field.representation.wireSize == 0U;
                const std::uint32_t expected_storage_size = field_storage.fixedSize != 0U
                    ? field_storage.fixedSize
                    : (field_storage.kind == StorageCodecKind::SqliteFixedBlob
                        ? field.representation.wireSize
                        : 0U);
                const bool decimal_shape_valid = semantic_type.kind == SemanticTypeKind::Decimal
                    ? field.representation.precision == semantic_type.precision &&
                        field.representation.scale == semantic_type.scale
                    : field.representation.precision == 0U && field.representation.scale == 0U;
                if (!wire_size_valid || field.representation.storageSize != expected_storage_size ||
                    !decimal_shape_valid)
                    return SemanticInvalid("Field representation shape is invalid.");
                if (field.type.kind != SemanticTypeKind::Timestamp && field.temporalRole != TemporalRole::None)
                    return SemanticInvalid("Field temporal role does not match its semantic type.");
                if (field.primaryKey)
                {
                    ++primary_count;
                    if (field.cardinality != FieldCardinality::RequiredOne ||
                        field.maximumItems.has_value() || field.redacted || !field.immutable)
                        return SemanticInvalid("Primary-key field traits are invalid.");
                }
                if (field.generatedPrimaryId)
                {
                    if (!field.primaryKey || !field.writeOnce ||
                        field.sourceKind != FieldSourceKind::BoundaryGenerated ||
                        field.type.kind != SemanticTypeKind::FixedId ||
                        !field.type.definingModel.has_value() || *field.type.definingModel != model.handle ||
                        field.name != model.name + "_id" ||
                        field.storageName != field.name ||
                        field.type.canonicalName != model.qualifiedName + "_id" ||
                        field.serialization != SerializationPolicy::Include ||
                        field.defaultValue.kind != FieldDefaultKind::Boundary ||
                        field.defaultValue.boundarySource != BoundaryDefaultSource::GeneratedId)
                    {
                        return SemanticInvalid("Expanded generated primary-ID semantics are invalid.");
                    }
                }
                if (field.defaultValue.kind == FieldDefaultKind::Boundary)
                {
                    const bool compatible =
                        (field.defaultValue.boundarySource == BoundaryDefaultSource::RequestTime &&
                         field.type.kind == SemanticTypeKind::Timestamp) ||
                        (field.defaultValue.boundarySource == BoundaryDefaultSource::GeneratedId &&
                         field.type.kind == SemanticTypeKind::FixedId) ||
                        (field.defaultValue.boundarySource == BoundaryDefaultSource::GeneratedToken &&
                         (field.type.kind == SemanticTypeKind::Text || field.type.kind == SemanticTypeKind::Blob));
                    if (!compatible)
                        return SemanticInvalid("Boundary default type is invalid.");
                }
                else if (!field.defaultValue.catalogValue.empty())
                {
                    return SemanticInvalid("Only boundary defaults may preserve a catalog value.");
                }
                if (field.defaultValue.kind == FieldDefaultKind::Literal &&
                    (field.type.kind == SemanticTypeKind::Blob || field.type.kind == SemanticTypeKind::FixedId))
                    return SemanticInvalid("Literal default is incompatible with its semantic type.");
                if (field.defaultValue.kind == FieldDefaultKind::Literal)
                {
                    bool literal_valid = true;
                    if (field.type.kind == SemanticTypeKind::Bool)
                        literal_valid = field.defaultValue.literal == "true" || field.defaultValue.literal == "false";
                    else if (field.type.kind == SemanticTypeKind::U8 || field.type.kind == SemanticTypeKind::U16 ||
                             field.type.kind == SemanticTypeKind::U32 || field.type.kind == SemanticTypeKind::U64 ||
                             field.type.kind == SemanticTypeKind::I8 || field.type.kind == SemanticTypeKind::I16 ||
                             field.type.kind == SemanticTypeKind::I32 || field.type.kind == SemanticTypeKind::I64 ||
                             field.type.kind == SemanticTypeKind::Timestamp)
                        literal_valid = ManifestIntegerFits(field.type.kind, field.defaultValue.literal);
                    else if (field.type.kind == SemanticTypeKind::F32 || field.type.kind == SemanticTypeKind::F64)
                        literal_valid = ManifestRealValid(field.defaultValue.literal);
                    else if (field.type.kind == SemanticTypeKind::Decimal)
                        literal_valid = ManifestDecimalValid(
                            field.defaultValue.literal, semantic_type.precision, semantic_type.scale
                        );
                    else if (field.type.kind == SemanticTypeKind::Enum)
                        literal_valid = semantic_type.definingEnum.has_value() &&
                            std::any_of(
                                ir.enums[semantic_type.definingEnum->value].values.begin(),
                                ir.enums[semantic_type.definingEnum->value].values.end(),
                                [&field](const auto& value) { return value.name == field.defaultValue.literal; }
                            );
                    else if (field.type.kind == SemanticTypeKind::JsonText)
                        literal_valid = detail::JsonLiteralValid(
                            field.defaultValue.literal,
                            semantic_type.jsonProfile
                        );
                    if (!literal_valid)
                        return SemanticInvalid("Field literal default is noncanonical or type-incompatible.");
                }
                std::string previous_check;
                std::set<CheckKind> generated_kinds;
                std::set<CheckKind> explicit_kinds;
                std::optional<std::uint64_t> minimum_unsigned;
                std::optional<std::uint64_t> maximum_unsigned;
                std::optional<std::int64_t> minimum_signed;
                std::optional<std::int64_t> maximum_signed;
                std::optional<double> minimum_real;
                std::optional<double> maximum_real;
                std::optional<std::uint32_t> minimum_length;
                std::optional<std::uint32_t> maximum_length;
                std::optional<std::uint32_t> fixed_length;
                for (const auto& check : field.checks)
                {
                    if (!IsCanonicalSnakeCase(check.name) ||
                        (!previous_check.empty() && previous_check >= check.name) ||
                        CheckKindName(check.kind) == "unknown" || CheckUnitName(check.unit) == "unknown" ||
                        check.targets != semantic_type.targets)
                        return SemanticInvalid("Field check identity, ordering, kind, or target profile is invalid.");
                    previous_check = check.name;
                    if (check.generated)
                    {
                        if (!generated_kinds.insert(check.kind).second)
                            return SemanticInvalid("Generated field check kind is duplicated.");
                    }
                    else
                    {
                        if (!explicit_kinds.insert(check.kind).second ||
                            !ManifestExplicitCheckCompatible(check.kind, field.type.kind, field_wire.kind))
                            return SemanticInvalid("Explicit field check kind is duplicated or incompatible.");
                        const bool length_check = check.kind == CheckKind::MinimumLength ||
                            check.kind == CheckKind::MaximumLength || check.kind == CheckKind::FixedLength;
                        const bool unit_valid = length_check
                            ? (field.representation.fieldKind == FieldKind::Blob
                                ? check.unit == CheckUnit::Bytes
                                : check.unit == CheckUnit::None || check.unit == CheckUnit::Utf8Bytes)
                            : check.unit == CheckUnit::None;
                        if (!unit_valid)
                            return SemanticInvalid("Explicit field check unit is incompatible with its semantic type.");
                        bool value_valid = true;
                        if (check.kind == CheckKind::MinimumInteger || check.kind == CheckKind::MaximumInteger)
                            value_valid = ManifestIntegerFits(field.type.kind, check.value);
                        else if (check.kind == CheckKind::MinimumReal || check.kind == CheckKind::MaximumReal)
                            value_valid = field.type.kind == SemanticTypeKind::Decimal
                                ? ManifestDecimalValid(check.value, semantic_type.precision, semantic_type.scale)
                                : ManifestRealValid(check.value);
                        else
                        {
                            std::uint32_t length = 0;
                            value_valid = ManifestCanonicalInteger(check.value, false) &&
                                ManifestParseInteger(check.value, length);
                        }
                        if (!value_valid)
                            return SemanticInvalid("Explicit field check value is noncanonical or out of range.");
                        if (check.kind == CheckKind::MinimumInteger || check.kind == CheckKind::MaximumInteger)
                        {
                            if (field.type.kind == SemanticTypeKind::I8 ||
                                field.type.kind == SemanticTypeKind::I16 ||
                                field.type.kind == SemanticTypeKind::I32 ||
                                field.type.kind == SemanticTypeKind::I64 ||
                                field.type.kind == SemanticTypeKind::Timestamp)
                            {
                                std::int64_t parsed = 0;
                                static_cast<void>(ManifestParseInteger(check.value, parsed));
                                (check.kind == CheckKind::MinimumInteger ? minimum_signed : maximum_signed) = parsed;
                            }
                            else
                            {
                                std::uint64_t parsed = 0;
                                static_cast<void>(ManifestParseInteger(check.value, parsed));
                                (check.kind == CheckKind::MinimumInteger ? minimum_unsigned : maximum_unsigned) =
                                    parsed;
                            }
                        }
                        else if (check.kind == CheckKind::MinimumReal || check.kind == CheckKind::MaximumReal)
                        {
                            const double parsed = Json::parse(check.value).get<double>();
                            (check.kind == CheckKind::MinimumReal ? minimum_real : maximum_real) = parsed;
                        }
                        else
                        {
                            std::uint32_t parsed = 0;
                            static_cast<void>(ManifestParseInteger(check.value, parsed));
                            if (check.kind == CheckKind::MinimumLength)
                                minimum_length = parsed;
                            else if (check.kind == CheckKind::MaximumLength)
                                maximum_length = parsed;
                            else
                                fixed_length = parsed;
                        }
                    }
                }
                if (field.type.kind == SemanticTypeKind::FixedId)
                {
                    if (fixed_length && *fixed_length != semantic_type.fixedSize)
                        return SemanticInvalid("Explicit fixed length conflicts with its governed semantic type.");
                    fixed_length = semantic_type.fixedSize;
                }
                if ((minimum_unsigned && maximum_unsigned && *minimum_unsigned > *maximum_unsigned) ||
                    (minimum_signed && maximum_signed && *minimum_signed > *maximum_signed) ||
                    (minimum_real && maximum_real && *minimum_real > *maximum_real) ||
                    (minimum_length && maximum_length && *minimum_length > *maximum_length) ||
                    (fixed_length && minimum_length && *fixed_length < *minimum_length) ||
                    (fixed_length && maximum_length && *fixed_length > *maximum_length))
                    return SemanticInvalid("Field check range or fixed-length relationship is invalid.");
                const bool enum_check = generated_kinds.contains(CheckKind::EnumDomain);
                const bool json_check = generated_kinds.contains(CheckKind::ValidJson);
                const bool precision_check = generated_kinds.contains(CheckKind::DecimalPrecision);
                const bool scale_check = generated_kinds.contains(CheckKind::DecimalScale);
                const bool fixed_check = generated_kinds.contains(CheckKind::FixedLength);
                if (enum_check != (field.type.kind == SemanticTypeKind::Enum) ||
                    json_check != (field.type.kind == SemanticTypeKind::JsonText) ||
                    precision_check != (field.type.kind == SemanticTypeKind::Decimal) ||
                    scale_check != (field.type.kind == SemanticTypeKind::Decimal) ||
                    fixed_check != (field.type.kind == SemanticTypeKind::FixedId))
                    return SemanticInvalid("Generated field check set is incomplete or incompatible.");
                for (const auto& check : field.checks)
                {
                    if (!check.generated) continue;
                    const std::string_view expected_name = check.kind == CheckKind::EnumDomain
                        ? "enum_domain"
                        : check.kind == CheckKind::ValidJson
                            ? "valid_json"
                            : check.kind == CheckKind::DecimalPrecision
                                ? "decimal_precision"
                                : check.kind == CheckKind::DecimalScale
                                    ? "decimal_scale"
                                    : check.kind == CheckKind::FixedLength ? "fixed_length" : "";
                    const bool valid =
                        check.name == expected_name && check.message.empty() && (
                        (check.kind == CheckKind::EnumDomain && semantic_type.definingEnum.has_value() &&
                         check.unit == CheckUnit::None &&
                         check.value == ir.enums[semantic_type.definingEnum->value].qualifiedName) ||
                        (check.kind == CheckKind::ValidJson && check.unit == CheckUnit::None &&
                         check.value == JsonProfileName(semantic_type.jsonProfile)) ||
                        (check.kind == CheckKind::DecimalPrecision &&
                         check.unit == CheckUnit::None &&
                         check.value == std::to_string(semantic_type.precision)) ||
                        (check.kind == CheckKind::DecimalScale &&
                         check.unit == CheckUnit::None &&
                         check.value == std::to_string(semantic_type.scale)) ||
                        (check.kind == CheckKind::FixedLength &&
                         check.unit == CheckUnit::Bytes &&
                         check.value == std::to_string(semantic_type.fixedSize)));
                    if (!valid) return SemanticInvalid("Generated field check value is not deterministic.");
                }
                if (!std::is_sorted(field.query.operators.begin(), field.query.operators.end()) ||
                    std::adjacent_find(field.query.operators.begin(), field.query.operators.end()) !=
                        field.query.operators.end() ||
                    (field.query.cursor && !field.query.sortable))
                    return SemanticInvalid("Field query ordering or cursor contract is invalid.");
                if (field.redacted &&
                    (field.serialization != SerializationPolicy::Omit || !field.query.operators.empty() ||
                     field.query.sortable || field.query.cursor))
                    return SemanticInvalid("Redacted field serialization or query policy is not fail-closed.");
                if ((field.defaultValue.kind != FieldDefaultKind::None || !field.checks.empty() ||
                     !field.query.operators.empty() || field.query.sortable || field.query.cursor) &&
                    !ManifestAllTargets(semantic_type.targets))
                    return SemanticInvalid("Field policy target profile family is incomplete.");
                if (field.cardinality == FieldCardinality::Repeated)
                {
                    if (!field.maximumItems.has_value() || *field.maximumItems == 0)
                        return SemanticInvalid("Repeated field maximum_items semantics are invalid.");
                }
                else if (field.maximumItems.has_value())
                {
                    return SemanticInvalid("Scalar field cannot declare maximum_items.");
                }
            }
            if (primary_count == 0)
                return SemanticInvalid("Every semantic model requires at least one primary-key field.");

            model.provenance.sourcePath = source.path;
        }
        for (std::size_t index = 0; index < ir.sources.size(); ++index)
        {
            if (ir.sources[index].models != source_models[index])
                return SemanticInvalid("Source model handle index is inconsistent with semantic models.");
        }
        if (!ValidateStructures(ir)) return false;
        for (const auto& type : ir.semanticTypes)
        {
            if (type.definingModel.has_value())
            {
                if (type.definingEnum.has_value() || type.definingModel->value >= ir.models.size())
                    return SemanticInvalid("Generated fixed-ID semantic type has an invalid defining model.");
                const auto& model = ir.models[type.definingModel->value];
                if (type.kind != SemanticTypeKind::FixedId || type.semanticTypeId.value != model.typeId.value ||
                    type.name != model.name + "_id" || type.qualifiedName != model.qualifiedName + "_id" ||
                    type.source != model.source || type.fixedSize != 16 || type.precision != 0 || type.scale != 0)
                    return SemanticInvalid("Generated fixed-ID semantic type does not match its defining model.");
            }
            if (type.definingEnum.has_value())
            {
                if (type.definingEnum->value >= ir.enums.size() || type.kind != SemanticTypeKind::Enum ||
                    ir.enums[type.definingEnum->value].semanticType != type.handle)
                    return SemanticInvalid("Enum semantic type has an invalid defining enum.");
            }
            if (type.kind == SemanticTypeKind::Enum && !type.definingEnum.has_value())
                return SemanticInvalid("Every enum semantic type requires exactly one defining enum.");
        }

        const auto model_reference_valid = [&ir](const SemanticModelReference& reference) {
            if (reference.model.value >= ir.models.size() || !reference.typeId.valid() ||
                !IsCanonicalQualifiedName(reference.canonicalName))
                return false;
            const auto& model = ir.models[reference.model.value];
            return reference.typeId == model.typeId && reference.canonicalName == model.qualifiedName;
        };
        const auto operation_profile_valid = [](const std::vector<OperationTargetProfile>& targets,
                                                const std::optional<FabricOperationProfile>& fabric) {
            if (targets.empty() || !std::is_sorted(targets.begin(), targets.end()) ||
                std::adjacent_find(targets.begin(), targets.end()) != targets.end())
                return false;
            for (const auto target : targets)
                if (OperationTargetProfileName(target) == "unknown") return false;
            const bool has_fabric = std::find(
                targets.begin(), targets.end(), OperationTargetProfile::Fabric
            ) != targets.end();
            return has_fabric == fabric.has_value() &&
                (!fabric.has_value() ||
                 (FabricLaneName(fabric->lane) != "unknown" && fabric->maximumEncodedBytes > 0));
        };
        const auto operation_targets_supported = [&ir](
            const std::vector<SemanticModelReference>& references,
            const std::vector<OperationTargetProfile>& targets
        ) {
            for (const auto& reference : references)
            {
                const auto& model = ir.models[reference.model.value];
                for (const auto& field : model.fields)
                {
                    const auto& semantic_type = ir.semanticTypes[field.type.semanticType.value];
                    for (const auto operation_target : targets)
                    {
                        const auto target = operation_target == OperationTargetProfile::Cpp
                            ? TargetProfile::Cpp
                            : operation_target == OperationTargetProfile::Fabric
                                ? TargetProfile::Fabric
                                : TargetProfile::HttpJson;
                        if (std::find(semantic_type.targets.begin(), semantic_type.targets.end(), target) ==
                            semantic_type.targets.end())
                            return false;
                    }
                }
            }
            return true;
        };
        std::vector<std::vector<CommandHandle>> source_commands(ir.sources.size());
        std::set<std::uint32_t> command_ids;
        std::set<std::string> command_names;
        std::optional<std::pair<std::uint32_t, std::string>> previous_command;
        for (std::size_t index = 0; index < ir.commands.size(); ++index)
        {
            auto& command = ir.commands[index];
            const std::pair current{command.commandId.value, command.qualifiedName};
            if (command.handle.value != index || command.source.value >= ir.sources.size() ||
                !command.commandId.valid() || !IsCanonicalSnakeCase(command.name) ||
                !IsCanonicalQualifiedName(command.qualifiedName) ||
                command.qualifiedName != ir.sources[command.source.value].schemaName + ".v" +
                                             std::to_string(ir.sources[command.source.value].schemaVersion) + "." +
                                             command.name ||
                command.ownerCapsule != ir.sources[command.source.value].ownerCapsule ||
                !command_ids.insert(command.commandId.value).second ||
                !command_names.insert(command.qualifiedName).second ||
                (previous_command.has_value() && *previous_command >= current) ||
                (command.request.has_value() && !model_reference_valid(*command.request)) ||
                (command.response.has_value() && !model_reference_valid(*command.response)) ||
                CommandIdempotencyName(command.idempotency) == "unknown" ||
                CommandStreamingName(command.streaming) == "unknown" ||
                !operation_profile_valid(command.targets, command.fabric))
            {
                return SemanticInvalid("Command handle, identity, model reference, policy, ownership, or ordering invariant is invalid.");
            }
            std::string previous_error;
            for (const auto& error : command.errors)
            {
                if (!model_reference_valid(error) || (!previous_error.empty() && previous_error >= error.canonicalName))
                    return SemanticInvalid("Command error model references must be valid, unique, and ordered.");
                previous_error = error.canonicalName;
            }
            std::vector<SemanticModelReference> operation_models;
            if (command.request.has_value()) operation_models.push_back(*command.request);
            if (command.response.has_value()) operation_models.push_back(*command.response);
            operation_models.insert(operation_models.end(), command.errors.begin(), command.errors.end());
            if (!operation_targets_supported(operation_models, command.targets))
                return SemanticInvalid("Command operation target semantic mappings are incomplete.");
            previous_command = current;
            command.provenance.sourcePath = ir.sources[command.source.value].path;
            source_commands[command.source.value].push_back(command.handle);
        }

        std::vector<std::vector<EventHandle>> source_events(ir.sources.size());
        std::set<std::uint32_t> event_ids;
        std::set<std::string> event_names;
        std::optional<std::pair<std::uint32_t, std::string>> previous_event;
        for (std::size_t index = 0; index < ir.events.size(); ++index)
        {
            auto& event = ir.events[index];
            const std::pair current{event.eventId.value, event.qualifiedName};
            if (event.handle.value != index || event.source.value >= ir.sources.size() ||
                !event.eventId.valid() || !IsCanonicalSnakeCase(event.name) ||
                !IsCanonicalQualifiedName(event.qualifiedName) ||
                event.qualifiedName != ir.sources[event.source.value].schemaName + ".v" +
                                         std::to_string(ir.sources[event.source.value].schemaVersion) + "." +
                                         event.name ||
                event.ownerCapsule != ir.sources[event.source.value].ownerCapsule ||
                !event_ids.insert(event.eventId.value).second ||
                !event_names.insert(event.qualifiedName).second ||
                (previous_event.has_value() && *previous_event >= current) ||
                !model_reference_valid(event.payload) || EventDeliveryName(event.delivery) == "unknown" ||
                EventOrderingName(event.ordering) == "unknown" || EventReplayName(event.replay) == "unknown" ||
                (event.delivery == EventDelivery::BestEffort && event.replay != EventReplay::None) ||
                !operation_profile_valid(event.targets, event.fabric) ||
                !operation_targets_supported({event.payload}, event.targets))
            {
                return SemanticInvalid("Event handle, identity, payload, policy, ownership, or ordering invariant is invalid.");
            }
            previous_event = current;
            event.provenance.sourcePath = ir.sources[event.source.value].path;
            source_events[event.source.value].push_back(event.handle);
        }
        for (std::size_t index = 0; index < ir.sources.size(); ++index)
        {
            if (ir.sources[index].commands != source_commands[index] ||
                ir.sources[index].events != source_events[index])
                return SemanticInvalid("Source operation-handle index is inconsistent with semantic operations.");
        }
        if (!ValidateReservations(ir)) return false;
        return ValidateImportAcyclic(ir);
    }

    bool ValidateReservations(SemanticIr& ir)
    {
        const RegistryScope model_scope{.kind = RegistryKind::Model};
        std::set<std::pair<RegistryScope, std::uint32_t>> active_ids;
        std::set<std::pair<RegistryScope, std::string>> active_names;
        std::map<ModelTypeId, const SemanticModel*> models;
        std::map<EnumId, const SemanticEnum*> enums;
        std::set<std::string> source_owners;
        for (const auto& source : ir.sources) source_owners.insert(source.ownerCapsule);
        for (const auto& wire : ir.wireTypes)
        {
            const RegistryScope scope{.kind = RegistryKind::WireType};
            active_ids.emplace(scope, wire.wireTypeId.value);
            active_names.emplace(scope, wire.qualifiedName);
        }
        for (const auto& codec : ir.storageCodecs)
        {
            const RegistryScope scope{.kind = RegistryKind::StorageCodec};
            active_ids.emplace(scope, codec.storageCodecId.value);
            active_names.emplace(scope, codec.qualifiedName);
        }
        for (const auto& type : ir.semanticTypes)
        {
            const RegistryScope scope{.kind = RegistryKind::SemanticType};
            active_ids.emplace(scope, type.semanticTypeId.value);
            active_names.emplace(scope, type.qualifiedName);
        }
        for (const auto& command : ir.commands)
        {
            const RegistryScope scope{.kind = RegistryKind::Command};
            active_ids.emplace(scope, command.commandId.value);
            active_names.emplace(scope, command.qualifiedName);
        }
        for (const auto& event : ir.events)
        {
            const RegistryScope scope{.kind = RegistryKind::Event};
            active_ids.emplace(scope, event.eventId.value);
            active_names.emplace(scope, event.qualifiedName);
        }
        for (const auto& model : ir.models)
        {
            models.emplace(model.typeId, &model);
            active_ids.emplace(model_scope, model.typeId.value);
            active_names.emplace(model_scope, model.qualifiedName);
            const RegistryScope field_scope{
                .kind = RegistryKind::Field,
                .modelTypeId = model.typeId,
            };
            for (const auto& field : model.fields)
            {
                active_ids.emplace(field_scope, field.fieldId.value);
                active_names.emplace(field_scope, field.name);
            }
        }
        const RegistryScope enum_scope{.kind = RegistryKind::Enum};
        for (const auto& declaration : ir.enums)
        {
            enums.emplace(declaration.enumId, &declaration);
            active_ids.emplace(enum_scope, declaration.enumId.value);
            active_names.emplace(enum_scope, declaration.qualifiedName);
            const RegistryScope value_scope{
                .kind = RegistryKind::EnumValue,
                .enumId = declaration.enumId,
            };
            for (const auto& value : declaration.values)
            {
                active_ids.emplace(value_scope, value.valueId.value);
                active_names.emplace(value_scope, value.name);
            }
        }

        std::optional<std::tuple<RegistryScope, std::uint32_t, std::string>> previous_id;
        std::set<std::pair<RegistryScope, std::uint32_t>> retired_ids;
        for (auto& reserved : ir.reservedIds)
        {
            const auto current = std::tuple{reserved.scope, reserved.value, reserved.ownerCapsule};
            if (!reserved.scope.valid() || reserved.value == 0 ||
                !IsCanonicalSnakeCase(reserved.ownerCapsule) ||
                !source_owners.contains(reserved.ownerCapsule) ||
                (previous_id.has_value() && *previous_id >= current) ||
                !retired_ids.emplace(reserved.scope, reserved.value).second ||
                active_ids.contains({reserved.scope, reserved.value}))
            {
                return SemanticInvalid("Reserved numeric identity invariant is invalid.");
            }
            if (reserved.scope.kind == RegistryKind::Field)
            {
                const auto model = models.find(*reserved.scope.modelTypeId);
                if (model == models.end() || model->second->ownerCapsule != reserved.ownerCapsule)
                    return SemanticInvalid("Reserved field identity owner or model scope is invalid.");
            }
            if (reserved.scope.kind == RegistryKind::EnumValue)
            {
                const auto declaration = enums.find(*reserved.scope.enumId);
                if (declaration == enums.end() || declaration->second->ownerCapsule != reserved.ownerCapsule)
                    return SemanticInvalid("Reserved enum value identity owner or enum scope is invalid.");
            }
            previous_id = current;
        }

        std::optional<std::tuple<RegistryScope, std::string, std::string>> previous_name;
        std::set<std::pair<RegistryScope, std::string>> retired_names;
        for (auto& reserved : ir.reservedNames)
        {
            const auto current = std::tuple{
                reserved.scope,
                reserved.canonicalName,
                reserved.ownerCapsule,
            };
            const bool name_valid = reserved.scope.kind == RegistryKind::Field ||
                    reserved.scope.kind == RegistryKind::EnumValue
                ? IsCanonicalSnakeCase(reserved.canonicalName)
                : IsCanonicalQualifiedName(reserved.canonicalName);
            if (!reserved.scope.valid() || !name_valid ||
                !IsCanonicalSnakeCase(reserved.ownerCapsule) ||
                !source_owners.contains(reserved.ownerCapsule) ||
                (previous_name.has_value() && *previous_name >= current) ||
                !retired_names.emplace(reserved.scope, reserved.canonicalName).second ||
                active_names.contains({reserved.scope, reserved.canonicalName}))
            {
                return SemanticInvalid("Reserved canonical name invariant is invalid.");
            }
            if (reserved.scope.kind == RegistryKind::Field)
            {
                const auto model = models.find(*reserved.scope.modelTypeId);
                if (model == models.end() || model->second->ownerCapsule != reserved.ownerCapsule)
                    return SemanticInvalid("Reserved field name owner or model scope is invalid.");
            }
            if (reserved.scope.kind == RegistryKind::EnumValue)
            {
                const auto declaration = enums.find(*reserved.scope.enumId);
                if (declaration == enums.end() || declaration->second->ownerCapsule != reserved.ownerCapsule)
                    return SemanticInvalid("Reserved enum value name owner or enum scope is invalid.");
            }
            previous_name = current;
        }
        return true;
    }

    bool ValidateImportAcyclic(const SemanticIr& ir)
    {
        enum class State { Unseen, Visiting, Done };
        std::vector<State> states(ir.sources.size(), State::Unseen);
        const auto visit = [&](const auto& self, SourceHandle handle, std::size_t depth) -> bool {
            if (depth > limits_.maximumImportDepth)
                return SemanticInvalid("Semantic manifest import depth exceeds the configured limit.");
            auto& state = states[handle.value];
            if (state == State::Visiting) return false;
            if (state == State::Done) return true;
            state = State::Visiting;
            for (const auto& import : ir.sources[handle.value].imports)
            {
                if (!self(self, import.source, depth + 1)) return false;
            }
            state = State::Done;
            return true;
        };
        for (const auto& source : ir.sources)
        {
            if (!visit(visit, source.handle, 0))
            {
                if (diagnostics_.empty())
                    return SemanticInvalid("Semantic manifest import graph contains a cycle.");
                return false;
            }
        }
        return true;
    }

    bool SemanticInvalid(std::string message)
    {
        Add("idl.manifest_semantic_invalid", std::move(message), "/semantic_ir");
        return false;
    }

    void Add(std::string code, std::string message, std::string_view path)
    {
        if (diagnostics_.size() >= limits_.maximumDiagnostics) return;
        diagnostics_.push_back({
            .code = std::move(code),
            .severity = DiagnosticSeverity::Error,
            .message = std::move(message) + " [" + std::string(path) + "]",
            .sourceName = sourceName_,
            .span = EmptySpan(),
        });
    }

    SemanticManifestResult Finish()
    {
        if (!diagnostics_.empty())
            return {.manifest = std::nullopt, .diagnostics = std::move(diagnostics_)};
        return {.manifest = std::move(manifest_), .diagnostics = {}};
    }

    std::string sourceName_;
    const ParseLimits& limits_;
    std::optional<SemanticManifest> manifest_;
    std::vector<Diagnostic> diagnostics_;
};
}

namespace detail
{
SemanticManifest CreateSemanticManifest(SemanticIr ir)
{
    const Json semantic_json = IrJson(ir);
    const std::string semantic_bytes = semantic_json.dump();
    const std::string fingerprint = Sha256Hex(semantic_bytes);
    const Json envelope = {
        {"format", kManifestFormat},
        {"format_version", kManifestFormatVersion},
        {"semantic_fingerprint", fingerprint},
        {"semantic_ir", semantic_json},
    };
    return {
        .ir = std::move(ir),
        .canonicalJson = envelope.dump(),
        .semanticFingerprint = fingerprint,
    };
}
}

SemanticManifestResult ParseSemanticManifest(
    std::string_view source_name,
    std::string_view manifest_json,
    const ParseLimits& limits
)
{
    return ManifestReader(source_name, limits).Read(manifest_json);
}
}
