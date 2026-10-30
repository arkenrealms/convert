#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/idl.hpp"

namespace arken::schema::idl::v1
{
struct SourceHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const SourceHandle&) const = default;
    [[nodiscard]] auto operator<=>(const SourceHandle&) const = default;
};

struct ModelHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const ModelHandle&) const = default;
    [[nodiscard]] auto operator<=>(const ModelHandle&) const = default;
};

struct FieldHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const FieldHandle&) const = default;
    [[nodiscard]] auto operator<=>(const FieldHandle&) const = default;
};

struct EnumHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const EnumHandle&) const = default;
    [[nodiscard]] auto operator<=>(const EnumHandle&) const = default;
};

struct WireTypeHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const WireTypeHandle&) const = default;
    [[nodiscard]] auto operator<=>(const WireTypeHandle&) const = default;
};

struct StorageCodecHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const StorageCodecHandle&) const = default;
    [[nodiscard]] auto operator<=>(const StorageCodecHandle&) const = default;
};

struct SemanticTypeHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const SemanticTypeHandle&) const = default;
    [[nodiscard]] auto operator<=>(const SemanticTypeHandle&) const = default;
};

struct EnumValueHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const EnumValueHandle&) const = default;
    [[nodiscard]] auto operator<=>(const EnumValueHandle&) const = default;
};

struct CommandHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const CommandHandle&) const = default;
    [[nodiscard]] auto operator<=>(const CommandHandle&) const = default;
};

struct EventHandle
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool operator==(const EventHandle&) const = default;
    [[nodiscard]] auto operator<=>(const EventHandle&) const = default;
};

static_assert(sizeof(SourceHandle) == sizeof(std::uint32_t));
static_assert(sizeof(ModelHandle) == sizeof(std::uint32_t));
static_assert(sizeof(FieldHandle) == sizeof(std::uint32_t));
static_assert(sizeof(EnumHandle) == sizeof(std::uint32_t));
static_assert(sizeof(WireTypeHandle) == sizeof(std::uint32_t));
static_assert(sizeof(StorageCodecHandle) == sizeof(std::uint32_t));
static_assert(sizeof(SemanticTypeHandle) == sizeof(std::uint32_t));
static_assert(sizeof(EnumHandle) == sizeof(std::uint32_t));
static_assert(sizeof(EnumValueHandle) == sizeof(std::uint32_t));
static_assert(sizeof(CommandHandle) == sizeof(std::uint32_t));
static_assert(sizeof(EventHandle) == sizeof(std::uint32_t));

struct SemanticProvenance
{
    std::string sourcePath;
    SourceSpan span;
};

struct SemanticCatalogFragment
{
    std::string fragmentId;
    std::string ownerCapsule;
    std::uint32_t version = 0;
    std::uint32_t declarationOrder = 0;
    SemanticProvenance provenance;
};

struct SemanticCatalog
{
    std::string catalogId;
    std::uint32_t version = 0;
    std::vector<SemanticCatalogFragment> fragments;
    SemanticProvenance provenance;
};

struct SemanticCatalogFieldIdentity
{
    std::string registryKey;
    std::string fieldName;
    std::uint32_t declarationOrder = 0;
};

struct SemanticCatalogModelIdentity
{
    std::string fragmentId;
    std::string registryKey;
    std::string modelId;
    std::string logicalName;
    std::string qualifiedName;
    std::string domain;
    std::uint32_t declarationOrder = 0;
};

enum class WirePrimitiveKind : std::uint8_t
{
    Bool = 1,
    U8 = 2,
    U16 = 3,
    U32 = 4,
    U64 = 5,
    I8 = 6,
    I16 = 7,
    I32 = 8,
    I64 = 9,
    F32 = 10,
    F64 = 11,
    Utf8 = 12,
    Bytes = 13,
    FixedBytes = 14,
    Decimal128 = 15,
};

enum class WireByteOrder : std::uint8_t
{
    None,
    BigEndian,
};

enum class WireCompatibilityClass : std::uint8_t
{
    Boolean,
    FixedUnsigned,
    FixedSigned,
    Ieee754,
    Utf8,
    Bytes,
    FixedBytes,
    Decimal128,
};

