#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "arken_schema_core_v1/value.hpp"

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
    JsonText,
    Secret,
    EthereumAddress,
    HashHex,
    TokenHash,
    KeyReference,
    Integer,
    Real,
    Decimal,
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
    Decimal128,
};

enum class StorageCodec
{
    None,
    SqliteInteger,
    SqliteReal,
    SqliteText,
    SqliteBlob,
    SqliteFixedBlob,
    SqliteOrderedU64,
    SqliteOrderedDecimal128,
};

struct SemanticTypeId
{
    std::uint32_t value = 0;
    [[nodiscard]] auto operator<=>(const SemanticTypeId&) const = default;
};

struct WireTypeId
{
    std::uint32_t value = 0;
    [[nodiscard]] auto operator<=>(const WireTypeId&) const = default;
};

struct StorageCodecId
{
    std::uint32_t value = 0;
    [[nodiscard]] auto operator<=>(const StorageCodecId&) const = default;
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

enum class ModelRole
{
    Unspecified,
    UniversalSpine,
    DomainFacet,
    Definition,
    Revision,
    Instance,
    Occurrence,
    Manifestation,
    Representation,
    Assertion,
    Observation,
    AuthoritativeAggregate,
    Ledger,
    Registry,
    Projection,
    ImportRecord,
    Receipt,
    Membership,
    ValueObject,
};

enum class NameReviewStatus
{
    Unspecified,
    Canonical,
    ReviewRequired,
    ApprovedException,
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
    std::string registryKey;
    std::string name;
    std::string storageName;
    FieldKind kind = FieldKind::Text;
    std::string semanticType;
    std::string semanticTypeKey;
    WireType wireType = WireType::None;
    int wireSize = 0;
    StorageCodec storageCodec = StorageCodec::None;
    int storageSize = 0;
    int decimalPrecision = 0;
    int decimalScale = 0;
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
    std::uint32_t fieldId = 0;
    SemanticTypeId semanticTypeId;
    WireTypeId wireTypeId;
    StorageCodecId storageCodecId;
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
    std::string registryKey;
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
    std::uint32_t typeId = 0;
    std::string qualifiedName;
    std::string mutationAuthority;
    ModelRole role = ModelRole::Unspecified;
    std::vector<std::string> sourceAliases;
    std::vector<std::string> retiredNames;
    NameReviewStatus nameReview = NameReviewStatus::Unspecified;
    std::string nameReviewRationale;
    std::vector<std::uint32_t> retiredFieldIds;
    std::vector<std::string> retiredFieldNames;
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

struct SemanticTypeDefinition
{
    SemanticTypeId typeId;
    std::string registryKey;
    std::string name;
    std::size_t fieldCount = 0;
};

struct TypeBindingDefinition
{
    SemanticTypeId semanticTypeId;
    FieldKind fieldKind = FieldKind::Text;
    WireTypeId wireTypeId;
    StorageCodecId storageCodecId;
    int wireSize = 0;
    int storageSize = 0;
    int decimalPrecision = 0;
    int decimalScale = 0;
    std::size_t fieldCount = 0;
};

struct WireTypeDefinition
{
    WireTypeId typeId;
    WireType type = WireType::None;
    int bitWidth = 0;
    bool signedInteger = false;
    bool fixedWidth = false;
};

struct StorageCodecDefinition
{
    StorageCodecId codecId;
    StorageCodec codec = StorageCodec::None;
    std::string sqliteStorageClass;
    int fixedSize = 0;
    bool exact = false;
    bool orderPreserving = false;
};

struct TypeRegistry
{
    int version = 1;
    std::vector<SemanticTypeDefinition> semanticTypes;
    std::vector<TypeBindingDefinition> bindings;
    std::vector<WireTypeDefinition> wireTypes;
    std::vector<StorageCodecDefinition> storageCodecs;
    std::vector<std::uint32_t> retiredSemanticTypeIds;
    std::vector<std::uint32_t> retiredWireTypeIds;
    std::vector<std::uint32_t> retiredStorageCodecIds;
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
std::string_view StorageCodecName(StorageCodec codec);
std::string_view NormalizationKindName(NormalizationKind kind);
std::string_view ModelKindName(ModelKind kind);
std::string_view ModelRoleName(ModelRole role);
std::string_view NameReviewStatusName(NameReviewStatus status);
std::string_view RetentionPolicyName(RetentionPolicy policy);
std::string_view DefaultKindName(DefaultKind kind);
std::string_view CheckKindName(CheckKind kind);
std::string_view KeyKindName(KeyKind kind);
std::string_view RelationCardinalityName(RelationCardinality cardinality);
std::string_view ReferentialActionName(ReferentialAction action);
std::string_view QueryOperatorName(QueryOperator query_operator);

std::uint32_t StableRegistryId(std::string_view registry_namespace, std::string_view canonical_name);
SemanticTypeId SemanticTypeRegistryId(std::string_view registry_key);
WireTypeId WireTypeRegistryId(WireType type);
StorageCodecId StorageCodecRegistryId(StorageCodec codec);
TypeRegistry BuildTypeRegistry(const SchemaCatalog& catalog);
std::string FingerprintTypeRegistry(const SchemaCatalog& catalog);
nlohmann::json BuildTypeRegistryManifest(const SchemaCatalog& catalog);

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
nlohmann::json BuildNamingRegistryManifest(const SchemaCatalog& catalog);
nlohmann::json BuildSchemaManifest(const SchemaCatalog& catalog);
FragmentManifestParseResult ParseSchemaFragmentManifest(const nlohmann::json& manifest);
} // namespace arken::schema::core::v1
