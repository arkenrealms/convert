#include "semantic_token_parsing.hpp"

namespace arken::schema::idl::v1::detail
{
std::optional<TargetProfile> ParseTargetProfile(std::string_view value)
{
    if (value == "cpp") return TargetProfile::Cpp;
    if (value == "sqlite") return TargetProfile::Sqlite;
    if (value == "csharp") return TargetProfile::CSharp;
    if (value == "javascript") return TargetProfile::JavaScript;
    if (value == "fabric") return TargetProfile::Fabric;
    if (value == "http_json") return TargetProfile::HttpJson;
    return std::nullopt;
}

std::optional<OperationTargetProfile> ParseOperationTargetProfile(std::string_view value)
{
    if (value == "cpp") return OperationTargetProfile::Cpp;
    if (value == "fabric") return OperationTargetProfile::Fabric;
    if (value == "http_json") return OperationTargetProfile::HttpJson;
    return std::nullopt;
}

std::optional<FabricLane> ParseFabricLane(std::string_view value)
{
    if (value == "control_reliable") return FabricLane::ControlReliable;
    if (value == "state_reliable") return FabricLane::StateReliable;
    if (value == "bulk_reliable") return FabricLane::BulkReliable;
    if (value == "realtime_unreliable") return FabricLane::RealtimeUnreliable;
    if (value == "compatibility") return FabricLane::Compatibility;
    return std::nullopt;
}

std::optional<ModelKind> ParseModelKind(std::string_view value)
{
    if (value == "entity") return ModelKind::Entity;
    if (value == "join") return ModelKind::Join;
    if (value == "credential") return ModelKind::Credential;
    if (value == "ephemeral") return ModelKind::Ephemeral;
    if (value == "audit_ledger") return ModelKind::AuditLedger;
    if (value == "provenance") return ModelKind::Provenance;
    if (value == "import_ledger") return ModelKind::ImportLedger;
    return std::nullopt;
}

std::optional<ModelRole> ParseModelRole(std::string_view value)
{
    if (value == "unspecified") return ModelRole::Unspecified;
    if (value == "universal_spine") return ModelRole::UniversalSpine;
    if (value == "domain_facet") return ModelRole::DomainFacet;
    if (value == "definition") return ModelRole::Definition;
    if (value == "revision") return ModelRole::Revision;
    if (value == "instance") return ModelRole::Instance;
    if (value == "occurrence") return ModelRole::Occurrence;
    if (value == "manifestation") return ModelRole::Manifestation;
    if (value == "representation") return ModelRole::Representation;
    if (value == "assertion") return ModelRole::Assertion;
    if (value == "observation") return ModelRole::Observation;
    if (value == "authoritative_aggregate") return ModelRole::AuthoritativeAggregate;
    if (value == "ledger") return ModelRole::Ledger;
    if (value == "registry") return ModelRole::Registry;
    if (value == "projection") return ModelRole::Projection;
    if (value == "import_record") return ModelRole::ImportRecord;
    if (value == "receipt") return ModelRole::Receipt;
    if (value == "membership") return ModelRole::Membership;
    if (value == "value_object") return ModelRole::ValueObject;
    return std::nullopt;
}

std::optional<RetentionPolicy> ParseRetentionPolicy(std::string_view value)
{
    if (value == "durable") return RetentionPolicy::Durable;
    if (value == "until_expiry") return RetentionPolicy::UntilExpiry;
    if (value == "immutable") return RetentionPolicy::Immutable;
    if (value == "until_promotion") return RetentionPolicy::UntilPromotion;
    return std::nullopt;
}

std::optional<NameReviewStatus> ParseNameReviewStatus(std::string_view value)
{
    if (value == "canonical") return NameReviewStatus::Canonical;
    if (value == "review_required") return NameReviewStatus::ReviewRequired;
    if (value == "approved_exception") return NameReviewStatus::ApprovedException;
    return std::nullopt;
}

std::optional<FieldSourceKind> ParseFieldSourceKind(std::string_view value)
{
    if (value == "canonical") return FieldSourceKind::Canonical;
    if (value == "imported") return FieldSourceKind::Imported;
    if (value == "derived") return FieldSourceKind::Derived;
    if (value == "boundary_generated") return FieldSourceKind::BoundaryGenerated;
    return std::nullopt;
}

std::optional<KeyKind> ParseKeyKind(std::string_view value)
{
    if (value == "primary") return KeyKind::Primary;
    if (value == "alternate") return KeyKind::Alternate;
    return std::nullopt;
}

std::optional<RelationCardinality> ParseRelationCardinality(std::string_view value)
{
    if (value == "one") return RelationCardinality::One;
    if (value == "optional_one") return RelationCardinality::OptionalOne;
    if (value == "many") return RelationCardinality::Many;
    return std::nullopt;
}

std::optional<ReferentialAction> ParseReferentialAction(std::string_view value)
{
    if (value == "restrict") return ReferentialAction::Restrict;
    if (value == "cascade") return ReferentialAction::Cascade;
    if (value == "set_null") return ReferentialAction::SetNull;
    return std::nullopt;
}

std::optional<TemporalRole> ParseTemporalRole(std::string_view value)
{
    if (value == "none") return TemporalRole::None;
    if (value == "valid_time") return TemporalRole::ValidTime;
    if (value == "transaction_time") return TemporalRole::TransactionTime;
    if (value == "occurrence_time") return TemporalRole::OccurrenceTime;
    if (value == "observation_time") return TemporalRole::ObservationTime;
    if (value == "publication_time") return TemporalRole::PublicationTime;
    if (value == "boundary_creation_time") return TemporalRole::BoundaryCreationTime;
    return std::nullopt;
}

std::optional<FieldDefaultKind> ParseFieldDefaultKind(std::string_view value)
{
    if (value == "none") return FieldDefaultKind::None;
    if (value == "literal") return FieldDefaultKind::Literal;
    if (value == "boundary") return FieldDefaultKind::Boundary;
    return std::nullopt;
}

std::optional<BoundaryDefaultSource> ParseBoundaryDefaultSource(std::string_view value)
{
    if (value.empty() || value == "none") return BoundaryDefaultSource::None;
    if (value == "request_time") return BoundaryDefaultSource::RequestTime;
    if (value == "generated_id") return BoundaryDefaultSource::GeneratedId;
    if (value == "generated_token") return BoundaryDefaultSource::GeneratedToken;
    return std::nullopt;
}

std::optional<CheckKind> ParseCheckKind(std::string_view value)
{
    if (value == "minimum_integer") return CheckKind::MinimumInteger;
    if (value == "maximum_integer") return CheckKind::MaximumInteger;
    if (value == "minimum_real") return CheckKind::MinimumReal;
    if (value == "maximum_real") return CheckKind::MaximumReal;
    if (value == "minimum_length") return CheckKind::MinimumLength;
    if (value == "maximum_length") return CheckKind::MaximumLength;
    if (value == "fixed_length") return CheckKind::FixedLength;
    if (value == "enum_domain") return CheckKind::EnumDomain;
    if (value == "valid_json") return CheckKind::ValidJson;
    if (value == "decimal_precision") return CheckKind::DecimalPrecision;
    if (value == "decimal_scale") return CheckKind::DecimalScale;
    return std::nullopt;
}

std::optional<CheckUnit> ParseCheckUnit(std::string_view value)
{
    if (value == "none") return CheckUnit::None;
    if (value == "utf8_bytes") return CheckUnit::Utf8Bytes;
    if (value == "bytes") return CheckUnit::Bytes;
    return std::nullopt;
}

std::optional<QueryOperator> ParseQueryOperator(std::string_view value)
{
    if (value == "equal") return QueryOperator::Equal;
    if (value == "not_equal") return QueryOperator::NotEqual;
    if (value == "in") return QueryOperator::In;
    if (value == "not_in") return QueryOperator::NotIn;
    if (value == "less_than") return QueryOperator::LessThan;
    if (value == "less_than_or_equal") return QueryOperator::LessThanOrEqual;
    if (value == "greater_than") return QueryOperator::GreaterThan;
    if (value == "greater_than_or_equal") return QueryOperator::GreaterThanOrEqual;
    if (value == "prefix") return QueryOperator::Prefix;
    if (value == "suffix") return QueryOperator::Suffix;
    if (value == "contains") return QueryOperator::Contains;
    if (value == "is_null") return QueryOperator::IsNull;
    return std::nullopt;
}

std::optional<SerializationPolicy> ParseSerializationPolicy(std::string_view value)
{
    if (value == "include") return SerializationPolicy::Include;
    if (value == "omit") return SerializationPolicy::Omit;
    return std::nullopt;
}

std::optional<TemporalOrdering> ParseTemporalOrdering(std::string_view value)
{
    if (value == "strict") return TemporalOrdering::Strict;
    if (value == "nondecreasing") return TemporalOrdering::Nondecreasing;
    return std::nullopt;
}

std::optional<OpenEndPolicy> ParseOpenEndPolicy(std::string_view value)
{
    if (value == "forbidden") return OpenEndPolicy::Forbidden;
    if (value == "allowed") return OpenEndPolicy::Allowed;
    return std::nullopt;
}

std::optional<WirePrimitiveKind> ParseWirePrimitiveKind(std::string_view value)
{
    if (value == "bool") return WirePrimitiveKind::Bool;
    if (value == "u8") return WirePrimitiveKind::U8;
    if (value == "u16") return WirePrimitiveKind::U16;
    if (value == "u32") return WirePrimitiveKind::U32;
    if (value == "u64") return WirePrimitiveKind::U64;
    if (value == "i8") return WirePrimitiveKind::I8;
    if (value == "i16") return WirePrimitiveKind::I16;
    if (value == "i32") return WirePrimitiveKind::I32;
    if (value == "i64") return WirePrimitiveKind::I64;
    if (value == "f32") return WirePrimitiveKind::F32;
    if (value == "f64") return WirePrimitiveKind::F64;
    if (value == "utf8") return WirePrimitiveKind::Utf8;
    if (value == "bytes") return WirePrimitiveKind::Bytes;
    if (value == "fixed_bytes") return WirePrimitiveKind::FixedBytes;
    if (value == "decimal128") return WirePrimitiveKind::Decimal128;
    return std::nullopt;
}

std::optional<WireByteOrder> ParseWireByteOrder(std::string_view value)
{
    if (value == "none") return WireByteOrder::None;
    if (value == "big_endian") return WireByteOrder::BigEndian;
    return std::nullopt;
}

std::optional<StorageCodecKind> ParseStorageCodecKind(std::string_view value)
{
    if (value == "sqlite_integer") return StorageCodecKind::SqliteInteger;
    if (value == "sqlite_real") return StorageCodecKind::SqliteReal;
    if (value == "sqlite_text") return StorageCodecKind::SqliteText;
    if (value == "sqlite_blob") return StorageCodecKind::SqliteBlob;
    if (value == "sqlite_fixed_blob") return StorageCodecKind::SqliteFixedBlob;
    if (value == "sqlite_ordered_u64") return StorageCodecKind::SqliteOrderedU64;
    if (value == "sqlite_ordered_decimal128") return StorageCodecKind::SqliteOrderedDecimal128;
    return std::nullopt;
}

std::optional<SqliteStorageClass> ParseStorageClass(std::string_view value)
{
    if (value == "integer") return SqliteStorageClass::Integer;
    if (value == "real") return SqliteStorageClass::Real;
    if (value == "text") return SqliteStorageClass::Text;
    if (value == "blob") return SqliteStorageClass::Blob;
    return std::nullopt;
}

std::optional<SemanticTypeKind> ParseSemanticTypeKind(std::string_view value)
{
    if (value == "bool") return SemanticTypeKind::Bool;
    if (value == "u8") return SemanticTypeKind::U8;
    if (value == "u16") return SemanticTypeKind::U16;
    if (value == "u32") return SemanticTypeKind::U32;
    if (value == "u64") return SemanticTypeKind::U64;
    if (value == "i8") return SemanticTypeKind::I8;
    if (value == "i16") return SemanticTypeKind::I16;
    if (value == "i32") return SemanticTypeKind::I32;
    if (value == "i64") return SemanticTypeKind::I64;
    if (value == "f32") return SemanticTypeKind::F32;
    if (value == "f64") return SemanticTypeKind::F64;
    if (value == "text") return SemanticTypeKind::Text;
    if (value == "enum") return SemanticTypeKind::Enum;
    if (value == "blob") return SemanticTypeKind::Blob;
    if (value == "fixed_id") return SemanticTypeKind::FixedId;
    if (value == "decimal") return SemanticTypeKind::Decimal;
    if (value == "timestamp") return SemanticTypeKind::Timestamp;
    if (value == "json_text") return SemanticTypeKind::JsonText;
    return std::nullopt;
}

std::optional<FieldKind> ParseFieldKind(std::string_view value)
{
    if (value == "object_id") return FieldKind::ObjectId;
    if (value == "internal_id") return FieldKind::InternalId;
    if (value == "text") return FieldKind::Text;
    if (value == "slug") return FieldKind::Slug;
    if (value == "display_name") return FieldKind::DisplayName;
    if (value == "url") return FieldKind::Url;
    if (value == "email") return FieldKind::Email;
    if (value == "enum") return FieldKind::Enum;
    if (value == "bool") return FieldKind::Bool;
    if (value == "timestamp_ms") return FieldKind::TimestampMs;
    if (value == "json_text") return FieldKind::JsonText;
    if (value == "secret") return FieldKind::Secret;
    if (value == "ethereum_address") return FieldKind::EthereumAddress;
    if (value == "hash_hex") return FieldKind::HashHex;
    if (value == "token_hash") return FieldKind::TokenHash;
    if (value == "key_reference") return FieldKind::KeyReference;
    if (value == "integer") return FieldKind::Integer;
    if (value == "real") return FieldKind::Real;
    if (value == "decimal") return FieldKind::Decimal;
    if (value == "blob") return FieldKind::Blob;
    return std::nullopt;
}

std::optional<NormalizationKind> ParseNormalization(std::string_view value)
{
    if (value == "none") return NormalizationKind::None;
    if (value == "trim") return NormalizationKind::Trim;
    if (value == "lowercase") return NormalizationKind::Lowercase;
    if (value == "lowercase_trim") return NormalizationKind::LowercaseTrim;
    if (value == "ethereum_address") return NormalizationKind::EthereumAddress;
    if (value == "url") return NormalizationKind::Url;
    return std::nullopt;
}
}
