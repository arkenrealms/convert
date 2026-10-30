#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/identity.hpp"

namespace arken::schema::idl::v1 {
enum class DiagnosticSeverity {
  Error,
  Warning,
};

struct SourcePosition {
  std::size_t offset = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 1;

  [[nodiscard]] bool operator==(const SourcePosition &) const = default;
};

struct SourceSpan {
  SourcePosition begin;
  SourcePosition end;

  [[nodiscard]] bool operator==(const SourceSpan &) const = default;
};

struct RelatedDiagnosticSpan {
  std::string message;
  std::string sourceName;
  SourceSpan span;
};

struct Diagnostic {
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string message;
  std::string sourceName;
  SourceSpan span;
  std::vector<RelatedDiagnosticSpan> relatedSpans;
};

struct Comment {
  std::string text;
  SourceSpan span;
};

struct ImportDeclaration {
  std::string path;
  SourceSpan pathSpan;
  SourceSpan span;
};

struct CatalogFragmentDeclaration {
  std::string fragmentId;
  std::string ownerCapsule;
  std::uint32_t version = 0;
  std::uint32_t declarationOrder = 0;
  SourceSpan span;
};

struct CatalogDeclaration {
  std::string catalogId;
  std::uint32_t version = 0;
  std::vector<CatalogFragmentDeclaration> fragments;
  SourceSpan span;
};

struct CatalogFieldIdentityDeclaration {
  std::string registryKey;
  std::string fieldName;
  std::uint32_t declarationOrder = 0;
  SourceSpan span;
};

struct CatalogModelIdentityDeclaration {
  std::string fragmentId;
  std::string registryKey;
  std::string modelId;
  std::string logicalName;
  std::string qualifiedName;
  std::string domain;
  std::uint32_t declarationOrder = 0;
  SourceSpan span;
};

struct FieldDefaultDeclaration {
  std::string kind = "none";
  std::string literal;
  std::string boundarySource;
  std::string catalogValue;
  SourceSpan span;
};

struct CatalogMemberIdentityDeclaration {
  std::string name;
  std::uint32_t declarationOrder = 0;
  SourceSpan span;
};

struct CheckDeclaration {
  std::string name;
  std::string kind;
  std::string unit = "none";
  std::string value;
  std::string message;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct QueryPolicyDeclaration {
  std::vector<std::string> operators;
  bool sortable = false;
  bool cursor = false;
  SourceSpan span;
};

struct FieldRepresentationDeclaration {
  std::string fieldKind;
  std::string wireTypeName;
  std::uint32_t wireSize = 0;
  std::string storageCodecName;
  std::uint32_t storageSize = 0;
  std::uint32_t precision = 0;
  std::uint32_t scale = 0;
  std::string normalization;
  SourceSpan span;
};

struct FieldDeclaration {
  std::string name;
  std::optional<std::string> cppName;
  std::optional<std::string> csharpName;
  std::string storageName;
  std::string typeName;
  std::uint32_t fieldId = 0;
  std::optional<std::uint32_t> minimum;
  std::optional<std::uint32_t> maximumItems;
  bool optional = false;
  std::optional<std::string> sourceField;
  std::string sourceProvenance = "canonical";
  std::string importTransform;
  std::string serialization = "include";
  FieldDefaultDeclaration defaultValue;
  std::vector<CheckDeclaration> checks;
  QueryPolicyDeclaration query;
  bool required = true;
  bool redacted = false;
  bool immutable = false;
  bool writeOnce = false;
  bool primary = false;
  std::string temporalRole = "inherit";
  std::vector<std::string> allowedEnumValues;
  std::optional<FieldRepresentationDeclaration> representation;
  std::optional<CatalogFieldIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct WireTypeDeclaration {
  std::string name;
  std::uint32_t wireTypeId = 0;
  std::uint32_t bitWidth = 0;
  std::uint32_t fixedSize = 0;
  std::string byteOrder;
  SourceSpan span;
};

struct StorageCodecDeclaration {
  std::string name;
  std::uint32_t storageCodecId = 0;
  std::string storageClass;
  std::uint32_t fixedSize = 0;
  bool exact = false;
  bool orderPreserving = false;
  SourceSpan span;
};

struct SemanticTypeDeclaration {
  std::string name;
  std::uint32_t semanticTypeId = 0;
  bool semanticTypeIdExplicit = false;
  std::string semanticName;
  std::string registryKey;
  std::string kind;
  std::string wireTypeName;
  std::string storageCodecName;
  std::uint32_t fixedSize = 0;
  std::uint32_t precision = 0;
  std::uint32_t scale = 0;
  std::string normalization = "none";
  std::string jsonProfile = "none";
  std::string boundaryMapping = "scalar";
  std::string temporalRole = "none";
  std::string presence = "explicit";
  std::string defaultPolicy = "none";
  std::string narrowing = "reject";
  std::string enumName;
  std::string representation = "automatic";
  std::vector<std::string> targets;
  SourceSpan span;
};

struct ReservedIdDeclaration {
  RegistryKind registry = RegistryKind::Model;
  std::uint32_t value = 0;
  SourceSpan span;
};

struct ReservedNameDeclaration {
  RegistryKind registry = RegistryKind::Model;
  std::string value;
  SourceSpan span;
};

struct EnumValueDeclaration {
  std::string name;
  std::uint32_t valueId = 0;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct EnumDeclaration {
  std::string name;
  std::uint32_t enumId = 0;
  std::string semanticTypeName;
  std::vector<EnumValueDeclaration> values;
  std::vector<ReservedIdDeclaration> reservedIds;
  std::vector<ReservedNameDeclaration> reservedNames;
  SourceSpan span;
};

struct KeyDeclaration {
  std::string name;
  std::string kind;
  std::vector<std::string> fields;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct IndexDeclaration {
  std::string name;
  std::vector<std::string> fields;
  bool unique = false;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct RelationDeclaration {
  std::string name;
  std::string targetModel;
  std::vector<std::string> sourceFields;
  std::vector<std::string> targetFields;
  std::string cardinality;
  std::string onDelete;
  std::string onUpdate;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct JoinDeclaration {
  std::string name;
  std::string throughModel;
  std::string sourceRelation;
  std::string targetRelation;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct OneOfVariantDeclaration {
  std::string tag;
  std::vector<std::string> fields;
  std::vector<std::string> optionalFields;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct OneOfDeclaration {
  std::string name;
  std::string discriminatorField;
  std::vector<OneOfVariantDeclaration> variants;
  std::optional<CatalogMemberIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct TemporalContractDeclaration {
  std::string name;
  std::string startField;
  std::string endField;
  std::string ordering;
  std::string openEnd;
  bool startInclusive = true;
  bool endInclusive = false;
  SourceSpan span;
};

struct ModelDeclaration {
  std::string name;
  bool external = false;
  std::string storageName;
  std::string domain;
  std::string description;
  std::string kind = "entity";
  std::string retention = "durable";
  std::string mutationAuthority;
  std::string role = "unspecified";
  std::vector<std::string> sourceAliases;
  std::vector<std::string> retiredNames;
  std::vector<std::string> catalogRetiredNames;
  std::string nameReview = "canonical";
  std::string nameReviewRationale;
  std::uint32_t typeId = 0;
  std::vector<FieldDeclaration> fields;
  std::vector<KeyDeclaration> keys;
  std::vector<IndexDeclaration> indexes;
  std::vector<RelationDeclaration> relations;
  std::vector<JoinDeclaration> joins;
  std::vector<OneOfDeclaration> oneOfs;
  std::vector<TemporalContractDeclaration> temporalContracts;
  std::vector<ReservedIdDeclaration> reservedIds;
  std::vector<ReservedNameDeclaration> reservedNames;
  std::optional<CatalogModelIdentityDeclaration> catalogIdentity;
  SourceSpan span;
};

struct CommandDeclaration {
  std::string name;
  std::uint32_t commandId = 0;
  std::string requestModelName;
  std::string responseModelName;
  std::vector<std::string> errorModelNames;
  std::string idempotency;
  std::string streaming;
  std::vector<std::string> targets;
  std::optional<std::string> fabricLane;
  std::optional<std::uint32_t> maximumEncodedBytes;
  std::optional<bool> directEligible;
  SourceSpan span;
};

struct EventDeclaration {
  std::string name;
  std::uint32_t eventId = 0;
  std::string payloadModelName;
  std::string delivery;
  std::string ordering;
  std::string replay;
  std::vector<std::string> targets;
  std::optional<std::string> fabricLane;
  std::optional<std::uint32_t> maximumEncodedBytes;
  std::optional<bool> directEligible;
  SourceSpan span;
};

struct Document {
  std::uint32_t edition = 0;
  std::string schemaName;
  std::uint32_t schemaVersion = 0;
  std::string ownerCapsule;
  std::optional<CatalogDeclaration> catalog;
  std::vector<Comment> comments;
  std::vector<ImportDeclaration> imports;
  std::vector<ReservedIdDeclaration> reservedIds;
  std::vector<ReservedNameDeclaration> reservedNames;
  std::vector<WireTypeDeclaration> wireTypes;
  std::vector<StorageCodecDeclaration> storageCodecs;
  std::vector<SemanticTypeDeclaration> semanticTypes;
  std::vector<EnumDeclaration> enums;
  std::vector<ModelDeclaration> models;
  std::vector<CommandDeclaration> commands;
  std::vector<EventDeclaration> events;
};

struct ParseLimits {
  std::size_t maximumInputBytes = 1024 * 1024;
  std::size_t maximumManifestBytes = 16 * 1024 * 1024;
  std::size_t maximumTotalInputBytes = 16 * 1024 * 1024;
  std::size_t maximumTokens = 100000;
  std::size_t maximumTokenBytes = 64 * 1024;
  std::size_t maximumStringBytes = 64 * 1024;
  std::size_t maximumPathBytes = 4096;
  std::size_t maximumCommentBytes = 64 * 1024;
  std::size_t maximumComments = 10000;
  std::size_t maximumDiagnostics = 100;
  std::size_t maximumCatalogFragments = 4096;
  std::size_t maximumModels = 4096;
  std::size_t maximumFieldsPerModel = 16384;
  std::size_t maximumWireTypes = 4096;
  std::size_t maximumStorageCodecs = 4096;
  std::size_t maximumSemanticTypes = 65536;
  std::size_t maximumEnums = 65536;
  std::size_t maximumEnumValuesPerEnum = 65536;
  std::size_t maximumKeysPerModel = 4096;
  std::size_t maximumIndexesPerModel = 16384;
  std::size_t maximumRelationsPerModel = 16384;
  std::size_t maximumJoinsPerModel = 4096;
  std::size_t maximumOneOfsPerModel = 4096;
  std::size_t maximumOneOfVariants = 4096;
  std::size_t maximumChecksPerField = 256;
  std::size_t maximumTemporalContractsPerModel = 4096;
  std::size_t maximumStructuralFields = 1024;
  std::size_t maximumCommands = 65536;
  std::size_t maximumEvents = 65536;
  std::size_t maximumImportsPerSource = 4096;
  std::size_t maximumReservations = 65536;
  std::size_t maximumSources = 4096;
  std::size_t maximumImportDepth = 128;
};

struct ParseResult {
  std::optional<Document> document;
  std::vector<Diagnostic> diagnostics;
  std::string normalizedText;

  [[nodiscard]] bool ok() const;
};

struct SourceFile {
  std::string path;
  std::string text;
  // Project-relative prefix applied to this source's root-relative imports.
  std::string importRoot;
};

struct ParsedSource {
  std::string path;
  Document document;
  std::string normalizedText;
};

struct Project {
  std::string entryPath;
  std::vector<ParsedSource> sources;
};

struct ProjectParseResult {
  std::optional<Project> project;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const;
};

[[nodiscard]] bool IsCanonicalSnakeCase(std::string_view value);
[[nodiscard]] bool IsCanonicalQualifiedName(std::string_view value);
[[nodiscard]] std::string Format(const Document &document);
[[nodiscard]] ParseResult Parse(std::string_view source_name,
                                std::string_view source,
                                const ParseLimits &limits = {});
[[nodiscard]] ProjectParseResult
ParseProject(std::string_view entry_path,
             const std::vector<SourceFile> &sources,
             const ParseLimits &limits = {});
} // namespace arken::schema::idl::v1
