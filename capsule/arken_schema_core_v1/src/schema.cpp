#include "arken_schema_core_v1/schema.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace arken::schema::core::v1
{
std::uint32_t StableRegistryId(std::string_view registry_namespace, std::string_view canonical_name)
{
    std::uint32_t hash = UINT32_C(2166136261);
    const auto hash_text = [&hash](std::string_view text) {
        for (const unsigned char ch : text)
        {
            hash ^= ch;
            hash *= UINT32_C(16777619);
        }
    };
    hash_text(registry_namespace);
    hash ^= UINT8_C(0xff);
    hash *= UINT32_C(16777619);
    hash_text(canonical_name);
    return hash == 0 ? UINT32_C(1) : hash;
}

SemanticTypeId SemanticTypeRegistryId(std::string_view registry_key)
{
    return {StableRegistryId("arken.schema.semantic-type.v1", registry_key)};
}

WireTypeId WireTypeRegistryId(WireType type)
{
    switch (type)
    {
        case WireType::None: return {};
        case WireType::Bool: return {1};
        case WireType::U8: return {2};
        case WireType::U16: return {3};
        case WireType::U32: return {4};
        case WireType::U64: return {5};
        case WireType::I8: return {6};
        case WireType::I16: return {7};
        case WireType::I32: return {8};
        case WireType::I64: return {9};
        case WireType::F32: return {10};
        case WireType::F64: return {11};
        case WireType::Utf8: return {12};
        case WireType::Bytes: return {13};
        case WireType::FixedBytes: return {14};
        case WireType::Decimal128: return {15};
    }
    return {};
}

StorageCodecId StorageCodecRegistryId(StorageCodec codec)
{
    switch (codec)
    {
        case StorageCodec::None: return {};
        case StorageCodec::SqliteInteger: return {1};
        case StorageCodec::SqliteReal: return {2};
        case StorageCodec::SqliteText: return {3};
        case StorageCodec::SqliteBlob: return {4};
        case StorageCodec::SqliteFixedBlob: return {5};
        case StorageCodec::SqliteOrderedU64: return {6};
        case StorageCodec::SqliteOrderedDecimal128: return {7};
    }
    return {};
}

namespace
{
using json = nlohmann::json;

bool IsAmbiguousStorageName(std::string_view name)
{
    static const std::set<std::string_view> names = {
        "acts",
        "applications",
        "areas",
        "assets",
        "collections",
        "counters",
        "data",
        "events",
        "files",
        "groups",
        "items",
        "logs",
        "markets",
        "memory",
        "memories",
        "messages",
        "meta",
        "metadata",
        "models",
        "nodes",
        "objects",
        "orders",
        "products",
        "profiles",
        "projects",
        "records",
        "repositories",
        "roles",
        "services",
        "sessions",
        "sets",
        "stock",
        "tags",
        "teams",
        "users",
        "worlds",
        "world_objects",
        "world_records",
    };
    return names.contains(name);
}

bool HasForbiddenProductPrefix(std::string_view name)
{
    return name.starts_with("arken_") || name.starts_with("legacy_") || name.starts_with("oasis_") ||
        name.starts_with("seer_");
}

ModelRole InferModelRole(ModelKind kind)
{
    switch (kind)
    {
        case ModelKind::Join: return ModelRole::Membership;
        case ModelKind::Ephemeral: return ModelRole::Occurrence;
        case ModelKind::AuditLedger: return ModelRole::Ledger;
        case ModelKind::Provenance: return ModelRole::Assertion;
        case ModelKind::ImportLedger: return ModelRole::ImportRecord;
        case ModelKind::Entity:
        case ModelKind::Credential: return ModelRole::AuthoritativeAggregate;
    }
    return ModelRole::AuthoritativeAggregate;
}

template <typename Value>
void SortAndUnique(std::vector<Value>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

void NormalizeModelRegistry(ModelSchema& model)
{
    if (model.qualifiedName.empty())
    {
        model.qualifiedName = model.domain + "." + model.modelId;
    }
    if (model.mutationAuthority.empty())
    {
        model.mutationAuthority = model.ownerCapsule;
    }
    if (model.registryKey.empty())
    {
        model.registryKey = model.qualifiedName;
    }
    if (model.role == ModelRole::Unspecified)
    {
        model.role = InferModelRole(model.kind);
    }
    if (model.nameReview == NameReviewStatus::Unspecified)
    {
        model.nameReview = IsAmbiguousStorageName(model.storageName)
            ? NameReviewStatus::ReviewRequired
            : NameReviewStatus::Canonical;
    }
    if (model.typeId == 0)
    {
        model.typeId = StableRegistryId("arken.schema.model.v1", model.qualifiedName);
    }
    for (auto& field : model.fields)
    {
        if (field.registryKey.empty())
        {
            const std::string_view field_identity = field.sourceField.empty()
                ? std::string_view(field.name)
                : std::string_view(field.sourceField);
            field.registryKey = model.registryKey + "/field:" + std::string(field_identity);
        }
        if (field.fieldId == 0)
        {
            field.fieldId = StableRegistryId("arken.schema.field.v1." + model.qualifiedName, field.name);
        }
        if (!field.semanticType.empty() && field.wireType != WireType::None &&
            field.storageCodec != StorageCodec::None)
        {
            if (field.semanticTypeKey.empty()) field.semanticTypeKey = "arken.semantic.v1/" + field.semanticType;
            if (field.semanticTypeId.value == 0) field.semanticTypeId = SemanticTypeRegistryId(field.semanticTypeKey);
            if (field.wireTypeId.value == 0) field.wireTypeId = WireTypeRegistryId(field.wireType);
            if (field.storageCodecId.value == 0) field.storageCodecId = StorageCodecRegistryId(field.storageCodec);
        }
    }
    SortAndUnique(model.sourceAliases);
    SortAndUnique(model.retiredNames);
    SortAndUnique(model.retiredFieldIds);
    SortAndUnique(model.retiredFieldNames);
}

template <typename Value, typename Projection>
bool HasDuplicate(const std::vector<Value>& values, Projection projection)
{
    std::set<std::string> seen;
    for (const auto& value : values)
    {
        if (!seen.insert(std::string(projection(value))).second)
        {
            return true;
        }
    }
    return false;
}

void AddError(
    ValidationResult& result,
    std::string code,
    std::string model_id,
    std::string path,
    std::string message
)
{
    result.errors.push_back({
        .code = std::move(code),
        .modelId = std::move(model_id),
        .path = std::move(path),
        .message = std::move(message),
    });
}

void AddWarning(
    ValidationResult& result,
    std::string code,
    std::string model_id,
    std::string path,
    std::string message
)
{
    result.warnings.push_back({
        .code = std::move(code),
        .modelId = std::move(model_id),
        .path = std::move(path),
        .message = std::move(message),
    });
}

void AppendCatalogViewValidation(
    ValidationResult& result,
    const catalog_view_validation_result& validation
)
{
    for (const auto& diagnostic : validation.errors)
    {
        const bool already_reported = std::any_of(
            result.errors.begin(),
            result.errors.end(),
            [&diagnostic](const ValidationDiagnostic& existing) {
                return existing.code == diagnostic.code &&
                    existing.modelId == diagnostic.model_id;
            }
        );
        if (already_reported) continue;
        AddError(
            result,
            diagnostic.code,
            diagnostic.model_id,
            diagnostic.path,
            diagnostic.message
        );
    }
}

json DiagnosticJson(const ValidationDiagnostic& diagnostic)
{
    return {
        {"code", diagnostic.code},
        {"modelId", diagnostic.modelId},
        {"path", diagnostic.path},
        {"message", diagnostic.message},
    };
}

bool IsTextKind(FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::ObjectId:
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::Enum:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
        case FieldKind::KeyReference:
            return true;
        case FieldKind::InternalId:
        case FieldKind::Bool:
        case FieldKind::TimestampMs:
        case FieldKind::JsonText:
        case FieldKind::Secret:
        case FieldKind::Integer:
        case FieldKind::Real:
        case FieldKind::Decimal:
        case FieldKind::Blob:
            return false;
    }
    return false;
}

bool IsLengthCheckedTextKind(FieldKind kind)
{
    return IsTextKind(kind) || kind == FieldKind::JsonText;
}

bool IsIntegerWireType(WireType type)
{
    switch (type)
    {
        case WireType::U8:
        case WireType::U16:
        case WireType::U32:
        case WireType::U64:
        case WireType::I8:
        case WireType::I16:
        case WireType::I32:
        case WireType::I64:
            return true;
        case WireType::None:
        case WireType::Bool:
        case WireType::F32:
        case WireType::F64:
        case WireType::Utf8:
        case WireType::Bytes:
        case WireType::FixedBytes:
        case WireType::Decimal128:
            return false;
    }
    return false;
}

bool IsWireTypeCompatible(FieldKind kind, WireType type)
{
    if (type == WireType::None)
    {
        return true;
    }
    switch (kind)
    {
        case FieldKind::Bool:
            return type == WireType::Bool;
        case FieldKind::InternalId:
        case FieldKind::Integer:
        case FieldKind::TimestampMs:
            return IsIntegerWireType(type);
        case FieldKind::Real:
            return type == WireType::F32 || type == WireType::F64;
        case FieldKind::Decimal:
            return type == WireType::Decimal128;
        case FieldKind::Blob:
            return type == WireType::Bytes || type == WireType::FixedBytes;
        case FieldKind::ObjectId:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
            return type == WireType::Utf8 || type == WireType::FixedBytes;
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::JsonText:
        case FieldKind::Secret:
        case FieldKind::KeyReference:
            return type == WireType::Utf8;
        case FieldKind::Enum:
            return type == WireType::Utf8 || IsIntegerWireType(type);
    }
    return false;
}

bool IsStorageCodecCompatible(FieldKind kind, StorageCodec codec)
{
    if (codec == StorageCodec::None)
    {
        return true;
    }
    switch (kind)
    {
        case FieldKind::InternalId:
        case FieldKind::Bool:
        case FieldKind::TimestampMs:
            return codec == StorageCodec::SqliteInteger;
        case FieldKind::Integer:
            return codec == StorageCodec::SqliteInteger || codec == StorageCodec::SqliteOrderedU64;
        case FieldKind::Real:
            return codec == StorageCodec::SqliteReal;
        case FieldKind::Decimal:
            return codec == StorageCodec::SqliteOrderedDecimal128;
        case FieldKind::Blob:
            return codec == StorageCodec::SqliteBlob || codec == StorageCodec::SqliteFixedBlob;
        case FieldKind::ObjectId:
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::JsonText:
        case FieldKind::Secret:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
        case FieldKind::KeyReference:
            return codec == StorageCodec::SqliteText;
        case FieldKind::Enum:
            return codec == StorageCodec::SqliteText || codec == StorageCodec::SqliteInteger;
    }
    return false;
}

bool IsOrderedKind(FieldKind kind)
{
    return IsTextKind(kind) || kind == FieldKind::InternalId || kind == FieldKind::Integer || kind == FieldKind::Real ||
        kind == FieldKind::Decimal || kind == FieldKind::TimestampMs;
}

bool IsQueryOperatorCompatible(FieldKind kind, QueryOperator query_operator)
{
    switch (query_operator)
    {
        case QueryOperator::Equal:
        case QueryOperator::NotEqual:
        case QueryOperator::In:
        case QueryOperator::NotIn:
        case QueryOperator::IsNull:
            return kind != FieldKind::Secret;
        case QueryOperator::LessThan:
        case QueryOperator::LessThanOrEqual:
        case QueryOperator::GreaterThan:
        case QueryOperator::GreaterThanOrEqual:
            return IsOrderedKind(kind);
        case QueryOperator::Prefix:
        case QueryOperator::Suffix:
        case QueryOperator::Contains:
            return IsTextKind(kind);
    }
    return false;
}

bool IsIntegerLiteral(std::string_view value)
{
    if (value.empty())
    {
        return false;
    }
    long long parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size();
}

bool IsLiteralDefaultCompatible(const FieldSchema& field)
{
    if (field.defaultValue.kind != DefaultKind::Literal)
    {
        return true;
    }
    switch (field.kind)
    {
        case FieldKind::Enum:
            return std::find(field.enumValues.begin(), field.enumValues.end(), field.defaultValue.value) != field.enumValues.end();
        case FieldKind::Bool:
            return field.defaultValue.value == "true" || field.defaultValue.value == "false";
        case FieldKind::Integer:
        case FieldKind::InternalId:
        case FieldKind::TimestampMs:
            return IsIntegerLiteral(field.defaultValue.value);
        case FieldKind::Secret:
        case FieldKind::Decimal:
        case FieldKind::Blob:
            return false;
        case FieldKind::ObjectId:
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
        case FieldKind::KeyReference:
        case FieldKind::Real:
            return true;
        case FieldKind::JsonText:
            return !json::parse(field.defaultValue.value, nullptr, false).is_discarded();
    }
    return false;
}

bool IsCheckCompatible(FieldKind kind, CheckKind check_kind)
{
    switch (check_kind)
    {
        case CheckKind::MinimumInteger:
        case CheckKind::MaximumInteger:
            return kind == FieldKind::InternalId || kind == FieldKind::Integer || kind == FieldKind::TimestampMs;
        case CheckKind::MinimumReal:
        case CheckKind::MaximumReal:
            return kind == FieldKind::Real;
        case CheckKind::MinimumLength:
        case CheckKind::MaximumLength:
        case CheckKind::FixedLength:
            return IsLengthCheckedTextKind(kind) || kind == FieldKind::Blob;
        case CheckKind::Pattern:
            return IsTextKind(kind);
        case CheckKind::Custom:
            return true;
    }
    return false;
}

bool FieldsExist(const ModelSchema& model, const std::vector<std::string>& fields)
{
    return std::all_of(fields.begin(), fields.end(), [&model](const std::string& field) {
        return FindField(model, field) != nullptr;
    });
}

bool HasRepeatedField(const std::vector<std::string>& fields)
{
    std::set<std::string> seen;
    for (const auto& field : fields)
    {
        if (!seen.insert(field).second)
        {
            return true;
        }
    }
    return false;
}

bool IsUniqueFieldSet(const ModelSchema& model, const std::vector<std::string>& fields)
{
    if (std::any_of(model.keys.begin(), model.keys.end(), [&fields](const KeySchema& key) {
        return key.fields == fields;
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const IndexSchema& index) {
        return index.unique && index.fields == fields;
    });
}

bool IsPrefix(const std::vector<std::string>& candidate, const std::vector<std::string>& fields)
{
    return candidate.size() >= fields.size() &&
        std::equal(fields.begin(), fields.end(), candidate.begin());
}

void HashByte(std::uint64_t& hash, unsigned char value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
}

void HashText(std::uint64_t& hash, std::string_view value)
{
    for (const unsigned char ch : value)
    {
        HashByte(hash, ch);
    }
    HashByte(hash, 0xff);
}

void HashBool(std::uint64_t& hash, bool value)
{
    HashByte(hash, value ? 1 : 0);
    HashByte(hash, 0xfe);
}

void HashStrings(std::uint64_t& hash, const std::vector<std::string>& values)
{
    HashText(hash, std::to_string(values.size()));
    for (const auto& value : values)
    {
        HashText(hash, value);
    }
}

void HashModel(std::uint64_t& hash, const ModelSchema& model)
{
    HashText(hash, model.modelId);
    HashText(hash, model.logicalName);
    HashText(hash, model.storageName);
    HashText(hash, model.ownerCapsule);
    HashText(hash, model.domain);
    HashText(hash, model.description);
    HashText(hash, ModelKindName(model.kind));
    HashText(hash, RetentionPolicyName(model.retention));
    for (const auto& field : model.fields)
    {
        HashText(hash, field.name);
        HashText(hash, field.storageName);
        HashText(hash, FieldKindName(field.kind));
        if (!field.semanticType.empty())
        {
            HashText(hash, "semanticType");
            HashText(hash, field.semanticType);
            HashText(hash, field.semanticTypeKey);
            HashText(hash, std::to_string(field.semanticTypeId.value));
        }
        if (field.wireType != WireType::None)
        {
            HashText(hash, "wireType");
            HashText(hash, WireTypeName(field.wireType));
            HashText(hash, std::to_string(field.wireTypeId.value));
            HashText(hash, std::to_string(field.wireSize));
        }
        if (field.storageCodec != StorageCodec::None)
        {
            HashText(hash, "storageCodec");
            HashText(hash, StorageCodecName(field.storageCodec));
            HashText(hash, std::to_string(field.storageCodecId.value));
            HashText(hash, std::to_string(field.storageSize));
        }
        if (field.kind == FieldKind::Decimal || field.decimalPrecision != 0 || field.decimalScale != 0)
        {
            HashText(hash, "decimalContract");
            HashText(hash, std::to_string(field.decimalPrecision));
            HashText(hash, std::to_string(field.decimalScale));
        }
        HashBool(hash, field.nullable);
        HashBool(hash, field.redacted);
        HashText(hash, field.sourceField);
        HashBool(hash, field.immutable);
        HashBool(hash, field.writeOnce);
        HashText(hash, NormalizationKindName(field.normalization));
        HashText(hash, field.importTransform);
        HashText(hash, DefaultKindName(field.defaultValue.kind));
        HashText(hash, field.defaultValue.value);
        HashStrings(hash, field.enumValues);
        for (const auto value_id : field.enumValueIds)
        {
            HashText(hash, std::to_string(value_id));
        }
        for (const auto& check : field.checks)
        {
            HashText(hash, check.checkId);
            HashText(hash, CheckKindName(check.kind));
            if (check.unit != CheckUnit::None)
            {
                HashText(hash, CheckUnitName(check.unit));
            }
            HashText(hash, check.value);
            HashText(hash, check.message);
        }
        for (const auto query_operator : field.query.filterOperators)
        {
            HashText(hash, QueryOperatorName(query_operator));
        }
        HashBool(hash, field.query.sortable);
        HashBool(hash, field.query.cursor);
    }
    for (const auto& key : model.keys)
    {
        HashText(hash, key.keyId);
        HashText(hash, KeyKindName(key.kind));
        HashStrings(hash, key.fields);
    }
    for (const auto& index : model.indexes)
    {
        HashText(hash, index.indexId);
        HashStrings(hash, index.fields);
        HashBool(hash, index.unique);
    }
    for (const auto& relation : model.relations)
    {
        HashText(hash, relation.relationId);
        HashText(hash, relation.targetModel);
        HashStrings(hash, relation.sourceFields);
        HashStrings(hash, relation.targetFields);
        HashText(hash, RelationCardinalityName(relation.cardinality));
        HashText(hash, ReferentialActionName(relation.onDelete));
        HashText(hash, ReferentialActionName(relation.onUpdate));
    }
    for (const auto& join : model.joins)
    {
        HashText(hash, join.joinId);
        HashText(hash, join.throughModel);
        HashText(hash, join.sourceRelation);
        HashText(hash, join.targetRelation);
    }
    for (const auto& one_of : model.oneOfs)
    {
        HashText(hash, one_of.oneOfId);
        HashText(hash, one_of.discriminatorField);
        for (const auto& variant : one_of.variants)
        {
            HashText(hash, variant.tag);
            if (variant.tagValueId != 0)
            {
                HashText(hash, std::to_string(variant.tagValueId));
            }
            HashStrings(hash, variant.fields);
            HashStrings(hash, variant.optionalFields);
        }
    }
}

json QueryPolicyJson(const QueryPolicy& query)
{
    json operators = json::array();
    for (const auto query_operator : query.filterOperators)
    {
        operators.push_back(std::string(QueryOperatorName(query_operator)));
    }
    return {
        {"filterOperators", std::move(operators)},
        {"sortable", query.sortable},
        {"cursor", query.cursor},
    };
}

json FieldJson(const FieldSchema& field)
{
    json checks = json::array();
    for (const auto& check : field.checks)
    {
        checks.push_back({
            {"checkId", check.checkId},
            {"kind", std::string(CheckKindName(check.kind))},
            {"unit", std::string(CheckUnitName(check.unit))},
            {"value", check.value},
            {"message", check.message},
        });
    }
    return {
        {"fieldId", field.fieldId},
        {"registryKey", field.registryKey},
        {"name", field.name},
        {"storageName", field.storageName},
        {"kind", std::string(FieldKindName(field.kind))},
        {"semanticType", field.semanticType},
        {"semanticTypeKey", field.semanticTypeKey},
        {"semanticTypeId", field.semanticTypeId.value},
        {"wireType", std::string(WireTypeName(field.wireType))},
        {"wireTypeId", field.wireTypeId.value},
        {"wireSize", field.wireSize},
        {"storageCodec", std::string(StorageCodecName(field.storageCodec))},
        {"storageCodecId", field.storageCodecId.value},
        {"storageSize", field.storageSize},
        {"decimalPrecision", field.decimalPrecision},
        {"decimalScale", field.decimalScale},
        {"nullable", field.nullable},
        {"redacted", field.redacted},
        {"sourceField", field.sourceField},
        {"immutable", field.immutable},
        {"writeOnce", field.writeOnce},
        {"normalization", std::string(NormalizationKindName(field.normalization))},
        {"importTransform", field.importTransform},
        {"default", {
            {"kind", std::string(DefaultKindName(field.defaultValue.kind))},
            {"value", field.defaultValue.value},
        }},
        {"enumValues", field.enumValues},
        {"enumValueIds", field.enumValueIds},
        {"checks", std::move(checks)},
        {"query", QueryPolicyJson(field.query)},
    };
}

json ModelJson(const ModelSchema& model)
{
    json fields = json::array();
    json keys = json::array();
    json indexes = json::array();
    json relations = json::array();
    json joins = json::array();
    json one_ofs = json::array();
    for (const auto& field : model.fields)
    {
        fields.push_back(FieldJson(field));
    }
    for (const auto& key : model.keys)
    {
        keys.push_back({
            {"keyId", key.keyId},
            {"kind", std::string(KeyKindName(key.kind))},
            {"fields", key.fields},
        });
    }
    for (const auto& index : model.indexes)
    {
        indexes.push_back({
            {"indexId", index.indexId},
            {"fields", index.fields},
            {"unique", index.unique},
        });
    }
    for (const auto& relation : model.relations)
    {
        relations.push_back({
            {"relationId", relation.relationId},
            {"targetModel", relation.targetModel},
            {"sourceFields", relation.sourceFields},
            {"targetFields", relation.targetFields},
            {"cardinality", std::string(RelationCardinalityName(relation.cardinality))},
            {"onDelete", std::string(ReferentialActionName(relation.onDelete))},
            {"onUpdate", std::string(ReferentialActionName(relation.onUpdate))},
        });
    }
    for (const auto& join : model.joins)
    {
        joins.push_back({
            {"joinId", join.joinId},
            {"throughModel", join.throughModel},
            {"sourceRelation", join.sourceRelation},
            {"targetRelation", join.targetRelation},
        });
    }
    for (const auto& one_of : model.oneOfs)
    {
        json variants = json::array();
        for (const auto& variant : one_of.variants)
        {
            variants.push_back({
                {"tag", variant.tag},
                {"tagValueId", variant.tagValueId},
                {"fields", variant.fields},
                {"optionalFields", variant.optionalFields},
            });
        }
        one_ofs.push_back({
            {"oneOfId", one_of.oneOfId},
            {"discriminatorField", one_of.discriminatorField},
            {"variants", std::move(variants)},
        });
    }
    return {
        {"typeId", model.typeId},
        {"registryKey", model.registryKey},
        {"modelId", model.modelId},
        {"qualifiedName", model.qualifiedName},
        {"logicalName", model.logicalName},
        {"storageName", model.storageName},
        {"ownerCapsule", model.ownerCapsule},
        {"mutationAuthority", model.mutationAuthority},
        {"domain", model.domain},
        {"description", model.description},
        {"kind", std::string(ModelKindName(model.kind))},
        {"role", std::string(ModelRoleName(model.role))},
        {"retention", std::string(RetentionPolicyName(model.retention))},
        {"sourceAliases", model.sourceAliases},
        {"retiredNames", model.retiredNames},
        {"nameReview", std::string(NameReviewStatusName(model.nameReview))},
        {"nameReviewRationale", model.nameReviewRationale},
        {"retiredFieldIds", model.retiredFieldIds},
        {"retiredFieldNames", model.retiredFieldNames},
        {"fields", std::move(fields)},
        {"keys", std::move(keys)},
        {"indexes", std::move(indexes)},
        {"relations", std::move(relations)},
        {"joins", std::move(joins)},
        {"oneOfs", std::move(one_ofs)},
    };
}

std::string NamingRegistryFingerprint(const SchemaCatalog& catalog)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    HashText(hash, "arken-schema-naming-registry-v1");
    for (const auto& model : catalog.models)
    {
        HashText(hash, model.registryKey);
        HashText(hash, std::to_string(model.typeId));
        HashText(hash, model.qualifiedName);
        HashText(hash, model.modelId);
        HashText(hash, model.logicalName);
        HashText(hash, model.storageName);
        HashText(hash, model.ownerCapsule);
        HashText(hash, model.mutationAuthority);
        HashText(hash, ModelRoleName(model.role));
        HashText(hash, NameReviewStatusName(model.nameReview));
        HashText(hash, model.nameReviewRationale);
        HashStrings(hash, model.sourceAliases);
        HashStrings(hash, model.retiredNames);
        for (const auto retired_field_id : model.retiredFieldIds)
        {
            HashText(hash, std::to_string(retired_field_id));
        }
        HashStrings(hash, model.retiredFieldNames);
        for (const auto& field : model.fields)
        {
            HashText(hash, field.registryKey);
            HashText(hash, std::to_string(field.fieldId));
            HashText(hash, field.name);
            HashText(hash, field.storageName);
        }
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}
} // namespace

bool ValidationResult::ok() const
{
    return errors.empty();
}

nlohmann::json ValidationResult::ToJson() const
{
    json error_rows = json::array();
    json warning_rows = json::array();
    for (const auto& error : errors)
    {
        error_rows.push_back(DiagnosticJson(error));
    }
    for (const auto& warning : warnings)
    {
        warning_rows.push_back(DiagnosticJson(warning));
    }
    return {
        {"ok", ok()},
        {"status", ok() ? "ready" : "invalid"},
        {"errorCount", errors.size()},
        {"warningCount", warnings.size()},
        {"errors", std::move(error_rows)},
        {"warnings", std::move(warning_rows)},
    };
}

std::string_view FieldKindName(FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::ObjectId: return "objectId";
        case FieldKind::InternalId: return "internalId";
        case FieldKind::Text: return "text";
        case FieldKind::Slug: return "slug";
        case FieldKind::DisplayName: return "displayName";
        case FieldKind::Url: return "url";
        case FieldKind::Email: return "email";
        case FieldKind::Enum: return "enum";
        case FieldKind::Bool: return "bool";
        case FieldKind::TimestampMs: return "timestampMs";
        case FieldKind::JsonText: return "jsonText";
        case FieldKind::Secret: return "secret";
        case FieldKind::EthereumAddress: return "ethereumAddress";
        case FieldKind::HashHex: return "hashHex";
        case FieldKind::TokenHash: return "tokenHash";
        case FieldKind::KeyReference: return "keyReference";
        case FieldKind::Integer: return "integer";
        case FieldKind::Real: return "real";
        case FieldKind::Decimal: return "decimal";
        case FieldKind::Blob: return "blob";
    }
    return "text";
}

std::string_view WireTypeName(WireType type)
{
    switch (type)
    {
        case WireType::None: return "none";
        case WireType::Bool: return "bool";
        case WireType::U8: return "u8";
        case WireType::U16: return "u16";
        case WireType::U32: return "u32";
        case WireType::U64: return "u64";
        case WireType::I8: return "i8";
        case WireType::I16: return "i16";
        case WireType::I32: return "i32";
        case WireType::I64: return "i64";
        case WireType::F32: return "f32";
        case WireType::F64: return "f64";
        case WireType::Utf8: return "utf8";
        case WireType::Bytes: return "bytes";
        case WireType::FixedBytes: return "fixedBytes";
        case WireType::Decimal128: return "decimal128";
    }
    return "none";
}

std::string_view StorageCodecName(StorageCodec codec)
{
    switch (codec)
    {
        case StorageCodec::None: return "none";
        case StorageCodec::SqliteInteger: return "sqliteInteger";
        case StorageCodec::SqliteReal: return "sqliteReal";
        case StorageCodec::SqliteText: return "sqliteText";
        case StorageCodec::SqliteBlob: return "sqliteBlob";
        case StorageCodec::SqliteFixedBlob: return "sqliteFixedBlob";
        case StorageCodec::SqliteOrderedU64: return "sqliteOrderedU64";
        case StorageCodec::SqliteOrderedDecimal128: return "sqliteOrderedDecimal128";
    }
    return "none";
}

std::string_view NormalizationKindName(NormalizationKind kind)
{
    switch (kind)
    {
        case NormalizationKind::None: return "none";
        case NormalizationKind::Trim: return "trim";
        case NormalizationKind::Lowercase: return "lowercase";
        case NormalizationKind::LowercaseTrim: return "lowercaseTrim";
        case NormalizationKind::EthereumAddress: return "ethereumAddress";
        case NormalizationKind::Url: return "url";
    }
    return "none";
}

std::string_view ModelKindName(ModelKind kind)
{
    switch (kind)
    {
        case ModelKind::Entity: return "entity";
        case ModelKind::Join: return "join";
        case ModelKind::Credential: return "credential";
        case ModelKind::Ephemeral: return "ephemeral";
        case ModelKind::AuditLedger: return "auditLedger";
        case ModelKind::Provenance: return "provenance";
        case ModelKind::ImportLedger: return "importLedger";
    }
    return "entity";
}

std::string_view ModelRoleName(ModelRole role)
{
    switch (role)
    {
        case ModelRole::Unspecified: return "unspecified";
        case ModelRole::UniversalSpine: return "universalSpine";
        case ModelRole::DomainFacet: return "domainFacet";
        case ModelRole::Definition: return "definition";
        case ModelRole::Revision: return "revision";
        case ModelRole::Instance: return "instance";
        case ModelRole::Occurrence: return "occurrence";
        case ModelRole::Manifestation: return "manifestation";
        case ModelRole::Representation: return "representation";
        case ModelRole::Assertion: return "assertion";
        case ModelRole::Observation: return "observation";
        case ModelRole::AuthoritativeAggregate: return "authoritativeAggregate";
        case ModelRole::Ledger: return "ledger";
        case ModelRole::Registry: return "registry";
        case ModelRole::Projection: return "projection";
        case ModelRole::ImportRecord: return "importRecord";
        case ModelRole::Receipt: return "receipt";
        case ModelRole::Membership: return "membership";
        case ModelRole::ValueObject: return "valueObject";
    }
    return "unspecified";
}

std::string_view NameReviewStatusName(NameReviewStatus status)
{
    switch (status)
    {
        case NameReviewStatus::Unspecified: return "unspecified";
        case NameReviewStatus::Canonical: return "canonical";
        case NameReviewStatus::ReviewRequired: return "reviewRequired";
        case NameReviewStatus::ApprovedException: return "approvedException";
    }
    return "unspecified";
}

std::string_view RetentionPolicyName(RetentionPolicy policy)
{
    switch (policy)
    {
        case RetentionPolicy::Durable: return "durable";
        case RetentionPolicy::UntilExpiry: return "untilExpiry";
        case RetentionPolicy::Immutable: return "immutable";
        case RetentionPolicy::UntilPromotion: return "untilPromotion";
    }
    return "durable";
}

std::string_view DefaultKindName(DefaultKind kind)
{
    switch (kind)
    {
        case DefaultKind::None: return "none";
        case DefaultKind::Literal: return "literal";
        case DefaultKind::BoundaryGenerated: return "boundaryGenerated";
    }
    return "none";
}

std::string_view CheckKindName(CheckKind kind)
{
    switch (kind)
    {
        case CheckKind::MinimumInteger: return "minimumInteger";
        case CheckKind::MaximumInteger: return "maximumInteger";
        case CheckKind::MinimumReal: return "minimumReal";
        case CheckKind::MaximumReal: return "maximumReal";
        case CheckKind::MinimumLength: return "minimumLength";
        case CheckKind::MaximumLength: return "maximumLength";
        case CheckKind::FixedLength: return "fixedLength";
        case CheckKind::Pattern: return "pattern";
        case CheckKind::Custom: return "custom";
    }
    return "custom";
}

std::string_view CheckUnitName(CheckUnit unit)
{
    switch (unit)
    {
        case CheckUnit::None: return "none";
        case CheckUnit::Utf8Bytes: return "utf8Bytes";
        case CheckUnit::Bytes: return "bytes";
    }
    return "none";
}

std::string_view KeyKindName(KeyKind kind)
{
    switch (kind)
    {
        case KeyKind::Primary: return "primary";
        case KeyKind::Alternate: return "alternate";
    }
    return "alternate";
}

std::string_view RelationCardinalityName(RelationCardinality cardinality)
{
    switch (cardinality)
    {
        case RelationCardinality::One: return "one";
        case RelationCardinality::OptionalOne: return "optionalOne";
        case RelationCardinality::Many: return "many";
    }
    return "one";
}

std::string_view ReferentialActionName(ReferentialAction action)
{
    switch (action)
    {
        case ReferentialAction::Restrict: return "restrict";
        case ReferentialAction::Cascade: return "cascade";
        case ReferentialAction::SetNull: return "setNull";
    }
    return "restrict";
}

std::string_view QueryOperatorName(QueryOperator query_operator)
{
    switch (query_operator)
    {
        case QueryOperator::Equal: return "equal";
        case QueryOperator::NotEqual: return "notEqual";
        case QueryOperator::In: return "in";
        case QueryOperator::NotIn: return "notIn";
        case QueryOperator::LessThan: return "lessThan";
        case QueryOperator::LessThanOrEqual: return "lessThanOrEqual";
        case QueryOperator::GreaterThan: return "greaterThan";
        case QueryOperator::GreaterThanOrEqual: return "greaterThanOrEqual";
        case QueryOperator::Prefix: return "prefix";
        case QueryOperator::Suffix: return "suffix";
        case QueryOperator::Contains: return "contains";
        case QueryOperator::IsNull: return "isNull";
    }
    return "equal";
}

TypeRegistry BuildTypeRegistry(const SchemaCatalog& catalog)
{
    TypeRegistry registry;
    std::map<std::string, SemanticTypeDefinition> semantic_types;
    using BindingKey = std::tuple<std::uint32_t, FieldKind, std::uint32_t, std::uint32_t, int, int, int, int>;
    std::map<BindingKey, TypeBindingDefinition> bindings;
    for (const auto& model : catalog.models)
    {
        for (const auto& field : model.fields)
        {
            if (field.semanticType.empty()) continue;
            auto [found, inserted] = semantic_types.try_emplace(field.semanticTypeKey, SemanticTypeDefinition{
                .typeId = field.semanticTypeId,
                .registryKey = field.semanticTypeKey,
                .name = field.semanticType,
                .fieldCount = 0,
            });
            static_cast<void>(inserted);
            ++found->second.fieldCount;
            const BindingKey binding_key{
                field.semanticTypeId.value,
                field.kind,
                field.wireTypeId.value,
                field.storageCodecId.value,
                field.wireSize,
                field.storageSize,
                field.decimalPrecision,
                field.decimalScale,
            };
            auto [binding, binding_inserted] = bindings.try_emplace(binding_key, TypeBindingDefinition{
                .semanticTypeId = field.semanticTypeId,
                .fieldKind = field.kind,
                .wireTypeId = field.wireTypeId,
                .storageCodecId = field.storageCodecId,
                .wireSize = field.wireSize,
                .storageSize = field.storageSize,
                .decimalPrecision = field.decimalPrecision,
                .decimalScale = field.decimalScale,
                .fieldCount = 0,
            });
            static_cast<void>(binding_inserted);
            ++binding->second.fieldCount;
        }
    }
    for (auto& [key, definition] : semantic_types)
    {
        static_cast<void>(key);
        registry.semanticTypes.push_back(std::move(definition));
    }
    for (auto& [key, definition] : bindings)
    {
        static_cast<void>(key);
        registry.bindings.push_back(std::move(definition));
    }
    registry.wireTypes = {
        {{1}, WireType::Bool, 1, false, true},
        {{2}, WireType::U8, 8, false, true},
        {{3}, WireType::U16, 16, false, true},
        {{4}, WireType::U32, 32, false, true},
        {{5}, WireType::U64, 64, false, true},
        {{6}, WireType::I8, 8, true, true},
        {{7}, WireType::I16, 16, true, true},
        {{8}, WireType::I32, 32, true, true},
        {{9}, WireType::I64, 64, true, true},
        {{10}, WireType::F32, 32, true, true},
        {{11}, WireType::F64, 64, true, true},
        {{12}, WireType::Utf8, 0, false, false},
        {{13}, WireType::Bytes, 0, false, false},
        {{14}, WireType::FixedBytes, 0, false, true},
        {{15}, WireType::Decimal128, 128, true, true},
    };
    registry.storageCodecs = {
        {{1}, StorageCodec::SqliteInteger, "INTEGER", 0, true, true},
        {{2}, StorageCodec::SqliteReal, "REAL", 0, true, true},
        {{3}, StorageCodec::SqliteText, "TEXT", 0, true, true},
        {{4}, StorageCodec::SqliteBlob, "BLOB", 0, true, false},
        {{5}, StorageCodec::SqliteFixedBlob, "BLOB", 0, true, false},
        {{6}, StorageCodec::SqliteOrderedU64, "BLOB", 8, true, true},
        {{7}, StorageCodec::SqliteOrderedDecimal128, "BLOB", 16, true, true},
    };
    return registry;
}

std::string FingerprintTypeRegistry(const SchemaCatalog& catalog)
{
    const auto registry = BuildTypeRegistry(catalog);
    std::uint64_t hash = UINT64_C(14695981039346656037);
    HashText(hash, "arken-schema-type-registry-v1");
    for (const auto& type : registry.semanticTypes)
    {
        HashText(hash, std::to_string(type.typeId.value));
        HashText(hash, type.registryKey);
        HashText(hash, type.name);
    }
    for (const auto& binding : registry.bindings)
    {
        HashText(hash, std::to_string(binding.semanticTypeId.value));
        HashText(hash, FieldKindName(binding.fieldKind));
        HashText(hash, std::to_string(binding.wireTypeId.value));
        HashText(hash, std::to_string(binding.storageCodecId.value));
        HashText(hash, std::to_string(binding.wireSize));
        HashText(hash, std::to_string(binding.storageSize));
        HashText(hash, std::to_string(binding.decimalPrecision));
        HashText(hash, std::to_string(binding.decimalScale));
    }
    for (const auto& type : registry.wireTypes)
    {
        HashText(hash, std::to_string(type.typeId.value));
        HashText(hash, WireTypeName(type.type));
        HashText(hash, std::to_string(type.bitWidth));
        HashBool(hash, type.signedInteger);
        HashBool(hash, type.fixedWidth);
    }
    for (const auto& codec : registry.storageCodecs)
    {
        HashText(hash, std::to_string(codec.codecId.value));
        HashText(hash, StorageCodecName(codec.codec));
        HashText(hash, codec.sqliteStorageClass);
        HashText(hash, std::to_string(codec.fixedSize));
        HashBool(hash, codec.exact);
        HashBool(hash, codec.orderPreserving);
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

nlohmann::json BuildTypeRegistryManifest(const SchemaCatalog& catalog)
{
    const auto registry = BuildTypeRegistry(catalog);
    json semantic_types = json::array();
    json bindings = json::array();
    json wire_types = json::array();
    json storage_codecs = json::array();
    for (const auto& type : registry.semanticTypes)
    {
        semantic_types.push_back({
            {"typeId", type.typeId.value},
            {"registryKey", type.registryKey},
            {"name", type.name},
            {"fieldCount", type.fieldCount},
        });
    }
    for (const auto& binding : registry.bindings)
    {
        bindings.push_back({
            {"semanticTypeId", binding.semanticTypeId.value},
            {"fieldKind", std::string(FieldKindName(binding.fieldKind))},
            {"wireTypeId", binding.wireTypeId.value},
            {"storageCodecId", binding.storageCodecId.value},
            {"wireSize", binding.wireSize},
            {"storageSize", binding.storageSize},
            {"decimalPrecision", binding.decimalPrecision},
            {"decimalScale", binding.decimalScale},
            {"fieldCount", binding.fieldCount},
        });
    }
    for (const auto& type : registry.wireTypes)
    {
        wire_types.push_back({
            {"typeId", type.typeId.value},
            {"name", std::string(WireTypeName(type.type))},
            {"bitWidth", type.bitWidth},
            {"signedInteger", type.signedInteger},
            {"fixedWidth", type.fixedWidth},
        });
    }
    for (const auto& codec : registry.storageCodecs)
    {
        storage_codecs.push_back({
            {"codecId", codec.codecId.value},
            {"name", std::string(StorageCodecName(codec.codec))},
            {"sqliteStorageClass", codec.sqliteStorageClass},
            {"fixedSize", codec.fixedSize},
            {"exact", codec.exact},
            {"orderPreserving", codec.orderPreserving},
        });
    }
    return {
        {"mode", "arken-schema-type-registry"},
        {"version", registry.version},
        {"idType", "u32"},
        {"allocation", "persisted-u32-registry-v1"},
        {"fingerprint", FingerprintTypeRegistry(catalog)},
        {"semanticTypeCount", semantic_types.size()},
        {"bindingCount", bindings.size()},
        {"wireTypeCount", wire_types.size()},
        {"storageCodecCount", storage_codecs.size()},
        {"semanticTypes", std::move(semantic_types)},
        {"bindings", std::move(bindings)},
        {"wireTypes", std::move(wire_types)},
        {"storageCodecs", std::move(storage_codecs)},
        {"retiredSemanticTypeIds", registry.retiredSemanticTypeIds},
        {"retiredWireTypeIds", registry.retiredWireTypeIds},
        {"retiredStorageCodecIds", registry.retiredStorageCodecIds},
        {"reservedWireTypeRange", {{"first", 16}, {"last", 31}}},
        {"reservedStorageCodecRange", {{"first", 8}, {"last", 31}}},
    };
}

SchemaCatalog ComposeCatalog(std::string catalog_id, int version, std::vector<SchemaFragment> fragments)
{
    std::sort(fragments.begin(), fragments.end(), [](const SchemaFragment& lhs, const SchemaFragment& rhs) {
        return std::tie(lhs.fragmentId, lhs.ownerCapsule, lhs.version) <
            std::tie(rhs.fragmentId, rhs.ownerCapsule, rhs.version);
    });
    SchemaCatalog catalog{
        .catalogId = std::move(catalog_id),
        .version = version,
        .fragments = std::move(fragments),
    };
    for (auto& fragment : catalog.fragments)
    {
        for (auto& model : fragment.models)
        {
            NormalizeModelRegistry(model);
        }
        std::sort(fragment.models.begin(), fragment.models.end(), [](const ModelSchema& lhs, const ModelSchema& rhs) {
            return std::tie(lhs.modelId, lhs.logicalName, lhs.storageName) <
                std::tie(rhs.modelId, rhs.logicalName, rhs.storageName);
        });
        catalog.models.insert(catalog.models.end(), fragment.models.begin(), fragment.models.end());
    }
    return catalog;
}

const ModelSchema* FindModelById(const SchemaCatalog& catalog, std::string_view model_id)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [model_id](const ModelSchema& model) {
        return model.modelId == model_id;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const ModelSchema* FindModelByLogicalName(const SchemaCatalog& catalog, std::string_view logical_name)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [logical_name](const ModelSchema& model) {
        return model.logicalName == logical_name;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const ModelSchema* FindModelByStorageName(const SchemaCatalog& catalog, std::string_view storage_name)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [storage_name](const ModelSchema& model) {
        return model.storageName == storage_name;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const FieldSchema* FindField(const ModelSchema& model, std::string_view field_name)
{
    const auto it = std::find_if(model.fields.begin(), model.fields.end(), [field_name](const FieldSchema& field) {
        return field.name == field_name;
    });
    return it == model.fields.end() ? nullptr : &*it;
}

const RelationSchema* FindRelationById(const ModelSchema& model, std::string_view relation_id)
{
    const auto it = std::find_if(model.relations.begin(), model.relations.end(), [relation_id](const RelationSchema& relation) {
        return relation.relationId == relation_id;
    });
    return it == model.relations.end() ? nullptr : &*it;
}

const JoinSchema* FindJoinById(const ModelSchema& model, std::string_view join_id)
{
    const auto it = std::find_if(model.joins.begin(), model.joins.end(), [join_id](const JoinSchema& join) {
        return join.joinId == join_id;
    });
    return it == model.joins.end() ? nullptr : &*it;
}

bool HasIndexPrefix(const ModelSchema& model, const std::vector<std::string>& fields)
{
    if (fields.empty())
    {
        return false;
    }
    if (std::any_of(model.keys.begin(), model.keys.end(), [&fields](const KeySchema& key) {
        return IsPrefix(key.fields, fields);
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const IndexSchema& index) {
        return IsPrefix(index.fields, fields);
    });
}

bool IsSingleFieldUnique(const ModelSchema& model, std::string_view field_name)
{
    if (std::any_of(model.keys.begin(), model.keys.end(), [field_name](const KeySchema& key) {
        return key.fields.size() == 1 && key.fields.front() == field_name;
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [field_name](const IndexSchema& index) {
        return index.unique && index.fields.size() == 1 && index.fields.front() == field_name;
    });
}

ValidationResult ValidateCatalog(const SchemaCatalog& catalog)
{
    ValidationResult result;
    if (catalog.catalogId.empty())
    {
        AddError(result, "catalog_id_required", {}, "catalogId", "Catalog id is required.");
    }
    if (catalog.version < 1)
    {
        AddError(result, "catalog_version_invalid", {}, "version", "Catalog version must be positive.");
    }
    if (catalog.fragments.empty())
    {
        AddError(result, "catalog_fragments_required", {}, "fragments", "At least one schema fragment is required.");
    }

    std::set<std::string> fragment_ids;
    std::size_t fragment_model_count = 0;
    for (const auto& fragment : catalog.fragments)
    {
        if (fragment.fragmentId.empty() || fragment.ownerCapsule.empty())
        {
            AddError(result, "fragment_identity_required", {}, "fragments", "Fragment id and owner capsule are required.");
        }
        if (!fragment_ids.insert(fragment.fragmentId).second)
        {
            AddError(result, "duplicate_fragment_id", {}, "fragments." + fragment.fragmentId, "Fragment id must be unique.");
        }
        if (fragment.version < 1)
        {
            AddError(result, "fragment_version_invalid", {}, "fragments." + fragment.fragmentId, "Fragment version must be positive.");
        }
        for (const auto& model : fragment.models)
        {
            ++fragment_model_count;
            if (!fragment.ownerCapsule.empty() && model.ownerCapsule != fragment.ownerCapsule)
            {
                AddError(result, "model_owner_mismatch", model.modelId, "ownerCapsule", "Model owner must match its fragment owner.");
            }
            const ModelSchema* composed_model = FindModelById(catalog, model.modelId);
            if (composed_model == nullptr || ModelJson(*composed_model) != ModelJson(model))
            {
                AddError(result, "catalog_fragment_model_mismatch", model.modelId, "models", "Composed models must exactly match their source fragments.");
            }
        }
    }
    if (fragment_model_count != catalog.models.size())
    {
        AddError(result, "catalog_model_count_mismatch", {}, "models", "Composed model count must equal the sum of fragment models.");
    }

    std::set<std::string> model_ids;
    std::set<std::string> logical_names;
    std::set<std::string> storage_names;
    std::set<std::string> qualified_names;
    std::set<std::string> model_registry_keys;
    std::set<std::uint32_t> type_ids;
    std::set<std::string> active_model_names;
    std::set<std::string> retired_model_names;
    std::map<std::uint32_t, std::string> semantic_type_ids;
    std::map<std::string, std::uint32_t> semantic_type_registry_ids;
    std::map<std::string, std::string> semantic_type_names;
    for (const auto& model : catalog.models)
    {
        if (model.registryKey.empty() || model.modelId.empty() || model.logicalName.empty() || model.storageName.empty() ||
            model.ownerCapsule.empty() || model.domain.empty() || model.qualifiedName.empty() ||
            model.mutationAuthority.empty() || model.typeId == 0 || model.role == ModelRole::Unspecified)
        {
            AddError(
                result,
                "model_identity_required",
                model.modelId,
                "model",
                "Model registry key, type id, names, owner capsule, mutation authority, domain, and semantic role are required."
            );
        }
        if (!model_registry_keys.insert(model.registryKey).second)
        {
            AddError(result, "duplicate_model_registry_key", model.modelId, "registryKey", "Model registry keys must be globally unique and durable across renames.");
        }
        if (!type_ids.insert(model.typeId).second)
        {
            AddError(result, "duplicate_model_type_id", model.modelId, "typeId", "Model type id must be globally unique.");
        }
        if (!model_ids.insert(model.modelId).second)
        {
            AddError(result, "duplicate_model_id", model.modelId, "modelId", "Model id must be unique.");
        }
        if (!logical_names.insert(model.logicalName).second)
        {
            AddError(result, "duplicate_logical_name", model.modelId, "logicalName", "Logical model name must be unique.");
        }
        if (!storage_names.insert(model.storageName).second)
        {
            AddError(result, "duplicate_storage_name", model.modelId, "storageName", "Storage name must be unique.");
        }
        if (!qualified_names.insert(model.qualifiedName).second)
        {
            AddError(result, "duplicate_qualified_name", model.modelId, "qualifiedName", "Qualified model name must be unique.");
        }
        if (model.qualifiedName.find('.') == std::string::npos)
        {
            AddError(result, "qualified_name_invalid", model.modelId, "qualifiedName", "Qualified model names require a domain namespace.");
        }
        if (HasForbiddenProductPrefix(model.storageName) &&
            (model.role != ModelRole::ImportRecord || model.nameReview != NameReviewStatus::ApprovedException))
        {
            AddError(result, "product_prefixed_storage_name", model.modelId, "storageName", "Product or legacy prefixes are reserved for explicitly approved import evidence.");
        }
        if (model.storageName == "sets" || model.logicalName == "Set" || model.logicalName == "sets")
        {
            AddError(result, "unqualified_set_model_name", model.modelId, "storageName", "Unqualified Set/sets is not a canonical domain concept.");
        }
        if (IsAmbiguousStorageName(model.storageName) && model.nameReview == NameReviewStatus::Canonical)
        {
            AddError(result, "ambiguous_name_not_reviewed", model.modelId, "nameReview", "Ambiguous storage names require review or an approved exception.");
        }
        if (model.nameReview == NameReviewStatus::ApprovedException && model.nameReviewRationale.empty())
        {
            AddError(result, "name_exception_rationale_required", model.modelId, "nameReviewRationale", "Approved naming exceptions require a rationale.");
        }
        if (model.nameReview == NameReviewStatus::ReviewRequired)
        {
            AddWarning(result, "ambiguous_name_review_required", model.modelId, "storageName", "Canonical naming review remains required for this model.");
        }
        if (HasDuplicate(model.sourceAliases, [](const std::string& value) -> std::string_view { return value; }) ||
            HasDuplicate(model.retiredNames, [](const std::string& value) -> std::string_view { return value; }))
        {
            AddError(result, "duplicate_model_name_alias", model.modelId, "sourceAliases", "Source aliases and retired names must be unique within their lists.");
        }
        if (std::any_of(model.sourceAliases.begin(), model.sourceAliases.end(), [](const std::string& value) { return value.empty(); }) ||
            std::any_of(model.retiredNames.begin(), model.retiredNames.end(), [](const std::string& value) { return value.empty(); }))
        {
            AddError(result, "empty_model_name_alias", model.modelId, "sourceAliases", "Source aliases and retired names cannot be empty.");
        }
        active_model_names.insert(model.modelId);
        active_model_names.insert(model.logicalName);
        active_model_names.insert(model.storageName);
        active_model_names.insert(model.qualifiedName);
        retired_model_names.insert(model.retiredNames.begin(), model.retiredNames.end());
        if (model.fields.empty())
        {
            AddError(result, "model_fields_required", model.modelId, "fields", "At least one field is required.");
        }
        if (model.kind == ModelKind::Ephemeral && model.retention != RetentionPolicy::UntilExpiry)
        {
            AddError(result, "ephemeral_retention_invalid", model.modelId, "retention", "Ephemeral models must expire explicitly.");
        }
        if (model.kind == ModelKind::AuditLedger && model.retention != RetentionPolicy::Immutable)
        {
            AddError(result, "audit_retention_invalid", model.modelId, "retention", "Audit ledgers must be immutable.");
        }
        if (model.kind == ModelKind::Provenance && model.retention != RetentionPolicy::Immutable)
        {
            AddError(result, "provenance_retention_invalid", model.modelId, "retention", "Provenance rows must be immutable.");
        }
        if (model.kind == ModelKind::ImportLedger &&
            model.retention != RetentionPolicy::Immutable &&
            model.retention != RetentionPolicy::UntilPromotion)
        {
            AddError(result, "import_retention_invalid", model.modelId, "retention", "Import ledgers must be immutable or retained through promotion.");
        }

        std::set<std::string> field_names;
        std::set<std::string> field_storage_names;
        std::set<std::uint32_t> field_ids;
        std::set<std::string> field_registry_keys;
        for (const auto& field : model.fields)
        {
            const std::string path = "fields." + field.name;
            if (field.fieldId == 0)
            {
                AddError(result, "field_id_required", model.modelId, path, "Every field requires a nonzero stable u32 field id.");
            }
            else if (!field_ids.insert(field.fieldId).second)
            {
                AddError(result, "duplicate_field_id", model.modelId, path, "Field ids must be unique within a model.");
            }
            if (field.registryKey.empty())
            {
                AddError(result, "field_registry_key_required", model.modelId, path, "Every field requires a durable registry key independent of its canonical name.");
            }
            else if (!field_registry_keys.insert(field.registryKey).second)
            {
                AddError(result, "duplicate_field_registry_key", model.modelId, path, "Field registry keys must be unique within a model.");
            }
            if (field.name.empty() || field.storageName.empty())
            {
                AddError(result, "field_identity_required", model.modelId, path, "Field name and storage name are required.");
            }
            if (!field_names.insert(field.name).second)
            {
                AddError(result, "duplicate_field_name", model.modelId, path, "Field name must be unique within a model.");
            }
            if (!field_storage_names.insert(field.storageName).second)
            {
                AddError(result, "duplicate_field_storage_name", model.modelId, path, "Field storage name must be unique within a model.");
            }
            if (!IsWireTypeCompatible(field.kind, field.wireType))
            {
                AddError(result, "wire_type_kind_mismatch", model.modelId, path, "Wire type is incompatible with the semantic field kind.");
            }
            if (!IsStorageCodecCompatible(field.kind, field.storageCodec))
            {
                AddError(result, "storage_codec_kind_mismatch", model.modelId, path, "Storage codec is incompatible with the semantic field kind.");
            }
            if ((!field.semanticType.empty() || field.storageCodec != StorageCodec::None) &&
                (field.semanticType.empty() || field.wireType == WireType::None || field.storageCodec == StorageCodec::None))
            {
                AddError(result, "strict_type_contract_incomplete", model.modelId, path, "Semantic type, wire type, and storage codec must be declared together.");
            }
            const bool strict_type_contract = !field.semanticType.empty() && field.wireType != WireType::None &&
                field.storageCodec != StorageCodec::None;
            if (!strict_type_contract && (!field.semanticTypeKey.empty() || field.semanticTypeId.value != 0 ||
                field.wireTypeId.value != 0 || field.storageCodecId.value != 0))
            {
                AddError(result, "type_registry_id_without_contract", model.modelId, path, "Type registry keys and ids require a complete strict type contract.");
            }
            if (strict_type_contract)
            {
                if (field.semanticTypeKey.empty() || field.semanticTypeId.value == 0 || field.wireTypeId.value == 0 ||
                    field.storageCodecId.value == 0)
                {
                    AddError(result, "type_registry_identity_required", model.modelId, path, "Complete type contracts require nonzero semantic, wire, and storage registry identities.");
                }
                if (field.wireTypeId != WireTypeRegistryId(field.wireType))
                {
                    AddError(result, "wire_type_id_mismatch", model.modelId, path, "Wire type id does not match the canonical wire registry.");
                }
                if (field.storageCodecId != StorageCodecRegistryId(field.storageCodec))
                {
                    AddError(result, "storage_codec_id_mismatch", model.modelId, path, "Storage codec id does not match the canonical storage registry.");
                }
                const auto prior_id = semantic_type_ids.emplace(field.semanticTypeId.value, field.semanticTypeKey);
                if (!prior_id.second && prior_id.first->second != field.semanticTypeKey)
                {
                    AddError(result, "semantic_type_id_collision", model.modelId, path, "Semantic type ids must be globally unique across durable registry keys.");
                }
                const auto prior_registry_id = semantic_type_registry_ids.emplace(
                    field.semanticTypeKey, field.semanticTypeId.value
                );
                if (!prior_registry_id.second &&
                    prior_registry_id.first->second != field.semanticTypeId.value)
                {
                    AddError(result, "semantic_type_key_id_conflict", model.modelId, path, "One semantic registry key cannot resolve to multiple numeric ids.");
                }
                const auto prior_name = semantic_type_names.emplace(field.semanticTypeKey, field.semanticType);
                if (!prior_name.second && prior_name.first->second != field.semanticType)
                {
                    AddError(result, "semantic_type_name_conflict", model.modelId, path, "One semantic registry key cannot resolve to multiple semantic names.");
                }
            }
            if ((field.wireType == WireType::FixedBytes || field.wireType == WireType::Decimal128) && field.wireSize <= 0)
            {
                AddError(result, "fixed_wire_size_required", model.modelId, path, "Fixed-width wire fields require a positive byte width.");
            }
            if (field.wireType == WireType::FixedBytes &&
                ((field.kind == FieldKind::ObjectId && field.wireSize != 12) ||
                 (field.kind == FieldKind::EthereumAddress && field.wireSize != 20) ||
                 (field.kind == FieldKind::TokenHash && field.wireSize != 32)))
            {
                AddError(
                    result,
                    "semantic_wire_size_mismatch",
                    model.modelId,
                    path,
                    "Fixed-byte wire width does not match the semantic field codec."
                );
            }
            if (field.wireType == WireType::Decimal128 && field.wireSize != 16)
            {
                AddError(result, "decimal_wire_size_mismatch", model.modelId, path, "Decimal128 wire fields require a 16-byte coefficient.");
            }
            if (field.wireType != WireType::FixedBytes && field.wireType != WireType::Decimal128 && field.wireSize != 0)
            {
                AddError(result, "wire_size_not_applicable", model.modelId, path, "Only fixed-width wire fields declare a byte width.");
            }
            if (field.storageCodec == StorageCodec::SqliteFixedBlob && field.storageSize <= 0)
            {
                AddError(result, "fixed_storage_size_required", model.modelId, path, "Fixed-blob storage fields require a positive byte width.");
            }
            if (field.storageCodec == StorageCodec::SqliteFixedBlob &&
                (field.wireType != WireType::FixedBytes || field.storageSize != field.wireSize))
            {
                AddError(result, "fixed_storage_wire_size_mismatch", model.modelId, path, "Fixed wire and storage widths must match.");
            }
            if (field.storageCodec == StorageCodec::SqliteOrderedU64 && field.storageSize != 8)
            {
                AddError(result, "ordered_u64_storage_size_mismatch", model.modelId, path, "Ordered u64 storage requires exactly 8 bytes.");
            }
            if (field.storageCodec == StorageCodec::SqliteOrderedDecimal128 && field.storageSize != 16)
            {
                AddError(result, "ordered_decimal_storage_size_mismatch", model.modelId, path, "Ordered Decimal128 storage requires exactly 16 bytes.");
            }
            if (field.storageCodec != StorageCodec::SqliteFixedBlob &&
                field.storageCodec != StorageCodec::SqliteOrderedU64 &&
                field.storageCodec != StorageCodec::SqliteOrderedDecimal128 && field.storageSize != 0)
            {
                AddError(result, "storage_size_not_applicable", model.modelId, path, "Only fixed-width storage codecs declare a byte width.");
            }
            if ((field.wireType == WireType::U64) != (field.storageCodec == StorageCodec::SqliteOrderedU64))
            {
                AddError(result, "u64_storage_codec_mismatch", model.modelId, path, "Full-range u64 fields require the ordered eight-byte SQLite codec, and that codec is reserved for u64.");
            }
            if (field.kind == FieldKind::Decimal)
            {
                if (field.wireType != WireType::Decimal128 ||
                    field.storageCodec != StorageCodec::SqliteOrderedDecimal128 ||
                    field.decimalPrecision < 1 || field.decimalPrecision > 38 ||
                    field.decimalScale < 0 || field.decimalScale > field.decimalPrecision)
                {
                    AddError(result, "decimal_contract_invalid", model.modelId, path, "Decimal fields require Decimal128, ordered 16-byte storage, precision 1..38, and scale within precision.");
                }
            }
            else if (field.wireType == WireType::Decimal128 ||
                     field.storageCodec == StorageCodec::SqliteOrderedDecimal128 ||
                     field.decimalPrecision != 0 || field.decimalScale != 0)
            {
                AddError(result, "decimal_contract_on_non_decimal", model.modelId, path, "Decimal wire, storage, precision, and scale metadata are reserved for decimal fields.");
            }
            if (field.kind == FieldKind::Enum && field.enumValues.empty())
            {
                AddError(result, "enum_values_required", model.modelId, path, "Enum fields require a model-specific value domain.");
            }
            if (field.kind != FieldKind::Enum && !field.enumValues.empty())
            {
                AddError(result, "enum_values_on_non_enum", model.modelId, path, "Only enum fields may declare enum values.");
            }
            if (HasDuplicate(field.enumValues, [](const std::string& value) -> std::string_view { return value; }))
            {
                AddError(result, "duplicate_enum_value", model.modelId, path, "Enum values must be unique.");
            }
            if (std::any_of(field.enumValues.begin(), field.enumValues.end(), [](const std::string& value) { return value.empty(); }))
            {
                AddError(result, "empty_enum_value", model.modelId, path, "Enum values cannot be empty.");
            }
            if (!field.enumValueIds.empty() && field.enumValueIds.size() != field.enumValues.size())
            {
                AddError(result, "enum_value_id_count_mismatch", model.modelId, path, "Numeric enum value ids must align one-to-one with enum names.");
            }
            std::set<std::uint32_t> enum_value_ids;
            for (const auto value_id : field.enumValueIds)
            {
                if (value_id == 0 || !enum_value_ids.insert(value_id).second)
                {
                    AddError(result, "enum_value_id_invalid", model.modelId, path, "Numeric enum value ids must be unique and nonzero.");
                }
            }
            if (field.kind == FieldKind::Secret && !field.redacted)
            {
                AddError(result, "secret_field_not_redacted", model.modelId, path, "Secret fields must be redacted.");
            }
            if (field.kind == FieldKind::TokenHash && !field.immutable)
            {
                AddError(result, "token_hash_mutable", model.modelId, path, "Token hashes are immutable; rotation creates a new credential or token row.");
            }
            if (field.writeOnce && field.immutable)
            {
                AddError(result, "write_once_immutable_conflict", model.modelId, path, "A field cannot be both immutable and write-once.");
            }
            if (field.writeOnce && !field.nullable)
            {
                AddError(result, "write_once_field_not_nullable", model.modelId, path, "Write-once fields must begin nullable so one later value can be recorded.");
            }
            if (field.normalization == NormalizationKind::EthereumAddress && field.kind != FieldKind::EthereumAddress)
            {
                AddError(result, "ethereum_normalization_kind_mismatch", model.modelId, path, "Ethereum normalization requires an Ethereum-address field.");
            }
            if (field.normalization == NormalizationKind::Url && field.kind != FieldKind::Url)
            {
                AddError(result, "url_normalization_kind_mismatch", model.modelId, path, "URL normalization requires a URL field.");
            }
            if ((field.normalization == NormalizationKind::Lowercase ||
                 field.normalization == NormalizationKind::LowercaseTrim ||
                 field.normalization == NormalizationKind::Trim) &&
                !IsTextKind(field.kind))
            {
                AddError(result, "text_normalization_kind_mismatch", model.modelId, path, "Text normalization requires a semantic text field.");
            }
            if (field.redacted && (!field.query.filterOperators.empty() || field.query.sortable || field.query.cursor))
            {
                AddError(result, "redacted_field_queryable", model.modelId, path, "Redacted fields cannot expose query capabilities.");
            }
            if (field.defaultValue.kind == DefaultKind::None && !field.defaultValue.value.empty())
            {
                AddError(result, "default_value_without_kind", model.modelId, path, "Default value requires an explicit default kind.");
            }
            if (field.defaultValue.kind == DefaultKind::BoundaryGenerated && field.defaultValue.value.empty())
            {
                AddError(result, "boundary_default_source_required", model.modelId, path, "Boundary-generated defaults require a named source.");
            }
            if (!IsLiteralDefaultCompatible(field))
            {
                AddError(result, "literal_default_kind_mismatch", model.modelId, path, "Literal default must conform to the semantic field kind and enum domain.");
            }
            if (HasDuplicate(field.checks, [](const CheckSchema& check) -> std::string_view { return check.checkId; }))
            {
                AddError(result, "duplicate_check_id", model.modelId, path, "Field check ids must be unique.");
            }
            if (std::any_of(field.checks.begin(), field.checks.end(), [](const CheckSchema& check) { return check.checkId.empty(); }))
            {
                AddError(result, "check_id_required", model.modelId, path, "Every field check requires an id.");
            }
            for (const auto& check : field.checks)
            {
                if (!IsCheckCompatible(field.kind, check.kind))
                {
                    AddError(result, "check_kind_mismatch", model.modelId, path, "Field check is incompatible with the semantic field kind.");
                }
                const bool length_check = check.kind == CheckKind::MinimumLength ||
                    check.kind == CheckKind::MaximumLength || check.kind == CheckKind::FixedLength;
                if (length_check && field.kind == FieldKind::Blob && check.unit != CheckUnit::Bytes)
                {
                    AddError(result, "blob_check_unit_invalid", model.modelId, path, "Blob length checks require the bytes unit.");
                }
                else if (length_check && IsLengthCheckedTextKind(field.kind) && check.unit == CheckUnit::Bytes)
                {
                    AddError(result, "text_check_unit_invalid", model.modelId, path, "Text length checks use character count or UTF-8 byte count, not the opaque bytes unit.");
                }
                else if (!length_check && check.unit != CheckUnit::None)
                {
                    AddError(result, "non_length_check_unit_invalid", model.modelId, path, "Only length checks may declare a unit.");
                }
            }
            std::set<QueryOperator> query_operators;
            for (const auto query_operator : field.query.filterOperators)
            {
                if (!query_operators.insert(query_operator).second)
                {
                    AddError(result, "duplicate_query_operator", model.modelId, path, "Query operators must be unique.");
                }
                if (!IsQueryOperatorCompatible(field.kind, query_operator))
                {
                    AddError(result, "query_operator_kind_mismatch", model.modelId, path, "Query operator is incompatible with the semantic field kind.");
                }
                if (query_operator == QueryOperator::IsNull && !field.nullable)
                {
                    AddWarning(result, "is_null_on_required_field", model.modelId, path, "A non-null field does not need an is-null query operator.");
                }
            }
            if (field.query.cursor && !field.query.sortable)
            {
                AddError(result, "cursor_field_not_sortable", model.modelId, path, "Cursor fields must also be sortable.");
            }
            if ((field.query.sortable || field.query.cursor) && !HasIndexPrefix(model, {field.name}))
            {
                AddError(result, "ordered_query_field_not_indexed", model.modelId, path, "Sortable and cursor fields require an index or key prefix.");
            }
        }
        if (HasDuplicate(model.retiredFieldNames, [](const std::string& value) -> std::string_view { return value; }))
        {
            AddError(result, "duplicate_retired_field_name", model.modelId, "retiredFieldNames", "Retired field names must be unique.");
        }
        if (std::any_of(model.retiredFieldNames.begin(), model.retiredFieldNames.end(), [&field_names](const std::string& name) {
            return name.empty() || field_names.contains(name);
        }))
        {
            AddError(result, "retired_field_name_reused", model.modelId, "retiredFieldNames", "Retired field names cannot be empty or reused by active fields.");
        }
        std::set<std::uint32_t> retired_field_ids;
        for (const auto retired_id : model.retiredFieldIds)
        {
            if (retired_id == 0 || !retired_field_ids.insert(retired_id).second || field_ids.contains(retired_id))
            {
                AddError(result, "retired_field_id_reused", model.modelId, "retiredFieldIds", "Retired field ids must be nonzero, unique, and absent from active fields.");
            }
        }

        std::set<std::string> one_of_ids;
        for (const auto& one_of : model.oneOfs)
        {
            const std::string path = "oneOfs." + one_of.oneOfId;
            const auto* discriminator = FindField(model, one_of.discriminatorField);
            if (one_of.oneOfId.empty() || discriminator == nullptr || discriminator->kind != FieldKind::Enum)
            {
                AddError(result, "one_of_identity_invalid", model.modelId, path, "One-of contracts require an id and enum discriminator field.");
                continue;
            }
            if (!one_of_ids.insert(one_of.oneOfId).second)
            {
                AddError(result, "duplicate_one_of_id", model.modelId, path, "One-of ids must be unique within a model.");
            }
            std::set<std::string> tags;
            for (const auto& variant : one_of.variants)
            {
                if (variant.tag.empty() || !tags.insert(variant.tag).second ||
                    std::find(discriminator->enumValues.begin(), discriminator->enumValues.end(), variant.tag) == discriminator->enumValues.end())
                {
                    AddError(result, "one_of_variant_tag_invalid", model.modelId, path, "One-of variant tags must be unique values from the discriminator enum.");
                }
                if (variant.tagValueId != 0)
                {
                    const auto tag = std::find(
                        discriminator->enumValues.begin(), discriminator->enumValues.end(), variant.tag
                    );
                    const auto ordinal = static_cast<std::size_t>(
                        std::distance(discriminator->enumValues.begin(), tag)
                    );
                    if (tag == discriminator->enumValues.end() ||
                        discriminator->enumValueIds.size() != discriminator->enumValues.size() ||
                        discriminator->enumValueIds[ordinal] != variant.tagValueId)
                    {
                        AddError(result, "one_of_variant_tag_value_id_invalid", model.modelId, path, "One-of numeric tag ids must match the discriminator enum allocation.");
                    }
                }
                if (HasRepeatedField(variant.fields) || !FieldsExist(model, variant.fields) ||
                    std::find(variant.fields.begin(), variant.fields.end(), one_of.discriminatorField) != variant.fields.end())
                {
                    AddError(result, "one_of_variant_field_invalid", model.modelId, path, "One-of payload fields must exist, be unique, and exclude the discriminator.");
                }
                if (HasRepeatedField(variant.optionalFields) || !FieldsExist(model, variant.optionalFields) ||
                    std::find(variant.optionalFields.begin(), variant.optionalFields.end(), one_of.discriminatorField) != variant.optionalFields.end() ||
                    std::any_of(variant.optionalFields.begin(), variant.optionalFields.end(), [&variant](const std::string& field_name) {
                        return std::find(variant.fields.begin(), variant.fields.end(), field_name) != variant.fields.end();
                    }))
                {
                    AddError(result, "one_of_optional_field_invalid", model.modelId, path, "Optional one-of payload fields must exist, be unique, exclude the discriminator, and not also be required.");
                }
                std::vector<std::string> payload_fields = variant.fields;
                payload_fields.insert(payload_fields.end(), variant.optionalFields.begin(), variant.optionalFields.end());
                for (const auto& field_name : payload_fields)
                {
                    const auto* payload = FindField(model, field_name);
                    if (payload != nullptr && !payload->nullable)
                    {
                        AddError(result, "one_of_payload_not_nullable", model.modelId, path, "One-of payload fields must be nullable outside their active variant.");
                    }
                }
            }
            if (tags.size() != discriminator->enumValues.size())
            {
                AddError(result, "one_of_variant_coverage_incomplete", model.modelId, path, "Every discriminator value requires exactly one variant contract.");
            }
        }

        std::set<std::string> key_ids;
        int primary_key_count = 0;
        for (const auto& key : model.keys)
        {
            const std::string path = "keys." + key.keyId;
            if (key.keyId.empty() || key.fields.empty())
            {
                AddError(result, "key_identity_required", model.modelId, path, "Key id and fields are required.");
            }
            if (!key_ids.insert(key.keyId).second)
            {
                AddError(result, "duplicate_key_id", model.modelId, path, "Key id must be unique within a model.");
            }
            if (key.kind == KeyKind::Primary)
            {
                ++primary_key_count;
                for (const auto& field_name : key.fields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        AddError(result, "nullable_primary_key_field", model.modelId, path, "Primary key fields cannot be nullable.");
                    }
                }
            }
            if (HasRepeatedField(key.fields) || !FieldsExist(model, key.fields))
            {
                AddError(result, "key_field_invalid", model.modelId, path, "Every key field must exist exactly once.");
            }
        }
        if (primary_key_count != 1)
        {
            AddError(result, "primary_key_count_invalid", model.modelId, "keys", "Every model requires exactly one primary key.");
        }

        std::set<std::string> index_ids;
        for (const auto& index : model.indexes)
        {
            const std::string path = "indexes." + index.indexId;
            if (index.indexId.empty() || index.fields.empty())
            {
                AddError(result, "index_identity_required", model.modelId, path, "Index id and fields are required.");
            }
            if (!index_ids.insert(index.indexId).second)
            {
                AddError(result, "duplicate_index_id", model.modelId, path, "Index id must be unique within a model.");
            }
            if (HasRepeatedField(index.fields) || !FieldsExist(model, index.fields))
            {
                AddError(result, "index_field_invalid", model.modelId, path, "Every index field must exist exactly once.");
            }
        }

        std::set<std::string> relation_ids;
        for (const auto& relation : model.relations)
        {
            const std::string path = "relations." + relation.relationId;
            if (model.retention == RetentionPolicy::Immutable &&
                (relation.onDelete != ReferentialAction::Restrict ||
                 relation.onUpdate != ReferentialAction::Restrict))
            {
                AddError(
                    result,
                    "immutable_relation_action_invalid",
                    model.modelId,
                    path,
                    "Immutable rows cannot be changed or removed by referential actions."
                );
            }
            if (relation.relationId.empty() || relation.targetModel.empty() || relation.sourceFields.empty() ||
                relation.targetFields.empty())
            {
                AddError(result, "relation_identity_required", model.modelId, path, "Relation id, target, and field mappings are required.");
            }
            if (!relation_ids.insert(relation.relationId).second)
            {
                AddError(result, "duplicate_relation_id", model.modelId, path, "Relation id must be unique within a model.");
            }
            if (relation.sourceFields.size() != relation.targetFields.size())
            {
                AddError(result, "relation_field_arity_mismatch", model.modelId, path, "Relation source and target field counts must match.");
            }
            if (HasRepeatedField(relation.sourceFields) || !FieldsExist(model, relation.sourceFields))
            {
                AddError(result, "relation_source_field_invalid", model.modelId, path, "Relation source fields must exist exactly once.");
            }
            const ModelSchema* target = FindModelById(catalog, relation.targetModel);
            if (target == nullptr)
            {
                AddError(result, "relation_target_missing", model.modelId, path, "Relation target model must exist.");
            }
            else if (HasRepeatedField(relation.targetFields) || !FieldsExist(*target, relation.targetFields))
            {
                AddError(result, "relation_target_field_invalid", model.modelId, path, "Relation target fields must exist exactly once.");
            }
            else if (!HasIndexPrefix(*target, relation.targetFields))
            {
                AddError(result, "relation_target_not_keyed", model.modelId, path, "Relation target fields require a key or index prefix.");
            }
            else if (relation.cardinality != RelationCardinality::Many &&
                     !IsUniqueFieldSet(*target, relation.targetFields))
            {
                AddError(result, "singular_relation_target_not_unique", model.modelId, path, "Singular relation target fields must identify a unique row.");
            }
            if (target != nullptr && relation.sourceFields.size() == relation.targetFields.size())
            {
                for (std::size_t field_index = 0; field_index < relation.sourceFields.size(); ++field_index)
                {
                    const auto* source_field = FindField(model, relation.sourceFields[field_index]);
                    const auto* target_field = FindField(*target, relation.targetFields[field_index]);
                    if (source_field != nullptr && target_field != nullptr &&
                        source_field->semanticTypeId.value != 0 && target_field->semanticTypeId.value != 0 &&
                        source_field->semanticTypeId != target_field->semanticTypeId)
                    {
                        AddError(result, "relation_semantic_type_mismatch", model.modelId, path,
                                 "Relation source and target fields must share the same semantic type id.");
                    }
                }
            }
            if (!HasIndexPrefix(model, relation.sourceFields))
            {
                AddError(result, "relation_source_not_indexed", model.modelId, path, "Relation source fields require an index prefix.");
            }
            if (relation.cardinality == RelationCardinality::One)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        AddError(result, "required_relation_nullable", model.modelId, path, "Required one-to-one relation fields cannot be nullable.");
                    }
                }
            }
            if (relation.onDelete == ReferentialAction::SetNull ||
                relation.onUpdate != ReferentialAction::Restrict)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name);
                        field != nullptr && (field->immutable || field->writeOnce))
                    {
                        AddError(
                            result,
                            "immutable_relation_field_action_invalid",
                            model.modelId,
                            path,
                            "Referential actions cannot rewrite immutable relation fields."
                        );
                    }
                }
            }
            if (relation.cardinality == RelationCardinality::OptionalOne)
            {
                bool has_nullable_source = false;
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        has_nullable_source = true;
                    }
                }
                if (!has_nullable_source)
                {
                    AddError(result, "nullable_relation_field_required", model.modelId, path, "Optional relations require at least one nullable source field.");
                }
            }
            if (relation.onDelete == ReferentialAction::SetNull || relation.onUpdate == ReferentialAction::SetNull)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && !field->nullable)
                    {
                        AddError(result, "set_null_relation_field_required", model.modelId, path, "Set-null actions require every source field to be nullable.");
                    }
                }
            }
        }

        std::set<std::string> join_ids;
        for (const auto& join : model.joins)
        {
            const std::string path = "joins." + join.joinId;
            if (join.joinId.empty() || join.throughModel.empty() || join.sourceRelation.empty() || join.targetRelation.empty())
            {
                AddError(result, "join_identity_required", model.modelId, path, "Join id, through model, and relation ids are required.");
            }
            if (!join_ids.insert(join.joinId).second)
            {
                AddError(result, "duplicate_join_id", model.modelId, path, "Join id must be unique within a model.");
            }
            if (relation_ids.contains(join.joinId))
            {
                AddError(result, "relation_join_id_collision", model.modelId, path, "Relation and join ids share one include namespace and must be unique.");
            }
            if (join.sourceRelation == join.targetRelation)
            {
                AddError(result, "join_relations_must_differ", model.modelId, path, "Join source and target relations must differ.");
            }
            const ModelSchema* through = FindModelById(catalog, join.throughModel);
            if (through == nullptr)
            {
                AddError(result, "join_through_model_missing", model.modelId, path, "Join through model must exist.");
                continue;
            }
            const RelationSchema* source_relation = FindRelationById(*through, join.sourceRelation);
            const RelationSchema* target_relation = FindRelationById(*through, join.targetRelation);
            if (source_relation == nullptr || target_relation == nullptr)
            {
                AddError(result, "join_relation_missing", model.modelId, path, "Join relations must exist on the through model.");
            }
            else if (source_relation->targetModel != model.modelId)
            {
                AddError(result, "join_source_target_mismatch", model.modelId, path, "Join source relation must point back to the declaring model.");
            }
        }
    }
    for (const auto& retired_name : retired_model_names)
    {
        if (active_model_names.contains(retired_name))
        {
            AddError(result, "retired_model_name_reused", {}, "retiredNames", "Retired model names cannot be reused by an active canonical name.");
        }
    }
    catalog_identity_storage identity_storage;
    const auto identity_view = BuildCatalogIdentityView(catalog, identity_storage);
    AppendCatalogViewValidation(result, ValidateCatalogView(identity_view));
    return result;
}