enum class StorageCodecKind : std::uint8_t
{
    SqliteInteger = 1,
    SqliteReal = 2,
    SqliteText = 3,
    SqliteBlob = 4,
    SqliteFixedBlob = 5,
    SqliteOrderedU64 = 6,
    SqliteOrderedDecimal128 = 7,
};

enum class SqliteStorageClass : std::uint8_t
{
    Integer,
    Real,
    Text,
    Blob,
};

enum class SemanticTypeKind : std::uint8_t
{
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
    Text,
    Enum,
    Blob,
    FixedId,
    Decimal,
    Timestamp,
    JsonText,
};

enum class JsonProfile : std::uint8_t
{
    None,
    Rfc8259Preserve,
    Rfc8785Canonical,
};

enum class BoundaryMappingPolicy : std::uint8_t
{
    Scalar,
    Opaque,
};

enum class NormalizationKind : std::uint8_t
{
    None,
    Trim,
    Lowercase,
    LowercaseTrim,
    EthereumAddress,
    Url,
};

enum class FieldKind : std::uint8_t
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

enum class PresencePolicy : std::uint8_t
{
    Explicit,
};

enum class DefaultPolicy : std::uint8_t
{
    None,
};

enum class TemporalRole : std::uint8_t
{
    None,
    ValidTime,
    TransactionTime,
    OccurrenceTime,
    ObservationTime,
    PublicationTime,
    BoundaryCreationTime,
};

enum class NarrowingPolicy : std::uint8_t
{
    Reject,
};

enum class TargetProfile : std::uint8_t
{
    Cpp,
    Sqlite,
    CSharp,
    JavaScript,
    Fabric,
    HttpJson,
};

enum class OperationTargetProfile : std::uint8_t
{
    Cpp,
    Fabric,
    HttpJson,
};

enum class FieldCardinality : std::uint8_t
{
    RequiredOne,
    OptionalOne,
    Repeated,
};

enum class FabricLane : std::uint8_t
{
    ControlReliable,
    StateReliable,
    BulkReliable,
    RealtimeUnreliable,
    Compatibility,
};

struct FabricOperationProfile
{
    FabricLane lane = FabricLane::ControlReliable;
    std::uint32_t maximumEncodedBytes = 0;
    bool directEligible = false;

    [[nodiscard]] bool operator==(const FabricOperationProfile&) const = default;
};

enum class ModelKind : std::uint8_t
{
    Entity,
    Join,
    Credential,
    Ephemeral,
    AuditLedger,
    Provenance,
    ImportLedger,
};

enum class ModelRole : std::uint8_t
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

enum class RetentionPolicy : std::uint8_t
{
    Durable,
    UntilExpiry,
    Immutable,
    UntilPromotion,
};

enum class NameReviewStatus : std::uint8_t
{
    Canonical,
    ReviewRequired,
    ApprovedException,
};

enum class FieldSourceKind : std::uint8_t
{
    Canonical,
    Imported,
    Derived,
    BoundaryGenerated,
};

enum class KeyKind : std::uint8_t
{
    Primary,
    Alternate,
};

enum class RelationCardinality : std::uint8_t
{
    One,
    OptionalOne,
    Many,
};

enum class ReferentialAction : std::uint8_t
{
    Restrict,
    Cascade,
    SetNull,
};

enum class FieldDefaultKind : std::uint8_t
{
    None,
    Literal,
    Boundary,
};

enum class BoundaryDefaultSource : std::uint8_t
{
    None,
    RequestTime,
    GeneratedId,
    GeneratedToken,
};

enum class CheckKind : std::uint8_t
{
    MinimumInteger,
    MaximumInteger,
    MinimumReal,
    MaximumReal,
    MinimumLength,
    MaximumLength,
    FixedLength,
    EnumDomain,
    ValidJson,
    DecimalPrecision,
    DecimalScale,
};

enum class CheckUnit : std::uint8_t
{
    None,
    Utf8Bytes,
    Bytes,
};

enum class QueryOperator : std::uint8_t
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

enum class SerializationPolicy : std::uint8_t
{
    Include,
    Omit,
};

enum class TemporalOrdering : std::uint8_t
{
    Strict,
    Nondecreasing,
};

