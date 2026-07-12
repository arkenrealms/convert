#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace arken::schema::core::v1
{
enum class FieldKind
{
    ObjectId,
    InternalId,
    Text,
    Slug,
    DisplayName,
    Url,
    Email,
    Enum,
    Bool,
    TimestampMs,
    JsonObject,
    Secret,
    EthereumAddress,
    HashHex,
    TokenHash,
    KeyReference,
    Integer,
    Real,
    Blob,
};

enum class WireType
{
    None,
    Bool,
    U8,
    U16,
    U32,
    U64,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    Utf8,
    Bytes,
    FixedBytes,
};

enum class NormalizationKind
{
    None,
    Trim,
    Lowercase,
    LowercaseTrim,
    EthereumAddress,
    Url,
};

enum class ModelKind
{
    Entity,
    Join,
    Credential,
    Ephemeral,
    AuditLedger,
    Provenance,
    ImportLedger,
};

enum class RetentionPolicy
{
    Durable,
    UntilExpiry,
    Immutable,
    UntilPromotion,
};

enum class DefaultKind
{
    None,
    Literal,
    BoundaryGenerated,
};

enum class CheckKind
{
    MinimumInteger,
    MaximumInteger,
    MinimumReal,
    MaximumReal,
    MinimumLength,
    MaximumLength,
    Pattern,
    Custom,
};

enum class KeyKind
{
    Primary,
    Alternate,
};

enum class RelationCardinality
{
    One,
    OptionalOne,
    Many,
};

enum class ReferentialAction
{
    Restrict,
    Cascade,
    SetNull,
};

enum class QueryOperator
{
    Equal,
    NotEqual,
    In,
    NotIn,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
    Prefix,
    Suffix,
    Contains,
    IsNull,
};

struct DefaultSchema
{
    DefaultKind kind = DefaultKind::None;
    std::string value;
};

struct CheckSchema
{
    std::string checkId;
    CheckKind kind = CheckKind::Custom;
    std::string value;
    std::string message;
};

struct QueryPolicy
{
    std::vector<QueryOperator> filterOperators;
    bool sortable = false;
    bool cursor = false;
};

struct FieldSchema
{
    std::string name;
    std::string storageName;
    FieldKind kind = FieldKind::Text;
    WireType wireType = WireType::None;
    int wireSize = 0;
    bool nullable = true;
    bool redacted = false;
    std::string sourceField;
    bool immutable = false;
    bool writeOnce = false;
    NormalizationKind normalization = NormalizationKind::None;
    std::string importTransform;
    DefaultSchema defaultValue;
    std::vector<std::string> enumValues;
    std::vector<CheckSchema> checks;
    QueryPolicy query;
};

struct OneOfVariantSchema
{
    std::string tag;
    std::vector<std::string> fields;
    std::vector<std::string> optionalFields;
};

struct OneOfSchema
{
    std::string oneOfId;
    std::string discriminatorField;
    std::vector<OneOfVariantSchema> variants;
};

struct KeySchema
{
    std::string keyId;
    KeyKind kind = KeyKind::Alternate;
    std::vector<std::string> fields;
};

struct IndexSchema
{
    std::string indexId;
    std::vector<std::string> fields;
    bool unique = false;
};

struct RelationSchema
{
    std::string relationId;
    std::string targetModel;
    std::vector<std::string> sourceFields;
    std::vector<std::string> targetFields;
    RelationCardinality cardinality = RelationCardinality::One;
    ReferentialAction onDelete = ReferentialAction::Restrict;
    ReferentialAction onUpdate = ReferentialAction::Restrict;
};

struct JoinSchema
{
    std::string joinId;
    std::string throughModel;
    std::string sourceRelation;
    std::string targetRelation;
};

struct ModelSchema
{
    std::string modelId;
    std::string logicalName;
    std::string storageName;
    std::string ownerCapsule;
    std::string domain;
    std::string description;
    ModelKind kind = ModelKind::Entity;
    RetentionPolicy retention = RetentionPolicy::Durable;
    std::vector<FieldSchema> fields;
    std::vector<KeySchema> keys;
    std::vector<IndexSchema> indexes;
    std::vector<RelationSchema> relations;
    std::vector<JoinSchema> joins;
    std::vector<OneOfSchema> oneOfs;
};

struct SchemaFragment
{
    std::string fragmentId;
    std::string ownerCapsule;
    int version = 1;
    std::vector<ModelSchema> models;
};

struct SchemaCatalog
{
    std::string catalogId;
    int version = 1;
    std::vector<SchemaFragment> fragments;
    std::vector<ModelSchema> models;
};

struct ValidationDiagnostic
{
    std::string code;
    std::string modelId;
    std::string path;
    std::string message;
};

struct ValidationResult
{
    std::vector<ValidationDiagnostic> errors;
    std::vector<ValidationDiagnostic> warnings;

    [[nodiscard]] bool ok() const;
    [[nodiscard]] nlohmann::json ToJson() const;
};

struct ParseDiagnostic
{
    std::string path;
    std::string message;
};

struct FragmentManifestParseResult
{
    std::vector<SchemaFragment> fragments;
    std::vector<ParseDiagnostic> errors;

    [[nodiscard]] bool ok() const;
};

std::string_view FieldKindName(FieldKind kind);
std::string_view WireTypeName(WireType type);
std::string_view NormalizationKindName(NormalizationKind kind);
std::string_view ModelKindName(ModelKind kind);
std::string_view RetentionPolicyName(RetentionPolicy policy);
std::string_view DefaultKindName(DefaultKind kind);
std::string_view CheckKindName(CheckKind kind);
std::string_view KeyKindName(KeyKind kind);
std::string_view RelationCardinalityName(RelationCardinality cardinality);
std::string_view ReferentialActionName(ReferentialAction action);
std::string_view QueryOperatorName(QueryOperator query_operator);

SchemaCatalog ComposeCatalog(std::string catalog_id, int version, std::vector<SchemaFragment> fragments);
const ModelSchema* FindModelById(const SchemaCatalog& catalog, std::string_view model_id);
const ModelSchema* FindModelByLogicalName(const SchemaCatalog& catalog, std::string_view logical_name);
const ModelSchema* FindModelByStorageName(const SchemaCatalog& catalog, std::string_view storage_name);
const FieldSchema* FindField(const ModelSchema& model, std::string_view field_name);
const RelationSchema* FindRelationById(const ModelSchema& model, std::string_view relation_id);
const JoinSchema* FindJoinById(const ModelSchema& model, std::string_view join_id);
bool HasIndexPrefix(const ModelSchema& model, const std::vector<std::string>& fields);
bool IsSingleFieldUnique(const ModelSchema& model, std::string_view field_name);
ValidationResult ValidateCatalog(const SchemaCatalog& catalog);
std::string FingerprintCatalog(const SchemaCatalog& catalog);
nlohmann::json BuildSchemaManifest(const SchemaCatalog& catalog);
FragmentManifestParseResult ParseSchemaFragmentManifest(const nlohmann::json& manifest);
} // namespace arken::schema::core::v1