std::string FingerprintCatalog(const SchemaCatalog& catalog)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    HashText(hash, catalog.catalogId);
    HashText(hash, std::to_string(catalog.version));
    for (const auto& fragment : catalog.fragments)
    {
        HashText(hash, fragment.fragmentId);
        HashText(hash, fragment.ownerCapsule);
        HashText(hash, std::to_string(fragment.version));
        for (const auto& model : fragment.models)
        {
            HashText(hash, model.modelId);
        }
    }
    for (const auto& model : catalog.models)
    {
        HashModel(hash, model);
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

nlohmann::json BuildNamingRegistryManifest(const SchemaCatalog& catalog)
{
    json models = json::array();
    json review_required = json::array();
    json resolved_names = json::array();
    std::size_t field_count = 0;
    for (const auto& model : catalog.models)
    {
        json fields = json::array();
        for (const auto& field : model.fields)
        {
            ++field_count;
            fields.push_back({
                {"fieldId", field.fieldId},
                {"registryKey", field.registryKey},
                {"name", field.name},
                {"storageName", field.storageName},
                {"semanticType", field.semanticType},
                {"semanticTypeKey", field.semanticTypeKey},
                {"semanticTypeId", field.semanticTypeId.value},
                {"wireTypeId", field.wireTypeId.value},
                {"storageCodecId", field.storageCodecId.value},
            });
        }
        models.push_back({
            {"typeId", model.typeId},
            {"registryKey", model.registryKey},
            {"modelId", model.modelId},
            {"qualifiedName", model.qualifiedName},
            {"logicalName", model.logicalName},
            {"storageName", model.storageName},
            {"ownerCapsule", model.ownerCapsule},
            {"mutationAuthority", model.mutationAuthority},
            {"role", std::string(ModelRoleName(model.role))},
            {"sourceAliases", model.sourceAliases},
            {"retiredNames", model.retiredNames},
            {"nameReview", std::string(NameReviewStatusName(model.nameReview))},
            {"nameReviewRationale", model.nameReviewRationale},
            {"retiredFieldIds", model.retiredFieldIds},
            {"retiredFieldNames", model.retiredFieldNames},
            {"fields", std::move(fields)},
        });
        if (model.nameReview == NameReviewStatus::ReviewRequired)
        {
            review_required.push_back({
                {"typeId", model.typeId},
                {"modelId", model.modelId},
                {"storageName", model.storageName},
            });
        }
        for (const auto& retired_name : model.retiredNames)
        {
            resolved_names.push_back({
                {"retiredName", retired_name},
                {"typeId", model.typeId},
                {"modelId", model.modelId},
                {"qualifiedName", model.qualifiedName},
                {"storageName", model.storageName},
            });
        }
    }
    return {
        {"mode", "arken-schema-naming-registry"},
        {"version", 1},
        {"modelIdType", "u32"},
        {"fieldIdType", "u32"},
        {"allocation", "persisted-u32-registry-v1"},
        {"fingerprint", NamingRegistryFingerprint(catalog)},
        {"modelCount", catalog.models.size()},
        {"fieldCount", field_count},
        {"reviewRequiredCount", review_required.size()},
        {"resolvedRetiredNameCount", resolved_names.size()},
        {"models", std::move(models)},
        {"reviewRequired", std::move(review_required)},
        {"resolvedNames", std::move(resolved_names)},
    };
}

nlohmann::json BuildSchemaManifest(const SchemaCatalog& catalog)
{
    json fragments = json::array();
    json models = json::array();
    for (const auto& fragment : catalog.fragments)
    {
        json model_ids = json::array();
        for (const auto& model : fragment.models)
        {
            model_ids.push_back(model.modelId);
        }
        fragments.push_back({
            {"fragmentId", fragment.fragmentId},
            {"ownerCapsule", fragment.ownerCapsule},
            {"version", fragment.version},
            {"modelIds", std::move(model_ids)},
        });
    }
    for (const auto& model : catalog.models)
    {
        models.push_back(ModelJson(model));
    }
    const ValidationResult validation = ValidateCatalog(catalog);
    return {
        {"mode", "arken-schema-catalog"},
        {"catalogId", catalog.catalogId},
        {"version", catalog.version},
        {"fingerprint", FingerprintCatalog(catalog)},
        {"fragmentCount", catalog.fragments.size()},
        {"modelCount", catalog.models.size()},
        {"fragments", std::move(fragments)},
        {"models", std::move(models)},
        {"namingRegistry", BuildNamingRegistryManifest(catalog)},
        {"typeRegistry", BuildTypeRegistryManifest(catalog)},
        {"validation", validation.ToJson()},
    };
}
} // namespace arken::schema::core::v1