enum class OpenEndPolicy : std::uint8_t
{
    Forbidden,
    Allowed,
};

enum class CommandIdempotency : std::uint8_t
{
    None,
    Idempotent,
    Safe,
};

enum class CommandStreaming : std::uint8_t
{
    Unary,
    ServerStream,
    ClientStream,
    Bidirectional,
};

enum class EventDelivery : std::uint8_t
{
    BestEffort,
    AtLeastOnce,
};

enum class EventOrdering : std::uint8_t
{
    None,
    Source,
    Aggregate,
    Global,
};

enum class EventReplay : std::uint8_t
{
    None,
    Retained,
    Resumable,
};

struct WireTypeDefinition
{
    WireTypeHandle handle;
    SourceHandle source;
    WireTypeId wireTypeId;
    std::string name;
    std::string qualifiedName;
    std::string ownerCapsule;
    WirePrimitiveKind kind = WirePrimitiveKind::Bool;
    std::uint32_t bitWidth = 0;
    std::uint32_t fixedSize = 0;
    WireByteOrder byteOrder = WireByteOrder::None;
    SemanticProvenance provenance;
};

struct StorageCodecDefinition
{
    StorageCodecHandle handle;
    SourceHandle source;
    StorageCodecId storageCodecId;
    std::string name;
    std::string qualifiedName;
    std::string ownerCapsule;
    StorageCodecKind kind = StorageCodecKind::SqliteInteger;
    SqliteStorageClass storageClass = SqliteStorageClass::Integer;
    std::uint32_t fixedSize = 0;
    bool exact = false;
    bool orderPreserving = false;
    SemanticProvenance provenance;
};

struct SemanticTypeDefinition
{
    SemanticTypeHandle handle;
    SourceHandle source;
    SemanticTypeId semanticTypeId;
    std::string name;
    std::string qualifiedName;
    std::string semanticName;
    std::string registryKey;
    std::string ownerCapsule;
    SemanticTypeKind kind = SemanticTypeKind::U32;
    WireTypeHandle wireType;
    StorageCodecHandle storageCodec;
    std::uint32_t fixedSize = 0;
    std::uint32_t precision = 0;
    std::uint32_t scale = 0;
    NormalizationKind normalization = NormalizationKind::None;
    JsonProfile jsonProfile = JsonProfile::None;
    BoundaryMappingPolicy boundaryMapping = BoundaryMappingPolicy::Scalar;
    PresencePolicy presence = PresencePolicy::Explicit;
    DefaultPolicy defaultPolicy = DefaultPolicy::None;
    NarrowingPolicy narrowing = NarrowingPolicy::Reject;
    TemporalRole temporalRole = TemporalRole::None;
    std::vector<TargetProfile> targets;
    std::optional<ModelHandle> definingModel;
    std::optional<EnumHandle> definingEnum;
    SemanticProvenance provenance;
};

struct SemanticTypeReference
{
    SemanticTypeKind kind = SemanticTypeKind::U32;
    SemanticTypeHandle semanticType;
    SemanticTypeId semanticTypeId;
    std::string canonicalName;
    std::optional<ModelHandle> definingModel;
    std::optional<EnumHandle> definingEnum;
};

struct SemanticImport
{
    SourceHandle source;
    std::string sourcePath;
    SemanticProvenance provenance;
};

struct SemanticFieldDefault
{
    FieldDefaultKind kind = FieldDefaultKind::None;
    std::string literal;
    BoundaryDefaultSource boundarySource = BoundaryDefaultSource::None;
    std::string catalogValue;
};

struct SemanticCatalogMemberIdentity
{
    std::string name;
    std::uint32_t declarationOrder = 0;
};

struct SemanticCheck
{
    std::string name;
    CheckKind kind = CheckKind::MinimumInteger;
    CheckUnit unit = CheckUnit::None;
    std::string value;
    std::string message;
    std::vector<TargetProfile> targets;
    bool generated = false;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
};

struct SemanticQueryPolicy
{
    std::vector<QueryOperator> operators;
    bool sortable = false;
    bool cursor = false;
};

struct SemanticFieldRepresentation
{
    FieldKind fieldKind = FieldKind::Text;
    WireTypeHandle wireType;
    WireTypeId wireTypeId;
    std::uint32_t wireSize = 0;
    StorageCodecHandle storageCodec;
    StorageCodecId storageCodecId;
    std::uint32_t storageSize = 0;
    std::uint32_t precision = 0;
    std::uint32_t scale = 0;
    NormalizationKind normalization = NormalizationKind::None;
};

struct SemanticField
{
    FieldHandle handle;
    FieldId fieldId;
    std::string name;
    std::optional<std::string> cppName;
    std::optional<std::string> csharpName;
    std::string storageName;
    SemanticTypeReference type;
    SemanticFieldRepresentation representation;
    TemporalRole temporalRole = TemporalRole::None;
    SemanticFieldDefault defaultValue;
    std::vector<EnumValueId> allowedEnumValueIds;
    std::vector<SemanticCheck> checks;
    SemanticQueryPolicy query;
    std::optional<std::string> sourceField;
    std::string importTransform;
    FieldSourceKind sourceKind = FieldSourceKind::Canonical;
    SerializationPolicy serialization = SerializationPolicy::Include;
    std::optional<std::uint32_t> minimum;
    FieldCardinality cardinality = FieldCardinality::RequiredOne;
    std::optional<std::uint32_t> maximumItems;
    bool required = true;
    bool redacted = false;
    bool immutable = false;
    bool writeOnce = false;
    bool primaryKey = false;
    bool generatedPrimaryId = false;
    bool equalityComparable = false;
    std::optional<SemanticCatalogFieldIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticFieldReference
{
    FieldHandle handle;
    FieldId fieldId;
    std::string canonicalName;
};

struct SemanticKey
{
    std::string name;
    KeyKind kind = KeyKind::Alternate;
    std::vector<SemanticFieldReference> fields;
    bool generated = false;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticIndex
{
    std::string name;
    std::vector<SemanticFieldReference> fields;
    bool unique = false;
    bool generated = false;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticRelation
{
    std::string name;
    ModelHandle targetModel;
    ModelTypeId targetTypeId;
    std::string targetQualifiedName;
    std::vector<SemanticFieldReference> sourceFields;
    std::vector<SemanticFieldReference> targetFields;
    RelationCardinality cardinality = RelationCardinality::One;
    ReferentialAction onDelete = ReferentialAction::Restrict;
    ReferentialAction onUpdate = ReferentialAction::Restrict;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticJoin
{
    std::string name;
    ModelHandle throughModel;
    ModelTypeId throughTypeId;
    std::string throughQualifiedName;
    std::string sourceRelation;
    std::string targetRelation;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticOneOfVariant
{
    std::string tag;
    std::vector<SemanticFieldReference> fields;
    std::vector<SemanticFieldReference> optionalFields;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticOneOf
{
    std::string name;
    SemanticFieldReference discriminator;
    EnumHandle enumeration;
    std::vector<SemanticOneOfVariant> variants;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticTemporalContract
{
    std::string name;
    SemanticFieldReference startField;
    SemanticFieldReference endField;
    TemporalOrdering ordering = TemporalOrdering::Strict;
    OpenEndPolicy openEnd = OpenEndPolicy::Forbidden;
    bool startInclusive = true;
    bool endInclusive = false;
    SemanticProvenance provenance;
};

struct SemanticModel
{
    ModelHandle handle;
    SourceHandle source;
    ModelTypeId typeId;
    bool external = false;
    std::string schemaName;
    std::uint32_t schemaVersion = 0;
    std::string name;
    std::string storageName;
    std::string qualifiedName;
    std::string ownerCapsule;
    std::string domain;
    std::string description;
    ModelKind kind = ModelKind::Entity;
    RetentionPolicy retention = RetentionPolicy::Durable;
    std::string mutationAuthority;
    ModelRole role = ModelRole::Unspecified;
    std::vector<std::string> sourceAliases;
    std::vector<std::string> retiredNames;
    std::vector<std::string> catalogRetiredNames;
    NameReviewStatus nameReview = NameReviewStatus::Canonical;
    std::string nameReviewRationale;
    std::optional<SemanticCatalogModelIdentity> catalogIdentity;
    std::vector<SemanticField> fields;
    std::vector<SemanticKey> keys;
    std::vector<SemanticIndex> indexes;
    std::vector<SemanticRelation> relations;
    std::vector<SemanticJoin> joins;
    std::vector<SemanticOneOf> oneOfs;
    std::vector<SemanticTemporalContract> temporalContracts;
    SemanticProvenance provenance;
};

struct SemanticEnumValue
{
    EnumValueId valueId;
    std::string name;
    std::optional<SemanticCatalogMemberIdentity> catalogIdentity;
    SemanticProvenance provenance;
};

struct SemanticEnum
{
    EnumHandle handle;
    SourceHandle source;
    EnumId enumId;
    std::string name;
    std::string qualifiedName;
    std::string ownerCapsule;
    std::string mutationAuthority;
    SemanticTypeHandle semanticType;
    SemanticTypeId semanticTypeId;
    std::vector<SemanticEnumValue> values;
    SemanticProvenance provenance;
};

struct SemanticModelReference
{
    ModelHandle model;
    ModelTypeId typeId;
    std::string canonicalName;

    [[nodiscard]] bool operator==(const SemanticModelReference&) const = default;
};

struct SemanticCommand
{
    CommandHandle handle;
    SourceHandle source;
    CommandId commandId;
    std::string name;
    std::string qualifiedName;
    std::string ownerCapsule;
    std::optional<SemanticModelReference> request;
    std::optional<SemanticModelReference> response;
    std::vector<SemanticModelReference> errors;
    CommandIdempotency idempotency = CommandIdempotency::None;
    CommandStreaming streaming = CommandStreaming::Unary;
    std::vector<OperationTargetProfile> targets;
    std::optional<FabricOperationProfile> fabric;
    SemanticProvenance provenance;
};

struct SemanticEvent
{
    EventHandle handle;
    SourceHandle source;
    EventId eventId;
    std::string name;
    std::string qualifiedName;
    std::string ownerCapsule;
    SemanticModelReference payload;
    EventDelivery delivery = EventDelivery::BestEffort;
    EventOrdering ordering = EventOrdering::None;
    EventReplay replay = EventReplay::None;
    std::vector<OperationTargetProfile> targets;
    std::optional<FabricOperationProfile> fabric;
    SemanticProvenance provenance;
};

struct SemanticReservedId
{
    RegistryScope scope;
    std::uint32_t value = 0;
    std::string ownerCapsule;
    SemanticProvenance provenance;
};

struct SemanticReservedName
{
    RegistryScope scope;
    std::string canonicalName;
    std::string ownerCapsule;
    SemanticProvenance provenance;
};

struct SemanticSource
{
    SourceHandle handle;
    std::string path;
    std::uint32_t edition = 0;
    std::string schemaName;
    std::uint32_t schemaVersion = 0;
    std::string ownerCapsule;
    std::vector<SemanticImport> imports;
    std::vector<WireTypeHandle> wireTypes;
    std::vector<StorageCodecHandle> storageCodecs;
    std::vector<SemanticTypeHandle> semanticTypes;
    std::vector<EnumHandle> enums;
    std::vector<ModelHandle> models;
    std::vector<CommandHandle> commands;
    std::vector<EventHandle> events;
};

struct SemanticIr
{
    std::uint32_t formatVersion = 16;
    SourceHandle entrySource;
    std::optional<SemanticCatalog> catalog;
    std::vector<SemanticSource> sources;
    std::vector<WireTypeDefinition> wireTypes;
    std::vector<StorageCodecDefinition> storageCodecs;
    std::vector<SemanticTypeDefinition> semanticTypes;
    std::vector<SemanticEnum> enums;
    std::vector<SemanticModel> models;
    std::vector<SemanticCommand> commands;
    std::vector<SemanticEvent> events;
    std::vector<SemanticReservedId> reservedIds;
    std::vector<SemanticReservedName> reservedNames;
};

struct SemanticManifest
{
    SemanticIr ir;
    std::string canonicalJson;
    std::string semanticFingerprint;
};

struct SemanticManifestResult
{
    std::optional<SemanticManifest> manifest;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

struct TargetSupportResult
{
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] std::string_view WirePrimitiveKindName(WirePrimitiveKind kind);
[[nodiscard]] std::string_view WireByteOrderName(WireByteOrder order);
[[nodiscard]] std::string_view WireCompatibilityClassName(WireCompatibilityClass compatibility);
[[nodiscard]] WireCompatibilityClass WireCompatibilityClassOf(WirePrimitiveKind kind);
[[nodiscard]] std::string_view StorageCodecKindName(StorageCodecKind kind);
[[nodiscard]] std::string_view SqliteStorageClassName(SqliteStorageClass storage_class);

[[nodiscard]] std::string_view SemanticTypeKindName(SemanticTypeKind kind);
[[nodiscard]] std::string_view JsonProfileName(JsonProfile profile);
[[nodiscard]] std::string_view BoundaryMappingPolicyName(BoundaryMappingPolicy policy);
[[nodiscard]] std::string_view FieldKindName(FieldKind kind);
[[nodiscard]] std::string_view NormalizationKindName(NormalizationKind kind);
[[nodiscard]] std::string_view PresencePolicyName(PresencePolicy policy);
[[nodiscard]] std::string_view DefaultPolicyName(DefaultPolicy policy);
[[nodiscard]] std::string_view TemporalRoleName(TemporalRole role);
[[nodiscard]] std::string_view NarrowingPolicyName(NarrowingPolicy policy);
[[nodiscard]] std::string_view TargetProfileName(TargetProfile profile);
[[nodiscard]] std::string_view ModelKindName(ModelKind kind);
[[nodiscard]] std::string_view ModelRoleName(ModelRole role);
[[nodiscard]] std::string_view RetentionPolicyName(RetentionPolicy policy);
[[nodiscard]] std::string_view NameReviewStatusName(NameReviewStatus status);
[[nodiscard]] std::string_view FieldSourceKindName(FieldSourceKind kind);
[[nodiscard]] std::string_view KeyKindName(KeyKind kind);
[[nodiscard]] std::string_view RelationCardinalityName(RelationCardinality cardinality);
[[nodiscard]] std::string_view ReferentialActionName(ReferentialAction action);
[[nodiscard]] std::string_view FieldDefaultKindName(FieldDefaultKind kind);
[[nodiscard]] std::string_view BoundaryDefaultSourceName(BoundaryDefaultSource source);
[[nodiscard]] std::string_view CheckKindName(CheckKind kind);
[[nodiscard]] std::string_view CheckUnitName(CheckUnit unit);
[[nodiscard]] std::string_view QueryOperatorName(QueryOperator operation);
[[nodiscard]] std::string_view SerializationPolicyName(SerializationPolicy policy);
[[nodiscard]] std::string_view TemporalOrderingName(TemporalOrdering ordering);
[[nodiscard]] std::string_view OpenEndPolicyName(OpenEndPolicy policy);
[[nodiscard]] std::string_view OperationTargetProfileName(OperationTargetProfile profile);
[[nodiscard]] std::string_view FieldCardinalityName(FieldCardinality cardinality);
[[nodiscard]] std::string_view FabricLaneName(FabricLane lane);
[[nodiscard]] std::string_view CommandIdempotencyName(CommandIdempotency policy);
[[nodiscard]] std::string_view CommandStreamingName(CommandStreaming streaming);
[[nodiscard]] std::string_view EventDeliveryName(EventDelivery delivery);
[[nodiscard]] std::string_view EventOrderingName(EventOrdering ordering);
[[nodiscard]] std::string_view EventReplayName(EventReplay replay);
[[nodiscard]] bool SemanticTypesMatch(
    const SemanticTypeReference& lhs,
    const SemanticTypeReference& rhs
);
[[nodiscard]] TargetSupportResult ValidateTargetSupport(
    const SemanticIr& ir,
    TargetProfile profile,
    const ParseLimits& limits = {}
);
[[nodiscard]] SemanticManifestResult BuildSemanticManifest(
    const Project& project,
    const ParseLimits& limits = {}
);
[[nodiscard]] SemanticManifestResult ParseSemanticManifest(
    std::string_view source_name,
    std::string_view manifest_json,
    const ParseLimits& limits = {}
);
}
