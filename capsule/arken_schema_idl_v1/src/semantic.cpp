#include "arken_schema_idl_v1/semantic.hpp"

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
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "semantic_internal.hpp"
#include "semantic_token_parsing.hpp"

namespace arken::schema::idl::v1 {
namespace {
using detail::ParseBoundaryDefaultSource;
using detail::ParseCheckKind;
using detail::ParseCheckUnit;
using detail::ParseFabricLane;
using detail::ParseFieldDefaultKind;
using detail::ParseFieldKind;
using detail::ParseFieldSourceKind;
using detail::ParseKeyKind;
using detail::ParseModelKind;
using detail::ParseModelRole;
using detail::ParseNameReviewStatus;
using detail::ParseNormalization;
using detail::ParseOpenEndPolicy;
using detail::ParseOperationTargetProfile;
using detail::ParseQueryOperator;
using detail::ParseReferentialAction;
using detail::ParseRelationCardinality;
using detail::ParseRetentionPolicy;
using detail::ParseSemanticTypeKind;
using detail::ParseSerializationPolicy;
using detail::ParseStorageClass;
using detail::ParseStorageCodecKind;
using detail::ParseTargetProfile;
using detail::ParseTemporalOrdering;
using detail::ParseTemporalRole;
using detail::ParseWireByteOrder;
using detail::ParseWirePrimitiveKind;

struct ModelCandidate {
  const ParsedSource *source = nullptr;
  const ModelDeclaration *model = nullptr;
  std::string qualifiedName;
};

struct WireTypeCandidate {
  const ParsedSource *source = nullptr;
  const WireTypeDeclaration *declaration = nullptr;
  std::string qualifiedName;
};

struct StorageCodecCandidate {
  const ParsedSource *source = nullptr;
  const StorageCodecDeclaration *declaration = nullptr;
  std::string qualifiedName;
};

struct SemanticTypeCandidate {
  const ParsedSource *source = nullptr;
  const SemanticTypeDeclaration *declaration = nullptr;
  const SemanticModel *definingModel = nullptr;
  std::string name;
  std::string qualifiedName;
  std::uint32_t semanticTypeId = 0;
};

struct EnumCandidate {
  const ParsedSource *source = nullptr;
  const EnumDeclaration *declaration = nullptr;
  std::string qualifiedName;
};

struct CommandCandidate {
  const ParsedSource *source = nullptr;
  const CommandDeclaration *declaration = nullptr;
  std::string qualifiedName;
};

struct EventCandidate {
  const ParsedSource *source = nullptr;
  const EventDeclaration *declaration = nullptr;
  std::string qualifiedName;
};

std::optional<SemanticCatalogMemberIdentity> CatalogMemberIdentity(
    const std::optional<CatalogMemberIdentityDeclaration> &identity) {
  if (!identity.has_value())
    return std::nullopt;
  return SemanticCatalogMemberIdentity{
      .name = identity->name,
      .declarationOrder = identity->declarationOrder,
  };
}

template <typename Member>
bool CatalogMemberLess(const Member &lhs, const Member &rhs) {
  if (lhs.catalogIdentity.has_value() != rhs.catalogIdentity.has_value())
    return lhs.catalogIdentity.has_value();
  if (lhs.catalogIdentity.has_value() &&
      lhs.catalogIdentity->declarationOrder !=
          rhs.catalogIdentity->declarationOrder)
    return lhs.catalogIdentity->declarationOrder <
           rhs.catalogIdentity->declarationOrder;
  return lhs.name < rhs.name;
}

bool CanonicalIntegerToken(std::string_view value, bool allow_negative) {
  if (value.empty() || value.front() == '+')
    return false;
  std::size_t offset = 0;
  if (value.front() == '-') {
    if (!allow_negative || value.size() == 1)
      return false;
    offset = 1;
  }
  if (value[offset] == '0' && value.size() - offset > 1)
    return false;
  return std::all_of(
      value.begin() + static_cast<std::ptrdiff_t>(offset), value.end(),
      [](char value_at) { return value_at >= '0' && value_at <= '9'; });
}

template <typename Integer>
bool ParseExactInteger(std::string_view value, Integer &output) {
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), output);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool IntegerLiteralFits(SemanticTypeKind kind, std::string_view value) {
  const bool signed_kind =
      kind == SemanticTypeKind::I8 || kind == SemanticTypeKind::I16 ||
      kind == SemanticTypeKind::I32 || kind == SemanticTypeKind::I64 ||
      kind == SemanticTypeKind::Timestamp;
  if (!CanonicalIntegerToken(value, signed_kind))
    return false;
  if (signed_kind) {
    std::int64_t parsed = 0;
    if (!ParseExactInteger(value, parsed))
      return false;
    if (kind == SemanticTypeKind::I8)
      return parsed >= std::numeric_limits<std::int8_t>::min() &&
             parsed <= std::numeric_limits<std::int8_t>::max();
    if (kind == SemanticTypeKind::I16)
      return parsed >= std::numeric_limits<std::int16_t>::min() &&
             parsed <= std::numeric_limits<std::int16_t>::max();
    if (kind == SemanticTypeKind::I32)
      return parsed >= std::numeric_limits<std::int32_t>::min() &&
             parsed <= std::numeric_limits<std::int32_t>::max();
    return true;
  }
  std::uint64_t parsed = 0;
  if (!ParseExactInteger(value, parsed))
    return false;
  if (kind == SemanticTypeKind::U8)
    return parsed <= std::numeric_limits<std::uint8_t>::max();
  if (kind == SemanticTypeKind::U16)
    return parsed <= std::numeric_limits<std::uint16_t>::max();
  if (kind == SemanticTypeKind::U32)
    return parsed <= std::numeric_limits<std::uint32_t>::max();
  return kind == SemanticTypeKind::U64;
}

bool RealLiteralValid(std::string_view value) {
  const auto parsed =
      nlohmann::json::parse(value.begin(), value.end(), nullptr, false, false);
  return !parsed.is_discarded() && parsed.is_number() &&
         std::isfinite(parsed.get<double>());
}

bool DecimalLiteralValid(std::string_view value, std::uint32_t precision,
                         std::uint32_t scale) {
  if (value.empty() || value.front() == '+')
    return false;
  std::size_t offset = value.front() == '-' ? 1 : 0;
  if (offset == value.size())
    return false;
  const auto point = value.find('.', offset);
  const auto integer_end =
      point == std::string_view::npos ? value.size() : point;
  if (integer_end == offset ||
      (value[offset] == '0' && integer_end - offset > 1))
    return false;
  const auto digit = [](char value_at) {
    return value_at >= '0' && value_at <= '9';
  };
  if (!std::all_of(value.begin() + static_cast<std::ptrdiff_t>(offset),
                   value.begin() + static_cast<std::ptrdiff_t>(integer_end),
                   digit))
    return false;
  std::size_t fraction = 0;
  if (point != std::string_view::npos) {
    fraction = value.size() - point - 1;
    if (fraction == 0 ||
        !std::all_of(value.begin() + static_cast<std::ptrdiff_t>(point + 1),
                     value.end(), digit))
      return false;
  }
  return integer_end - offset + fraction <= precision && fraction <= scale;
}

bool LengthValueValid(std::string_view value) {
  if (!CanonicalIntegerToken(value, false))
    return false;
  std::uint32_t parsed = 0;
  return ParseExactInteger(value, parsed);
}

bool IsIntegerKind(SemanticTypeKind kind) {
  return kind == SemanticTypeKind::U8 || kind == SemanticTypeKind::U16 ||
         kind == SemanticTypeKind::U32 || kind == SemanticTypeKind::U64 ||
         kind == SemanticTypeKind::I8 || kind == SemanticTypeKind::I16 ||
         kind == SemanticTypeKind::I32 || kind == SemanticTypeKind::I64 ||
         kind == SemanticTypeKind::Timestamp;
}

bool ExplicitCheckCompatible(CheckKind check, SemanticTypeKind type,
                             WirePrimitiveKind wire) {
  switch (check) {
  case CheckKind::MinimumInteger:
  case CheckKind::MaximumInteger:
    return IsIntegerKind(type);
  case CheckKind::MinimumReal:
  case CheckKind::MaximumReal:
    return type == SemanticTypeKind::F32 || type == SemanticTypeKind::F64 ||
           type == SemanticTypeKind::Decimal;
  case CheckKind::MinimumLength:
  case CheckKind::MaximumLength:
  case CheckKind::FixedLength:
    return wire == WirePrimitiveKind::Utf8 ||
           wire == WirePrimitiveKind::Bytes ||
           wire == WirePrimitiveKind::FixedBytes;
  case CheckKind::EnumDomain:
  case CheckKind::ValidJson:
  case CheckKind::DecimalPrecision:
  case CheckKind::DecimalScale:
    return false;
  }
  return false;
}

bool QueryOperatorCompatible(QueryOperator operation, SemanticTypeKind type,
                             bool required) {
  if (operation == QueryOperator::IsNull)
    return !required;
  if (type == SemanticTypeKind::JsonText)
    return false;
  if (operation == QueryOperator::Prefix ||
      operation == QueryOperator::Suffix ||
      operation == QueryOperator::Contains)
    return type == SemanticTypeKind::Text;
  if (operation == QueryOperator::LessThan ||
      operation == QueryOperator::LessThanOrEqual ||
      operation == QueryOperator::GreaterThan ||
      operation == QueryOperator::GreaterThanOrEqual)
    return type != SemanticTypeKind::Bool &&
           type != SemanticTypeKind::FixedId && type != SemanticTypeKind::Blob;
  return true;
}

bool HasAllValidationTargets(const std::vector<TargetProfile> &targets) {
  const auto includes =
      [&targets](std::initializer_list<TargetProfile> required) {
        return std::all_of(required.begin(), required.end(),
                           [&targets](TargetProfile target) {
                             return std::find(targets.begin(), targets.end(),
                                              target) != targets.end();
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

std::optional<CommandIdempotency>
ParseCommandIdempotency(std::string_view value) {
  if (value == "none")
    return CommandIdempotency::None;
  if (value == "idempotent")
    return CommandIdempotency::Idempotent;
  if (value == "safe")
    return CommandIdempotency::Safe;
  return std::nullopt;
}

std::optional<CommandStreaming> ParseCommandStreaming(std::string_view value) {
  if (value == "unary")
    return CommandStreaming::Unary;
  if (value == "server_stream")
    return CommandStreaming::ServerStream;
  if (value == "client_stream")
    return CommandStreaming::ClientStream;
  if (value == "bidirectional")
    return CommandStreaming::Bidirectional;
  return std::nullopt;
}

std::optional<EventDelivery> ParseEventDelivery(std::string_view value) {
  if (value == "best_effort")
    return EventDelivery::BestEffort;
  if (value == "at_least_once")
    return EventDelivery::AtLeastOnce;
  return std::nullopt;
}

std::optional<EventOrdering> ParseEventOrdering(std::string_view value) {
  if (value == "none")
    return EventOrdering::None;
  if (value == "source")
    return EventOrdering::Source;
  if (value == "aggregate")
    return EventOrdering::Aggregate;
  if (value == "global")
    return EventOrdering::Global;
  return std::nullopt;
}

std::optional<EventReplay> ParseEventReplay(std::string_view value) {
  if (value == "none")
    return EventReplay::None;
  if (value == "retained")
    return EventReplay::Retained;
  if (value == "resumable")
    return EventReplay::Resumable;
  return std::nullopt;
}

std::string QualifiedDeclarationName(const Document &document,
                                     std::string_view name) {
  return document.schemaName + ".v" + std::to_string(document.schemaVersion) +
         "." + std::string(name);
}

struct WireShape {
  std::uint32_t bitWidth;
  std::uint32_t fixedSize;
  WireByteOrder byteOrder;
};

WireShape ExpectedWireShape(WirePrimitiveKind kind) {
  switch (kind) {
  case WirePrimitiveKind::Bool:
    return {1, 1, WireByteOrder::None};
  case WirePrimitiveKind::U8:
  case WirePrimitiveKind::I8:
    return {8, 1, WireByteOrder::None};
  case WirePrimitiveKind::U16:
  case WirePrimitiveKind::I16:
    return {16, 2, WireByteOrder::BigEndian};
  case WirePrimitiveKind::U32:
  case WirePrimitiveKind::I32:
  case WirePrimitiveKind::F32:
    return {32, 4, WireByteOrder::BigEndian};
  case WirePrimitiveKind::U64:
  case WirePrimitiveKind::I64:
  case WirePrimitiveKind::F64:
    return {64, 8, WireByteOrder::BigEndian};
  case WirePrimitiveKind::Decimal128:
    return {128, 16, WireByteOrder::BigEndian};
  case WirePrimitiveKind::Utf8:
  case WirePrimitiveKind::Bytes:
  case WirePrimitiveKind::FixedBytes:
    return {0, 0, WireByteOrder::None};
  }
  return {};
}

std::uint32_t ExpectedWireTypeId(WirePrimitiveKind kind) {
  return static_cast<std::uint32_t>(kind);
}

std::pair<SqliteStorageClass, std::uint32_t>
ExpectedStorageShape(StorageCodecKind kind) {
  switch (kind) {
  case StorageCodecKind::SqliteInteger:
    return {SqliteStorageClass::Integer, 0};
  case StorageCodecKind::SqliteReal:
    return {SqliteStorageClass::Real, 0};
  case StorageCodecKind::SqliteText:
    return {SqliteStorageClass::Text, 0};
  case StorageCodecKind::SqliteBlob:
  case StorageCodecKind::SqliteFixedBlob:
    return {SqliteStorageClass::Blob, 0};
  case StorageCodecKind::SqliteOrderedU64:
    return {SqliteStorageClass::Blob, 8};
  case StorageCodecKind::SqliteOrderedDecimal128:
    return {SqliteStorageClass::Blob, 16};
  }
  return {};
}

std::uint32_t ExpectedStorageCodecId(StorageCodecKind kind) {
  return static_cast<std::uint32_t>(kind);
}

bool UsesSchemaV2(const Project &project) {
  return std::any_of(
      project.sources.begin(), project.sources.end(),
      [](const auto &source) { return source.document.edition == 2; });
}

std::optional<WirePrimitiveKind> DefaultWireKind(SemanticTypeKind semantic,
                                                 std::uint32_t fixed_size) {
  switch (semantic) {
  case SemanticTypeKind::Bool:
    return WirePrimitiveKind::Bool;
  case SemanticTypeKind::U8:
    return WirePrimitiveKind::U8;
  case SemanticTypeKind::U16:
    return WirePrimitiveKind::U16;
  case SemanticTypeKind::U32:
    return WirePrimitiveKind::U32;
  case SemanticTypeKind::U64:
    return WirePrimitiveKind::U64;
  case SemanticTypeKind::I8:
    return WirePrimitiveKind::I8;
  case SemanticTypeKind::I16:
    return WirePrimitiveKind::I16;
  case SemanticTypeKind::I32:
    return WirePrimitiveKind::I32;
  case SemanticTypeKind::I64:
  case SemanticTypeKind::Timestamp:
    return WirePrimitiveKind::I64;
  case SemanticTypeKind::F32:
    return WirePrimitiveKind::F32;
  case SemanticTypeKind::F64:
    return WirePrimitiveKind::F64;
  case SemanticTypeKind::Text:
  case SemanticTypeKind::JsonText:
  case SemanticTypeKind::Enum:
    return WirePrimitiveKind::Utf8;
  case SemanticTypeKind::Blob:
    return fixed_size == 0u ? WirePrimitiveKind::Bytes
                            : WirePrimitiveKind::FixedBytes;
  case SemanticTypeKind::FixedId:
    return WirePrimitiveKind::FixedBytes;
  case SemanticTypeKind::Decimal:
    return WirePrimitiveKind::Decimal128;
  }
  return std::nullopt;
}

enum class ValueRepresentationIntent : std::uint8_t {
  Automatic,
  Native,
  EnumName,
  EnumId,
  Bytes,
  HexText,
};

std::optional<ValueRepresentationIntent>
ParseValueRepresentation(std::string_view value) {
  if (value == "automatic")
    return ValueRepresentationIntent::Automatic;
  if (value == "native")
    return ValueRepresentationIntent::Native;
  if (value == "enum_name")
    return ValueRepresentationIntent::EnumName;
  if (value == "enum_id")
    return ValueRepresentationIntent::EnumId;
  if (value == "bytes")
    return ValueRepresentationIntent::Bytes;
  if (value == "hex_text")
    return ValueRepresentationIntent::HexText;
  return std::nullopt;
}

ValueRepresentationIntent
ResolveValueRepresentation(SemanticTypeKind semantic, WirePrimitiveKind wire,
                           ValueRepresentationIntent requested) {
  if (requested != ValueRepresentationIntent::Automatic)
    return requested;
  if (semantic == SemanticTypeKind::Enum)
    return wire == WirePrimitiveKind::Utf8 ? ValueRepresentationIntent::EnumName
                                           : ValueRepresentationIntent::EnumId;
  if (semantic == SemanticTypeKind::Blob ||
      semantic == SemanticTypeKind::FixedId)
    return ValueRepresentationIntent::Bytes;
  return ValueRepresentationIntent::Native;
}

bool ValueRepresentationCompatible(SemanticTypeKind semantic,
                                   WirePrimitiveKind wire,
                                   ValueRepresentationIntent representation) {
  switch (representation) {
  case ValueRepresentationIntent::Automatic:
    return true;
  case ValueRepresentationIntent::Native:
    return semantic != SemanticTypeKind::Enum &&
           semantic != SemanticTypeKind::Blob &&
           semantic != SemanticTypeKind::FixedId;
  case ValueRepresentationIntent::EnumName:
    return semantic == SemanticTypeKind::Enum;
  case ValueRepresentationIntent::EnumId:
    return semantic == SemanticTypeKind::Enum &&
           (wire == WirePrimitiveKind::U8 || wire == WirePrimitiveKind::U16 ||
            wire == WirePrimitiveKind::U32);
  case ValueRepresentationIntent::Bytes:
    return semantic == SemanticTypeKind::Blob ||
           semantic == SemanticTypeKind::FixedId;
  case ValueRepresentationIntent::HexText:
    return (semantic == SemanticTypeKind::Blob ||
            semantic == SemanticTypeKind::FixedId) &&
           wire == WirePrimitiveKind::FixedBytes;
  }
  return false;
}

std::optional<StorageCodecKind>
DefaultStorageKind(SemanticTypeKind semantic, WirePrimitiveKind wire,
                   ValueRepresentationIntent representation) {
  const auto resolved =
      ResolveValueRepresentation(semantic, wire, representation);
  switch (semantic) {
  case SemanticTypeKind::Bool:
  case SemanticTypeKind::U8:
  case SemanticTypeKind::U16:
  case SemanticTypeKind::U32:
  case SemanticTypeKind::I8:
  case SemanticTypeKind::I16:
  case SemanticTypeKind::I32:
  case SemanticTypeKind::I64:
  case SemanticTypeKind::Timestamp:
    return StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::U64:
    return StorageCodecKind::SqliteOrderedU64;
  case SemanticTypeKind::F32:
  case SemanticTypeKind::F64:
    return StorageCodecKind::SqliteReal;
  case SemanticTypeKind::Text:
  case SemanticTypeKind::JsonText:
    return StorageCodecKind::SqliteText;
  case SemanticTypeKind::Enum:
    return resolved == ValueRepresentationIntent::EnumName
               ? StorageCodecKind::SqliteText
               : StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::Blob:
    if (resolved == ValueRepresentationIntent::HexText)
      return StorageCodecKind::SqliteText;
    return wire == WirePrimitiveKind::FixedBytes
               ? StorageCodecKind::SqliteFixedBlob
               : StorageCodecKind::SqliteBlob;
  case SemanticTypeKind::FixedId:
    return resolved == ValueRepresentationIntent::HexText
               ? StorageCodecKind::SqliteText
               : StorageCodecKind::SqliteFixedBlob;
  case SemanticTypeKind::Decimal:
    return StorageCodecKind::SqliteOrderedDecimal128;
  }
  return std::nullopt;
}

bool TypeBindingCompatible(SemanticTypeKind semantic, WirePrimitiveKind wire,
                           StorageCodecKind storage) {
  switch (semantic) {
  case SemanticTypeKind::Bool:
    return wire == WirePrimitiveKind::Bool &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::U8:
    return wire == WirePrimitiveKind::U8 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::U16:
    return wire == WirePrimitiveKind::U16 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::U32:
    return wire == WirePrimitiveKind::U32 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::U64:
    return wire == WirePrimitiveKind::U64 &&
           storage == StorageCodecKind::SqliteOrderedU64;
  case SemanticTypeKind::I8:
    return wire == WirePrimitiveKind::I8 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::I16:
    return wire == WirePrimitiveKind::I16 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::I32:
    return wire == WirePrimitiveKind::I32 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::I64:
  case SemanticTypeKind::Timestamp:
    return wire == WirePrimitiveKind::I64 &&
           storage == StorageCodecKind::SqliteInteger;
  case SemanticTypeKind::F32:
    return wire == WirePrimitiveKind::F32 &&
           storage == StorageCodecKind::SqliteReal;
  case SemanticTypeKind::F64:
    return wire == WirePrimitiveKind::F64 &&
           storage == StorageCodecKind::SqliteReal;
  case SemanticTypeKind::Text:
  case SemanticTypeKind::JsonText:
    return wire == WirePrimitiveKind::Utf8 &&
           storage == StorageCodecKind::SqliteText;
  case SemanticTypeKind::Enum:
    return ((wire == WirePrimitiveKind::U8 || wire == WirePrimitiveKind::U16 ||
             wire == WirePrimitiveKind::U32) &&
            (storage == StorageCodecKind::SqliteInteger ||
             storage == StorageCodecKind::SqliteText)) ||
           (wire == WirePrimitiveKind::Utf8 &&
            storage == StorageCodecKind::SqliteText);
  case SemanticTypeKind::Blob:
    return (wire == WirePrimitiveKind::Bytes &&
            storage == StorageCodecKind::SqliteBlob) ||
           (wire == WirePrimitiveKind::FixedBytes &&
            (storage == StorageCodecKind::SqliteFixedBlob ||
             storage == StorageCodecKind::SqliteText));
  case SemanticTypeKind::FixedId:
    return wire == WirePrimitiveKind::FixedBytes &&
           (storage == StorageCodecKind::SqliteFixedBlob ||
            storage == StorageCodecKind::SqliteText);
  case SemanticTypeKind::Decimal:
    return wire == WirePrimitiveKind::Decimal128 &&
           storage == StorageCodecKind::SqliteOrderedDecimal128;
  }
  return false;
}

FieldKind DefaultFieldKind(SemanticTypeKind kind) {
  switch (kind) {
  case SemanticTypeKind::Bool:
    return FieldKind::Bool;
  case SemanticTypeKind::U8:
  case SemanticTypeKind::U16:
  case SemanticTypeKind::U32:
  case SemanticTypeKind::U64:
  case SemanticTypeKind::I8:
  case SemanticTypeKind::I16:
  case SemanticTypeKind::I32:
  case SemanticTypeKind::I64:
    return FieldKind::Integer;
  case SemanticTypeKind::F32:
  case SemanticTypeKind::F64:
    return FieldKind::Real;
  case SemanticTypeKind::Text:
    return FieldKind::Text;
  case SemanticTypeKind::Enum:
    return FieldKind::Enum;
  case SemanticTypeKind::Blob:
    return FieldKind::Blob;
  case SemanticTypeKind::FixedId:
    return FieldKind::Blob;
  case SemanticTypeKind::Decimal:
    return FieldKind::Decimal;
  case SemanticTypeKind::Timestamp:
    return FieldKind::TimestampMs;
  case SemanticTypeKind::JsonText:
    return FieldKind::JsonText;
  }
  return FieldKind::Text;
}

bool FieldKindCompatible(FieldKind field, SemanticTypeKind semantic) {
  switch (field) {
  case FieldKind::ObjectId:
    return semantic == SemanticTypeKind::FixedId ||
           semantic == SemanticTypeKind::Text;
  case FieldKind::InternalId:
  case FieldKind::Integer:
    return semantic == SemanticTypeKind::U8 ||
           semantic == SemanticTypeKind::U16 ||
           semantic == SemanticTypeKind::U32 ||
           semantic == SemanticTypeKind::U64 ||
           semantic == SemanticTypeKind::I8 ||
           semantic == SemanticTypeKind::I16 ||
           semantic == SemanticTypeKind::I32 ||
           semantic == SemanticTypeKind::I64;
  case FieldKind::Text:
  case FieldKind::Slug:
  case FieldKind::DisplayName:
  case FieldKind::Url:
  case FieldKind::Email:
  case FieldKind::Secret:
  case FieldKind::KeyReference:
    return semantic == SemanticTypeKind::Text;
  case FieldKind::Enum:
    return semantic == SemanticTypeKind::Enum;
  case FieldKind::Bool:
    return semantic == SemanticTypeKind::Bool;
  case FieldKind::TimestampMs:
    return semantic == SemanticTypeKind::Timestamp;
  case FieldKind::JsonText:
    return semantic == SemanticTypeKind::JsonText;
  case FieldKind::EthereumAddress:
  case FieldKind::HashHex:
  case FieldKind::TokenHash:
    return semantic == SemanticTypeKind::Blob ||
           semantic == SemanticTypeKind::Text;
  case FieldKind::Real:
    return semantic == SemanticTypeKind::F32 ||
           semantic == SemanticTypeKind::F64;
  case FieldKind::Decimal:
    return semantic == SemanticTypeKind::Decimal;
  case FieldKind::Blob:
    return semantic == SemanticTypeKind::Blob ||
           semantic == SemanticTypeKind::FixedId;
  }
  return false;
}

class SemanticBuilder {
public:
  explicit SemanticBuilder(const ParseLimits &limits) : limits_(limits) {}

  SemanticManifestResult Build(const Project &project) {
    if (limits_.maximumDiagnostics == 0) {
      AddDiagnostic("idl.invalid_parse_limits",
                    "The diagnostic limit must be greater than zero.",
                    "<semantic>", {});
      return Finish();
    }
    if (project.sources.empty()) {
      AddDiagnostic("idl.semantic_project_empty",
                    "Semantic resolution requires at least one parsed source.",
                    "<semantic>", {});
      return Finish();
    }
    if (project.sources.size() > limits_.maximumSources ||
        project.sources.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.semantic_source_limit_exceeded",
                    "Semantic source count exceeds the supported handle range.",
                    "<semantic>", {});
      return Finish();
    }

    BuildSources(project);
    ResolveEntry(project);
    ResolveImports(project);
    BuildCatalog(project);
    BuildWireTypes(project);
    BuildStorageCodecs(project);
    BuildModels(project);
    BuildSemanticTypes(project);
    BuildEnums(project);
    BuildFields();
    BuildStructures();
    BuildTemporalContractsAndValidateQueries();
    BuildCommands(project);
    BuildEvents(project);
    BuildReservations(project);
    return Finish();
  }

private:
  void BuildSources(const Project &project) {
    std::vector<const ParsedSource *> sources;
    sources.reserve(project.sources.size());
    for (const auto &source : project.sources)
      sources.push_back(&source);
    std::sort(
        sources.begin(), sources.end(),
        [](const auto *lhs, const auto *rhs) { return lhs->path < rhs->path; });

    std::string previous_path;
    for (std::size_t index = 0; index < sources.size(); ++index) {
      const auto *source = sources[index];
      if (!previous_path.empty() && previous_path == source->path) {
        AddDiagnostic("idl.semantic_duplicate_source",
                      "Semantic source paths must be unique.", source->path,
                      {});
        continue;
      }
      previous_path = source->path;
      const SourceHandle handle{static_cast<std::uint32_t>(ir_.sources.size())};
      sourceHandles_.emplace(source->path, handle);
      sourceByHandle_.push_back(source);
      ir_.sources.push_back({
          .handle = handle,
          .path = source->path,
          .edition = source->document.edition,
          .schemaName = source->document.schemaName,
          .schemaVersion = source->document.schemaVersion,
          .ownerCapsule = source->document.ownerCapsule,
      });
    }
  }

  void BuildCatalog(const Project &project) {
    const ParsedSource *catalog_source = nullptr;
    const CatalogDeclaration *catalog_declaration = nullptr;
    bool has_catalog_identities = false;
    std::set<std::string> operation_model_names;
    for (const auto &source : project.sources) {
      if (source.document.catalog.has_value()) {
        if (catalog_declaration != nullptr)
          AddDuplicateDiagnostic(
              "idl.semantic_duplicate_catalog",
              "A semantic project may declare exactly one catalog.",
              {source.path, source.document.catalog->span},
              {catalog_source->path, catalog_declaration->span});
        else {
          catalog_source = &source;
          catalog_declaration = &*source.document.catalog;
        }
      }
      for (const auto &model : source.document.models) {
        has_catalog_identities =
            has_catalog_identities || model.catalogIdentity.has_value();
        for (const auto &field : model.fields)
          has_catalog_identities =
              has_catalog_identities || field.catalogIdentity.has_value();
      }
      for (const auto &command : source.document.commands) {
        operation_model_names.insert(command.requestModelName);
        operation_model_names.insert(command.responseModelName);
        operation_model_names.insert(command.errorModelNames.begin(),
                                     command.errorModelNames.end());
      }
      for (const auto &event : source.document.events)
        operation_model_names.insert(event.payloadModelName);
    }
    if (catalog_declaration == nullptr) {
      if (has_catalog_identities)
        AddDiagnostic("idl.semantic_catalog_declaration_missing",
                      "Catalog-backed model or field identities require one "
                      "catalog declaration in the entry source.",
                      project.entryPath, {});
      return;
    }
    if (catalog_source->path != project.entryPath)
      AddDiagnostic("idl.semantic_catalog_not_entry_owned",
                    "The catalog declaration must be owned by the semantic "
                    "project entry source.",
                    catalog_source->path, catalog_declaration->span);

    SemanticCatalog catalog{
        .catalogId = catalog_declaration->catalogId,
        .version = catalog_declaration->version,
        .provenance = {catalog_source->path, catalog_declaration->span},
    };
    std::map<std::string, const CatalogFragmentDeclaration *> fragments;
    for (const auto &declaration : catalog_declaration->fragments) {
      fragments.emplace(declaration.fragmentId, &declaration);
      catalog.fragments.push_back({
          .fragmentId = declaration.fragmentId,
          .ownerCapsule = declaration.ownerCapsule,
          .version = declaration.version,
          .declarationOrder = declaration.declarationOrder,
          .provenance = {catalog_source->path, declaration.span},
      });
    }
    std::sort(catalog.fragments.begin(), catalog.fragments.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declarationOrder != rhs.declarationOrder)
                  return lhs.declarationOrder < rhs.declarationOrder;
                return lhs.fragmentId < rhs.fragmentId;
              });

    std::map<std::string, SemanticProvenance> model_registry_keys;
    std::map<std::string, SemanticProvenance> model_ids;
    std::map<std::string, SemanticProvenance> qualified_names;
    std::map<std::string, std::map<std::uint32_t, SemanticProvenance>>
        model_orders;
    std::map<std::string, SemanticProvenance> field_registry_keys;
    for (const auto &source : project.sources) {
      for (const auto &model : source.document.models) {
        if (model.external) {
          if (model.catalogIdentity.has_value() ||
              std::any_of(model.fields.begin(), model.fields.end(),
                          [](const auto &field) {
                            return field.catalogIdentity.has_value();
                          }))
            AddDiagnostic("idl.external_model_catalog_identity_forbidden",
                          "External model declarations cannot claim catalog "
                          "model or field identity.",
                          source.path, model.span);
          continue;
        }
        const SemanticProvenance model_provenance{source.path, model.span};
        if (!model.catalogIdentity.has_value()) {
          if (operation_model_names.contains(model.name))
            continue;
          AddDiagnostic("idl.semantic_model_catalog_identity_missing",
                        "Every model in a catalog-backed project requires "
                        "catalog_identity.",
                        source.path, model.span);
          continue;
        }
        const auto &identity = *model.catalogIdentity;
        const SemanticProvenance identity_provenance{source.path,
                                                     identity.span};
        const auto fragment = fragments.find(identity.fragmentId);
        if (fragment == fragments.end())
          AddDiagnostic("idl.semantic_catalog_fragment_unresolved",
                        "Model catalog_identity references an undeclared "
                        "catalog fragment.",
                        source.path, identity.span);
        else if (fragment->second->ownerCapsule != source.document.ownerCapsule)
          AddDiagnostic("idl.semantic_catalog_fragment_owner_mismatch",
                        "Model source owner must match its catalog fragment "
                        "owner capsule.",
                        source.path, identity.span);
        for (auto [values, value, code, message] : {
                 std::tuple{
                     &model_registry_keys, &identity.registryKey,
                     "idl.semantic_duplicate_catalog_model_registry_key",
                     "Catalog model registry keys must be globally unique."},
                 std::tuple{&model_ids, &identity.modelId,
                            "idl.semantic_duplicate_catalog_model_id",
                            "Catalog model IDs must be globally unique."},
                 std::tuple{
                     &qualified_names, &identity.qualifiedName,
                     "idl.semantic_duplicate_catalog_model_qualified_name",
                     "Catalog model qualified names must be globally unique."},
             }) {
          if (const auto [existing, inserted] =
                  values->emplace(*value, identity_provenance);
              !inserted)
            AddDuplicateDiagnostic(code, message, identity_provenance,
                                   existing->second);
        }
        auto &orders = model_orders[identity.fragmentId];
        if (const auto [existing, inserted] =
                orders.emplace(identity.declarationOrder, identity_provenance);
            !inserted)
          AddDuplicateDiagnostic("idl.semantic_duplicate_catalog_model_order",
                                 "Catalog model declaration orders must be "
                                 "unique within each fragment.",
                                 identity_provenance, existing->second);

        std::map<std::uint32_t, SemanticProvenance> field_orders;
        std::map<std::string, SemanticProvenance> field_names;
        for (const auto &field : model.fields) {
          if (!field.catalogIdentity.has_value()) {
            AddDiagnostic("idl.semantic_field_catalog_identity_missing",
                          "Every field in a catalog-backed project requires "
                          "catalog_identity.",
                          source.path, field.span);
            continue;
          }
          const auto &field_identity = *field.catalogIdentity;
          const SemanticProvenance field_provenance{source.path,
                                                    field_identity.span};
          if (const auto [existing, inserted] = field_registry_keys.emplace(
                  field_identity.registryKey, field_provenance);
              !inserted)
            AddDuplicateDiagnostic(
                "idl.semantic_duplicate_catalog_field_registry_key",
                "Catalog field registry keys must be globally unique.",
                field_provenance, existing->second);
          if (const auto [existing, inserted] = field_names.emplace(
                  field_identity.fieldName, field_provenance);
              !inserted)
            AddDuplicateDiagnostic(
                "idl.semantic_duplicate_catalog_field_name",
                "Catalog field names must be unique within each model.",
                field_provenance, existing->second);
          if (const auto [existing, inserted] = field_orders.emplace(
                  field_identity.declarationOrder, field_provenance);
              !inserted)
            AddDuplicateDiagnostic("idl.semantic_duplicate_catalog_field_order",
                                   "Catalog field declaration orders must be "
                                   "unique within each model.",
                                   field_provenance, existing->second);
        }
        std::uint32_t expected_field_order = 0;
        for (const auto &[order, provenance] : field_orders) {
          if (order != expected_field_order) {
            AddDiagnostic("idl.semantic_catalog_field_order_not_contiguous",
                          "Catalog field declaration orders must be contiguous "
                          "from zero within each model.",
                          provenance.sourcePath, provenance.span);
            break;
          }
          ++expected_field_order;
        }
      }
    }
    for (const auto &entry : model_orders) {
      const auto &orders = entry.second;
      std::uint32_t expected_model_order = 0;
      for (const auto &[order, provenance] : orders) {
        if (order != expected_model_order) {
          AddDiagnostic("idl.semantic_catalog_model_order_not_contiguous",
                        "Catalog model declaration orders must be contiguous "
                        "from zero within each fragment.",
                        provenance.sourcePath, provenance.span);
          break;
        }
        ++expected_model_order;
      }
    }
    ir_.catalog = std::move(catalog);
  }

  void BuildWireTypes(const Project &project) {
    std::vector<WireTypeCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.wireTypes) {
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
        });
      }
    }
    if (candidates.size() > limits_.maximumWireTypes ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic(
          "idl.semantic_wire_type_limit_exceeded",
          "Project wire type count exceeds the configured semantic limit.",
          "<semantic>", {});
      return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declaration->wireTypeId != rhs.declaration->wireTypeId)
                  return lhs.declaration->wireTypeId <
                         rhs.declaration->wireTypeId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });
    if (candidates.size() > limits_.maximumEnums ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.semantic_enum_limit_exceeded",
                    "Project enum count exceeds the configured semantic limit.",
                    "<semantic>", {});
      return;
    }

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    std::map<WirePrimitiveKind, SemanticProvenance> kinds;
    for (const auto &candidate : candidates) {
      const SemanticProvenance provenance{candidate.source->path,
                                          candidate.declaration->span};
      const auto kind = ParseWirePrimitiveKind(candidate.declaration->name);
      const auto byte_order =
          ParseWireByteOrder(candidate.declaration->byteOrder);
      if (!kind.has_value()) {
        AddDiagnostic("idl.wire_type_kind_invalid",
                      "Wire type name is not a supported primitive.",
                      candidate.source->path, candidate.declaration->span);
        continue;
      }
      if (!byte_order.has_value()) {
        AddDiagnostic("idl.wire_byte_order_invalid",
                      "Wire byte order must be none or big_endian.",
                      candidate.source->path, candidate.declaration->span);
        continue;
      }
      const auto expected = ExpectedWireShape(*kind);
      if (candidate.declaration->bitWidth != expected.bitWidth ||
          candidate.declaration->fixedSize != expected.fixedSize ||
          *byte_order != expected.byteOrder ||
          candidate.declaration->wireTypeId != ExpectedWireTypeId(*kind)) {
        AddDiagnostic(
            "idl.wire_type_shape_invalid",
            "Wire primitive ID, width, fixed size, or byte order does "
            "not match its canonical byte contract.",
            candidate.source->path, candidate.declaration->span);
      }
      if (const auto [existing, inserted] =
              ids.emplace(candidate.declaration->wireTypeId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_wire_type_id",
                               "Wire type IDs must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.declaration->name, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_wire_type_name",
                               "Wire primitive names must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] = kinds.emplace(*kind, provenance);
          !inserted)
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_wire_type_kind",
            "Each wire primitive kind may be declared only once.", provenance,
            existing->second);

      const WireTypeHandle handle{
          static_cast<std::uint32_t>(ir_.wireTypes.size())};
      ir_.wireTypes.push_back({
          .handle = handle,
          .source = sourceHandles_.at(candidate.source->path),
          .wireTypeId = WireTypeId{candidate.declaration->wireTypeId},
          .name = candidate.declaration->name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .kind = *kind,
          .bitWidth = candidate.declaration->bitWidth,
          .fixedSize = candidate.declaration->fixedSize,
          .byteOrder = *byte_order,
          .provenance = provenance,
      });
      wireByName_.emplace(candidate.declaration->name, handle);
      wireByKind_.emplace(*kind, handle);
      ir_.sources.at(sourceHandles_.at(candidate.source->path).value)
          .wireTypes.push_back(handle);
    }
    if (UsesSchemaV2(project)) {
      const SourceHandle source =
          ir_.entrySource.valid() ? ir_.entrySource : SourceHandle{0};
      const std::string source_path = ir_.sources.at(source.value).path;
      for (const auto kind : {
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
           }) {
        if (wireByKind_.contains(kind))
          continue;
        const auto shape = ExpectedWireShape(kind);
        const std::string name(WirePrimitiveKindName(kind));
        const WireTypeHandle handle{
            static_cast<std::uint32_t>(ir_.wireTypes.size())};
        ir_.wireTypes.push_back({
            .handle = handle,
            .source = source,
            .wireTypeId = WireTypeId{ExpectedWireTypeId(kind)},
            .name = name,
            .qualifiedName = "arken_schema.v2." + name,
            .ownerCapsule = "arken_schema_idl_v1",
            .kind = kind,
            .bitWidth = shape.bitWidth,
            .fixedSize = shape.fixedSize,
            .byteOrder = shape.byteOrder,
            .provenance = {source_path, {}},
        });
        wireByName_.emplace(name, handle);
        wireByKind_.emplace(kind, handle);
        ir_.sources.at(source.value).wireTypes.push_back(handle);
      }
    }
  }

  void BuildStorageCodecs(const Project &project) {
    std::vector<StorageCodecCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.storageCodecs) {
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
        });
      }
    }
    if (candidates.size() > limits_.maximumStorageCodecs ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic(
          "idl.semantic_storage_codec_limit_exceeded",
          "Project storage codec count exceeds the configured semantic limit.",
          "<semantic>", {});
      return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declaration->storageCodecId !=
                    rhs.declaration->storageCodecId)
                  return lhs.declaration->storageCodecId <
                         rhs.declaration->storageCodecId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    std::map<StorageCodecKind, SemanticProvenance> kinds;
    for (const auto &candidate : candidates) {
      const SemanticProvenance provenance{candidate.source->path,
                                          candidate.declaration->span};
      const auto kind = ParseStorageCodecKind(candidate.declaration->name);
      const auto storage_class =
          ParseStorageClass(candidate.declaration->storageClass);
      if (!kind.has_value()) {
        AddDiagnostic("idl.storage_codec_kind_invalid",
                      "Storage codec name is not a supported SQLite codec.",
                      candidate.source->path, candidate.declaration->span);
        continue;
      }
      if (!storage_class.has_value()) {
        AddDiagnostic(
            "idl.storage_class_invalid",
            "SQLite storage class must be integer, real, text, or blob.",
            candidate.source->path, candidate.declaration->span);
        continue;
      }
      const auto expected = ExpectedStorageShape(*kind);
      if (*storage_class != expected.first ||
          candidate.declaration->fixedSize != expected.second ||
          candidate.declaration->storageCodecId !=
              ExpectedStorageCodecId(*kind)) {
        AddDiagnostic(
            "idl.storage_codec_shape_invalid",
            "Storage codec ID, class, or fixed size does not match the "
            "canonical SQLite codec contract.",
            candidate.source->path, candidate.declaration->span);
      }
      if (!candidate.declaration->exact) {
        AddDiagnostic("idl.storage_codec_lossy",
                      "Edition 1 storage codecs must preserve every admitted "
                      "value exactly.",
                      candidate.source->path, candidate.declaration->span);
      }
      if ((*kind == StorageCodecKind::SqliteInteger ||
           *kind == StorageCodecKind::SqliteOrderedU64 ||
           *kind == StorageCodecKind::SqliteOrderedDecimal128) &&
          !candidate.declaration->orderPreserving) {
        AddDiagnostic(
            "idl.storage_codec_order_invalid",
            "Ordered numeric SQLite codecs must preserve comparison order.",
            candidate.source->path, candidate.declaration->span);
      }
      if (const auto [existing, inserted] =
              ids.emplace(candidate.declaration->storageCodecId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_storage_codec_id",
                               "Storage codec IDs must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.declaration->name, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_storage_codec_name",
                               "Storage codec names must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] = kinds.emplace(*kind, provenance);
          !inserted)
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_storage_codec_kind",
            "Each SQLite storage codec kind may be declared only once.",
            provenance, existing->second);

      const StorageCodecHandle handle{
          static_cast<std::uint32_t>(ir_.storageCodecs.size())};
      ir_.storageCodecs.push_back({
          .handle = handle,
          .source = sourceHandles_.at(candidate.source->path),
          .storageCodecId =
              StorageCodecId{candidate.declaration->storageCodecId},
          .name = candidate.declaration->name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .kind = *kind,
          .storageClass = *storage_class,
          .fixedSize = candidate.declaration->fixedSize,
          .exact = candidate.declaration->exact,
          .orderPreserving = candidate.declaration->orderPreserving,
          .provenance = provenance,
      });
      storageByName_.emplace(candidate.declaration->name, handle);
      storageByKind_.emplace(*kind, handle);
      ir_.sources.at(sourceHandles_.at(candidate.source->path).value)
          .storageCodecs.push_back(handle);
    }
    if (UsesSchemaV2(project)) {
      const SourceHandle source =
          ir_.entrySource.valid() ? ir_.entrySource : SourceHandle{0};
      const std::string source_path = ir_.sources.at(source.value).path;
      for (const auto kind : {
               StorageCodecKind::SqliteInteger,
               StorageCodecKind::SqliteReal,
               StorageCodecKind::SqliteText,
               StorageCodecKind::SqliteBlob,
               StorageCodecKind::SqliteFixedBlob,
               StorageCodecKind::SqliteOrderedU64,
               StorageCodecKind::SqliteOrderedDecimal128,
           }) {
        if (storageByKind_.contains(kind))
          continue;
        const auto shape = ExpectedStorageShape(kind);
        const std::string name(StorageCodecKindName(kind));
        const bool ordered = kind == StorageCodecKind::SqliteInteger ||
                             kind == StorageCodecKind::SqliteText ||
                             kind == StorageCodecKind::SqliteOrderedU64 ||
                             kind == StorageCodecKind::SqliteOrderedDecimal128;
        const StorageCodecHandle handle{
            static_cast<std::uint32_t>(ir_.storageCodecs.size())};
        ir_.storageCodecs.push_back({
            .handle = handle,
            .source = source,
            .storageCodecId = StorageCodecId{ExpectedStorageCodecId(kind)},
            .name = name,
            .qualifiedName = "arken_schema.v2." + name,
            .ownerCapsule = "arken_schema_idl_v1",
            .kind = kind,
            .storageClass = shape.first,
            .fixedSize = shape.second,
            .exact = true,
            .orderPreserving = ordered,
            .provenance = {source_path, {}},
        });
        storageByName_.emplace(name, handle);
        storageByKind_.emplace(kind, handle);
        ir_.sources.at(source.value).storageCodecs.push_back(handle);
      }
    }
  }

  void BuildModels(const Project &project) {
    for (const auto &source : project.sources) {
      for (const auto &model : source.document.models) {
        modelCandidates_.push_back({
            .source = &source,
            .model = &model,
            .qualifiedName = QualifiedName(source.document, model),
        });
      }
    }
    std::sort(modelCandidates_.begin(), modelCandidates_.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.model->typeId != rhs.model->typeId)
                  return lhs.model->typeId < rhs.model->typeId;
                if (lhs.qualifiedName != rhs.qualifiedName)
                  return lhs.qualifiedName < rhs.qualifiedName;
                return lhs.source->path < rhs.source->path;
              });
    if (modelCandidates_.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.semantic_model_limit_exceeded",
                    "Semantic model count exceeds the supported handle range.",
                    "<semantic>", {});
      return;
    }

    std::map<std::uint32_t, SemanticProvenance> model_ids;
    std::map<std::string, SemanticProvenance> model_names;
    std::map<std::string, SemanticProvenance> storage_names;
    std::map<std::string, SemanticProvenance> resolution_names;
    std::map<std::string, SemanticProvenance> source_aliases;
    for (const auto &candidate : modelCandidates_) {
      const SemanticProvenance provenance{candidate.source->path,
                                          candidate.model->span};
      resolution_names.emplace(candidate.qualifiedName, provenance);
    }
    for (std::size_t index = 0; index < modelCandidates_.size(); ++index) {
      const auto &candidate = modelCandidates_[index];
      const SemanticProvenance provenance{
          .sourcePath = candidate.source->path,
          .span = candidate.model->span,
      };
      if (const auto [existing, inserted] =
              model_ids.emplace(candidate.model->typeId, provenance);
          !inserted) {
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_model_id",
            "Model type IDs must be globally unique in the semantic project.",
            provenance, existing->second);
      }
      if (const auto [existing, inserted] =
              model_names.emplace(candidate.qualifiedName, provenance);
          !inserted) {
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_model_name",
            "Qualified model names must be unique in the semantic project.",
            provenance, existing->second);
      }
      if (const auto [existing, inserted] =
              storage_names.emplace(candidate.model->storageName, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_model_storage_name",
                               "Model storage names must be globally unique in "
                               "the semantic project.",
                               provenance, existing->second);
      for (const auto &alias : candidate.model->sourceAliases) {
        if (const auto [existing, inserted] =
                source_aliases.emplace(alias, provenance);
            !inserted)
          AddDuplicateDiagnostic("idl.semantic_duplicate_source_alias",
                                 "Model source aliases must resolve to exactly "
                                 "one model across imports.",
                                 provenance, existing->second);
      }
      for (const auto &retired_name : candidate.model->retiredNames) {
        const auto qualified =
            QualifiedDeclarationName(candidate.source->document, retired_name);
        if (const auto [existing, inserted] =
                resolution_names.emplace(qualified, provenance);
            !inserted)
          AddDuplicateDiagnostic("idl.semantic_model_resolution_name_collision",
                                 "Active and retired model names must resolve "
                                 "to exactly one model across imports.",
                                 provenance, existing->second);
      }

      const ModelHandle handle{static_cast<std::uint32_t>(index)};
      modelHandles_.emplace(candidate.model, handle);
      modelBySourceAndName_.emplace(
          std::pair{sourceHandles_.at(candidate.source->path),
                    candidate.model->name},
          handle);
      ir_.models.push_back(BuildModel(handle, candidate));
    }

    for (auto &source : ir_.sources) {
      const auto *parsed_source = sourceByHandle_.at(source.handle.value);
      for (const auto &model : parsed_source->document.models) {
        if (const auto found = modelHandles_.find(&model);
            found != modelHandles_.end())
          source.models.push_back(found->second);
      }
      std::sort(source.models.begin(), source.models.end());
    }
  }

  SemanticModel BuildModel(ModelHandle handle,
                           const ModelCandidate &candidate) {
    const auto kind = ParseModelKind(candidate.model->kind);
    const auto retention = ParseRetentionPolicy(candidate.model->retention);
    const auto role = ParseModelRole(candidate.model->role);
    const auto name_review = ParseNameReviewStatus(candidate.model->nameReview);
    if (!kind.has_value())
      AddDiagnostic("idl.model_kind_invalid", "Model kind is not supported.",
                    candidate.source->path, candidate.model->span);
    if (!retention.has_value())
      AddDiagnostic("idl.model_retention_invalid",
                    "Model retention policy is not supported.",
                    candidate.source->path, candidate.model->span);
    if (!role.has_value())
      AddDiagnostic("idl.model_role_invalid", "Model role is not supported.",
                    candidate.source->path, candidate.model->span);
    if (!name_review.has_value())
      AddDiagnostic("idl.model_name_review_invalid",
                    "Model name review status is not supported.",
                    candidate.source->path, candidate.model->span);
    SemanticModel model{
        .handle = handle,
        .source = sourceHandles_.at(candidate.source->path),
        .typeId = ModelTypeId{candidate.model->typeId},
        .external = candidate.model->external,
        .schemaName = candidate.source->document.schemaName,
        .schemaVersion = candidate.source->document.schemaVersion,
        .name = candidate.model->name,
        .storageName = candidate.model->storageName,
        .qualifiedName = candidate.qualifiedName,
        .ownerCapsule = candidate.source->document.ownerCapsule,
        .domain = candidate.model->domain,
        .description = candidate.model->description,
        .kind = kind.value_or(ModelKind::Entity),
        .retention = retention.value_or(RetentionPolicy::Durable),
        .mutationAuthority = candidate.model->mutationAuthority,
        .role = role.value_or(ModelRole::Unspecified),
        .sourceAliases = candidate.model->sourceAliases,
        .retiredNames = candidate.model->retiredNames,
        .catalogRetiredNames = candidate.model->catalogRetiredNames,
        .nameReview = name_review.value_or(NameReviewStatus::Canonical),
        .nameReviewRationale = candidate.model->nameReviewRationale,
        .provenance =
            {
                .sourcePath = candidate.source->path,
                .span = candidate.model->span,
            },
    };
    if (candidate.model->external) {
      if (std::any_of(candidate.model->fields.begin(),
                      candidate.model->fields.end(),
                      [](const auto &field) { return field.primary; }))
        AddDiagnostic("idl.external_model_primary_id_forbidden",
                      "External models must declare exact imported identity "
                      "fields and cannot use primary_id shorthand.",
                      candidate.source->path, candidate.model->span);
      if (!candidate.model->relations.empty() ||
          !candidate.model->joins.empty() || !candidate.model->oneOfs.empty() ||
          !candidate.model->temporalContracts.empty())
        AddDiagnostic("idl.external_model_behavior_forbidden",
                      "External models may declare fields and keys/indexes for "
                      "reference validation but cannot own relations, joins, "
                      "one_ofs, or temporal contracts.",
                      candidate.source->path, candidate.model->span);
    }
    if (candidate.model->catalogIdentity.has_value()) {
      const auto &identity = *candidate.model->catalogIdentity;
      model.catalogIdentity = SemanticCatalogModelIdentity{
          .fragmentId = identity.fragmentId,
          .registryKey = identity.registryKey,
          .modelId = identity.modelId,
          .logicalName = identity.logicalName,
          .qualifiedName = identity.qualifiedName,
          .domain = identity.domain,
          .declarationOrder = identity.declarationOrder,
      };
    }
    return model;
  }

  std::optional<EnumHandle> ResolveEnumReference(const ParsedSource &source,
                                                 std::string_view name,
                                                 SourceSpan span) {
    const SourceHandle source_handle = sourceHandles_.at(source.path);
    std::vector<EnumHandle> matches;
    if (const auto local =
            enumBySourceAndName_.find({source_handle, std::string(name)});
        local != enumBySourceAndName_.end()) {
      matches.push_back(local->second);
    } else {
      for (const auto &import : ir_.sources.at(source_handle.value).imports) {
        const auto imported =
            enumBySourceAndName_.find({import.source, std::string(name)});
        if (imported != enumBySourceAndName_.end())
          matches.push_back(imported->second);
      }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    if (matches.empty()) {
      AddDiagnostic("idl.semantic_enum_unresolved",
                    "Semantic enum reference does not resolve in its source or "
                    "direct imports.",
                    source.path, span);
      return std::nullopt;
    }
    if (matches.size() != 1) {
      AddDiagnostic(
          "idl.semantic_enum_ambiguous",
          "Semantic enum reference is ambiguous across direct imports.",
          source.path, span);
      return std::nullopt;
    }
    return matches.front();
  }

  void BuildSemanticTypes(const Project &project) {
    std::vector<SemanticTypeCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.semanticTypes) {
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .name = declaration.name,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
            .semanticTypeId = declaration.semanticTypeId,
        });
      }
    }
    for (std::size_t index = 0; index < modelCandidates_.size(); ++index) {
      const auto &candidate = modelCandidates_[index];
      const auto &model = ir_.models[index];
      if (std::none_of(candidate.model->fields.begin(),
                       candidate.model->fields.end(),
                       [](const auto &field) { return field.primary; }))
        continue;
      candidates.push_back({
          .source = candidate.source,
          .definingModel = &model,
          .name = model.name + "_id",
          .qualifiedName = model.qualifiedName + "_id",
          .semanticTypeId = model.typeId.value,
      });
    }
    if (candidates.size() > limits_.maximumSemanticTypes ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.semantic_type_limit_exceeded",
                    "Project semantic type count, including model identity "
                    "types, exceeds the configured limit.",
                    "<semantic>", {});
      return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.semanticTypeId != rhs.semanticTypeId)
                  return lhs.semanticTypeId < rhs.semanticTypeId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });

    const auto fixed_wire = wireByKind_.find(WirePrimitiveKind::FixedBytes);
    const auto fixed_storage =
        storageByKind_.find(StorageCodecKind::SqliteFixedBlob);
    const bool generated_ids_required = std::any_of(
        candidates.begin(), candidates.end(), [](const auto &candidate) {
          return candidate.definingModel != nullptr;
        });
    if (generated_ids_required && (fixed_wire == wireByKind_.end() ||
                                   fixed_storage == storageByKind_.end())) {
      AddDiagnostic("idl.primary_id_codec_missing",
                    "Models require declared fixed_bytes and sqlite_fixed_blob "
                    "contracts for their generated fixed IDs.",
                    "<semantic>", {});
    }

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    std::map<std::string, SemanticProvenance> registry_keys;
    for (const auto &candidate : candidates) {
      const SemanticProvenance provenance{
          candidate.source->path,
          candidate.declaration == nullptr
              ? candidate.definingModel->provenance.span
              : candidate.declaration->span,
      };
      if (const auto [existing, inserted] =
              ids.emplace(candidate.semanticTypeId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_semantic_type_id",
                               "Semantic type IDs must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.name, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_semantic_type_name",
                               "Semantic type names must be globally unique in "
                               "an edition 1 project.",
                               provenance, existing->second);

      SemanticTypeDefinition definition{
          .handle = SemanticTypeHandle{static_cast<std::uint32_t>(
              ir_.semanticTypes.size())},
          .source = sourceHandles_.at(candidate.source->path),
          .semanticTypeId = SemanticTypeId{candidate.semanticTypeId},
          .name = candidate.name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .provenance = provenance,
      };
      if (candidate.definingModel != nullptr) {
        if (fixed_wire == wireByKind_.end() ||
            fixed_storage == storageByKind_.end())
          continue;
        definition.kind = SemanticTypeKind::FixedId;
        definition.wireType = fixed_wire->second;
        definition.storageCodec = fixed_storage->second;
        definition.fixedSize = 16;
        definition.targets = {
            TargetProfile::Cpp,    TargetProfile::Sqlite,
            TargetProfile::CSharp, TargetProfile::JavaScript,
            TargetProfile::Fabric, TargetProfile::HttpJson,
        };
        definition.definingModel = candidate.definingModel->handle;
      } else {
        const auto &declaration = *candidate.declaration;
        definition.semanticName = declaration.semanticName;
        definition.registryKey = declaration.registryKey;
        if (definition.semanticName.empty() != definition.registryKey.empty())
          AddDiagnostic("idl.semantic_type_identity_incomplete",
                        "Semantic type identity must provide both "
                        "semantic_name and registry_key.",
                        candidate.source->path, declaration.span);
        if (!definition.registryKey.empty())
          if (const auto [existing, inserted] =
                  registry_keys.emplace(definition.registryKey, provenance);
              !inserted)
            AddDuplicateDiagnostic(
                "idl.semantic_duplicate_semantic_type_registry_key",
                "Semantic type registry keys must be globally unique.",
                provenance, existing->second);
        const auto kind = ParseSemanticTypeKind(declaration.kind);
        const auto normalization =
            ParseNormalization(declaration.normalization);
        const auto json_profile =
            detail::ParseJsonProfile(declaration.jsonProfile);
        const auto boundary_mapping =
            detail::ParseBoundaryMappingPolicy(declaration.boundaryMapping);
        const auto temporal_role = ParseTemporalRole(declaration.temporalRole);
        const auto representation =
            ParseValueRepresentation(declaration.representation);
        std::optional<WireTypeHandle> wire;
        std::optional<StorageCodecHandle> storage;
        if (!kind.has_value())
          AddDiagnostic("idl.semantic_type_kind_invalid",
                        "Semantic type kind is not supported.",
                        candidate.source->path, declaration.span);
        if (!normalization.has_value())
          AddDiagnostic("idl.semantic_normalization_invalid",
                        "Semantic normalization policy is not supported.",
                        candidate.source->path, declaration.span);
        if (!json_profile.has_value())
          AddDiagnostic("idl.semantic_json_profile_invalid",
                        "Semantic JSON profile is not supported.",
                        candidate.source->path, declaration.span);
        if (!boundary_mapping.has_value())
          AddDiagnostic("idl.semantic_boundary_mapping_invalid",
                        "Semantic boundary mapping policy is not supported.",
                        candidate.source->path, declaration.span);
        if (!temporal_role.has_value())
          AddDiagnostic("idl.semantic_temporal_role_invalid",
                        "Semantic temporal role is not supported.",
                        candidate.source->path, declaration.span);
        if (!representation.has_value())
          AddDiagnostic("idl.semantic_value_representation_invalid",
                        "Type representation must be automatic, native, "
                        "enum_name, enum_id, bytes, or hex_text.",
                        candidate.source->path, declaration.span);
        if (declaration.presence != "explicit")
          AddDiagnostic("idl.semantic_presence_invalid",
                        "Semantic types require explicit presence.",
                        candidate.source->path, declaration.span);
        if (declaration.defaultPolicy != "none")
          AddDiagnostic("idl.semantic_default_invalid",
                        "Semantic types cannot define an implicit default.",
                        candidate.source->path, declaration.span);
        if (declaration.narrowing != "reject")
          AddDiagnostic("idl.semantic_narrowing_invalid",
                        "Semantic types must reject narrowing.",
                        candidate.source->path, declaration.span);
        if (kind.has_value()) {
          if (!declaration.wireTypeName.empty()) {
            const auto found = wireByName_.find(declaration.wireTypeName);
            if (found != wireByName_.end())
              wire = found->second;
          } else if (candidate.source->document.edition == 2) {
            const auto default_kind =
                DefaultWireKind(*kind, declaration.fixedSize);
            if (default_kind.has_value()) {
              const auto found = wireByKind_.find(*default_kind);
              if (found != wireByKind_.end())
                wire = found->second;
            }
          }
        }
        if (!wire.has_value())
          AddDiagnostic("idl.semantic_wire_type_unresolved",
                        "Type wire mapping does not resolve and has no "
                        "canonical default.",
                        candidate.source->path, declaration.span);
        if (candidate.source->document.edition == 1) {
          const auto found = storageByName_.find(declaration.storageCodecName);
          if (found != storageByName_.end())
            storage = found->second;
        } else if (kind.has_value() && wire.has_value() &&
                   representation.has_value()) {
          const auto wire_kind = ir_.wireTypes.at(wire->value).kind;
          const auto storage_kind =
              DefaultStorageKind(*kind, wire_kind, *representation);
          if (storage_kind.has_value()) {
            const auto found = storageByKind_.find(*storage_kind);
            if (found != storageByKind_.end())
              storage = found->second;
          }
        }
        if (!storage.has_value())
          AddDiagnostic("idl.semantic_storage_codec_unresolved",
                        "The selected target profile cannot derive an exact "
                        "storage mapping.",
                        candidate.source->path, declaration.span);
        if (!kind.has_value() || !normalization.has_value() ||
            !json_profile.has_value() || !boundary_mapping.has_value() ||
            !temporal_role.has_value() || !representation.has_value() ||
            !wire.has_value() || !storage.has_value()) {
          continue;
        }
        definition.kind = *kind;
        definition.wireType = *wire;
        definition.storageCodec = *storage;
        const auto &wire_definition =
            ir_.wireTypes.at(definition.wireType.value);
        definition.fixedSize = declaration.fixedSize != 0u
                                   ? declaration.fixedSize
                                   : wire_definition.fixedSize;
        definition.precision = declaration.precision;
        definition.scale = declaration.scale;
        definition.normalization = *normalization;
        definition.jsonProfile = *json_profile;
        definition.boundaryMapping = *boundary_mapping;
        definition.temporalRole = *temporal_role;
        if (!detail::JsonContractValid(*kind, *json_profile, *boundary_mapping))
          AddDiagnostic("idl.semantic_json_contract_invalid",
                        "JsonText requires an explicit JSON profile and opaque "
                        "boundary mapping; "
                        "other semantic kinds prohibit both.",
                        candidate.source->path, declaration.span);
        if (*kind != SemanticTypeKind::Timestamp &&
            *temporal_role != TemporalRole::None)
          AddDiagnostic("idl.semantic_temporal_role_mismatch",
                        "Only timestamp semantic types may declare a default "
                        "temporal role.",
                        candidate.source->path, declaration.span);
        if (*kind != SemanticTypeKind::Enum && !declaration.enumName.empty())
          AddDiagnostic(
              "idl.semantic_enum_binding_unexpected",
              "Only enum semantic types may declare an enum property.",
              candidate.source->path, declaration.span);

        const auto &storage_definition =
            ir_.storageCodecs.at(definition.storageCodec.value);
        const auto resolved_representation = ResolveValueRepresentation(
            *kind, wire_definition.kind, *representation);
        if (!ValueRepresentationCompatible(*kind, wire_definition.kind,
                                           resolved_representation))
          AddDiagnostic("idl.semantic_value_representation_mismatch",
                        "Type representation is incompatible with its semantic "
                        "kind and wire primitive.",
                        candidate.source->path, declaration.span);
        if (!TypeBindingCompatible(*kind, wire_definition.kind,
                                   storage_definition.kind)) {
          AddDiagnostic("idl.semantic_type_binding_invalid",
                        "Semantic kind is incompatible with its wire primitive "
                        "or SQLite storage codec.",
                        candidate.source->path, declaration.span);
        }
        ValidateSemanticShape(definition, declaration);
        std::set<TargetProfile> targets;
        for (const auto &target_name : declaration.targets) {
          const auto target = ParseTargetProfile(target_name);
          if (!target.has_value()) {
            AddDiagnostic("idl.semantic_target_invalid",
                          "Semantic target profile is not supported.",
                          candidate.source->path, declaration.span);
          } else if (!targets.insert(*target).second) {
            AddDiagnostic("idl.semantic_target_duplicate",
                          "Semantic target profile must be unique.",
                          candidate.source->path, declaration.span);
          }
        }
        definition.targets.assign(targets.begin(), targets.end());
        if (definition.targets.empty())
          AddDiagnostic("idl.semantic_targets_empty",
                        "Semantic type requires at least one target profile.",
                        candidate.source->path, declaration.span);
      }

      const auto handle = definition.handle;
      ir_.semanticTypes.push_back(std::move(definition));
      semanticByName_.emplace(candidate.name, handle);
      const auto source_handle = sourceHandles_.at(candidate.source->path);
      semanticBySourceAndName_.emplace(std::pair{source_handle, candidate.name},
                                       handle);
      ir_.sources.at(source_handle.value).semanticTypes.push_back(handle);
    }
  }

  void ValidateSemanticShape(const SemanticTypeDefinition &definition,
                             const SemanticTypeDeclaration &declaration) {
    const auto &wire = ir_.wireTypes.at(definition.wireType.value);
    bool valid = true;
    switch (definition.kind) {
    case SemanticTypeKind::FixedId:
      valid = definition.fixedSize > 0 && definition.precision == 0 &&
              definition.scale == 0 &&
              definition.normalization == NormalizationKind::None;
      break;
    case SemanticTypeKind::Decimal:
      valid = definition.fixedSize == 16 && definition.precision >= 1 &&
              definition.precision <= 38 &&
              definition.scale <= definition.precision &&
              definition.normalization == NormalizationKind::None;
      break;
    case SemanticTypeKind::Text:
      valid = definition.fixedSize == 0 && definition.precision == 0 &&
              definition.scale == 0;
      break;
    case SemanticTypeKind::Enum:
      valid = definition.precision == 0 && definition.scale == 0 &&
              ((wire.kind == WirePrimitiveKind::Utf8 &&
                definition.fixedSize == 0) ||
               (wire.kind != WirePrimitiveKind::Utf8 &&
                definition.fixedSize == wire.fixedSize &&
                definition.normalization == NormalizationKind::None));
      break;
    case SemanticTypeKind::Blob:
      valid = definition.precision == 0 && definition.scale == 0 &&
              definition.normalization == NormalizationKind::None &&
              ((wire.kind == WirePrimitiveKind::Bytes &&
                definition.fixedSize == 0) ||
               (wire.kind == WirePrimitiveKind::FixedBytes &&
                definition.fixedSize > 0));
      break;
    case SemanticTypeKind::JsonText:
      valid = definition.fixedSize == 0 && definition.precision == 0 &&
              definition.scale == 0 &&
              definition.normalization == NormalizationKind::None;
      break;
    default:
      valid = definition.fixedSize == wire.fixedSize &&
              definition.precision == 0 && definition.scale == 0 &&
              definition.normalization == NormalizationKind::None;
      break;
    }
    if (!valid) {
      AddDiagnostic("idl.semantic_type_shape_invalid",
                    "Semantic size, precision, scale, or normalization is "
                    "invalid for its kind.",
                    definition.provenance.sourcePath, declaration.span);
    }
  }

  void BuildEnums(const Project &project) {
    std::vector<EnumCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.enums)
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
        });
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declaration->enumId != rhs.declaration->enumId)
                  return lhs.declaration->enumId < rhs.declaration->enumId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });
    if (candidates.size() > limits_.maximumEnums ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.semantic_enum_limit_exceeded",
                    "Project enum count exceeds the configured semantic limit.",
                    "<semantic>", {});
      return;
    }

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    for (const auto &candidate : candidates) {
      const auto &declaration = *candidate.declaration;
      const SemanticProvenance provenance{candidate.source->path,
                                          declaration.span};
      if (const auto [existing, inserted] =
              ids.emplace(declaration.enumId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_enum_id",
                               "Enum IDs must be globally unique.", provenance,
                               existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.qualifiedName, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_enum_name",
                               "Qualified enum names must be globally unique.",
                               provenance, existing->second);
      SemanticEnum definition{
          .handle = EnumHandle{static_cast<std::uint32_t>(ir_.enums.size())},
          .source = sourceHandles_.at(candidate.source->path),
          .enumId = EnumId{declaration.enumId},
          .name = declaration.name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .mutationAuthority = candidate.source->document.ownerCapsule,
          .provenance = provenance,
      };
      std::map<std::uint32_t, SemanticProvenance> value_ids;
      std::map<std::string, SemanticProvenance> value_names;
      for (const auto &value : declaration.values) {
        const SemanticProvenance value_provenance{candidate.source->path,
                                                  value.span};
        if (const auto [existing, inserted] =
                value_ids.emplace(value.valueId, value_provenance);
            !inserted)
          AddDuplicateDiagnostic(
              "idl.semantic_duplicate_enum_value_id",
              "Enum value IDs must be unique within their enum.",
              value_provenance, existing->second);
        if (const auto [existing, inserted] =
                value_names.emplace(value.name, value_provenance);
            !inserted)
          AddDuplicateDiagnostic(
              "idl.semantic_duplicate_enum_value_name",
              "Enum value names must be unique within their enum.",
              value_provenance, existing->second);
        definition.values.push_back({
            .valueId = EnumValueId{value.valueId},
            .name = value.name,
            .catalogIdentity = CatalogMemberIdentity(value.catalogIdentity),
            .provenance = value_provenance,
        });
      }
      const auto handle = definition.handle;
      enumBySourceAndName_.emplace(
          std::pair{definition.source, definition.name}, handle);
      ir_.sources.at(definition.source.value)
          .enums.push_back(definition.handle);
      ir_.enums.push_back(std::move(definition));
    }

    const auto bind = [this](EnumHandle enum_handle,
                             SemanticTypeHandle type_handle,
                             const SemanticProvenance &provenance) {
      auto &enumeration = ir_.enums.at(enum_handle.value);
      auto &type = ir_.semanticTypes.at(type_handle.value);
      if (type.kind != SemanticTypeKind::Enum ||
          type.definingModel.has_value()) {
        AddDiagnostic("idl.enum_semantic_type_invalid",
                      "Enum bindings require an enum semantic type that is not "
                      "model-defined.",
                      provenance.sourcePath, provenance.span);
        return;
      }
      if ((type.definingEnum.has_value() &&
           *type.definingEnum != enum_handle) ||
          (enumeration.semanticType.valid() &&
           enumeration.semanticType != type_handle)) {
        AddDiagnostic("idl.enum_semantic_binding_conflict",
                      "Enum and semantic type bindings must be one-to-one and "
                      "agree in both declarations.",
                      provenance.sourcePath, provenance.span);
        return;
      }
      type.definingEnum = enum_handle;
      enumeration.semanticType = type_handle;
      enumeration.semanticTypeId = type.semanticTypeId;
    };

    for (const auto &candidate : candidates) {
      const auto &declaration = *candidate.declaration;
      if (declaration.semanticTypeName.empty())
        continue;
      const auto source_handle = sourceHandles_.at(candidate.source->path);
      const auto type = semanticBySourceAndName_.find(
          {source_handle, declaration.semanticTypeName});
      if (type == semanticBySourceAndName_.end()) {
        AddDiagnostic(
            "idl.enum_semantic_type_unresolved",
            "Enum semantic type does not resolve in the enum's source.",
            candidate.source->path, declaration.span);
        continue;
      }
      const auto enumeration =
          enumBySourceAndName_.find({source_handle, declaration.name});
      if (enumeration != enumBySourceAndName_.end())
        bind(enumeration->second, type->second,
             {candidate.source->path, declaration.span});
    }

    for (const auto &source : project.sources) {
      const auto source_handle = sourceHandles_.at(source.path);
      for (const auto &declaration : source.document.semanticTypes) {
        if (declaration.kind != "enum" || declaration.enumName.empty())
          continue;
        const auto type =
            semanticBySourceAndName_.find({source_handle, declaration.name});
        const auto enumeration = ResolveEnumReference(
            source, declaration.enumName, declaration.span);
        if (type != semanticBySourceAndName_.end() && enumeration.has_value())
          bind(*enumeration, type->second, {source.path, declaration.span});
      }
    }

    for (const auto &type : ir_.semanticTypes) {
      if (type.kind == SemanticTypeKind::Enum && !type.definingEnum.has_value())
        AddDiagnostic(
            "idl.enum_definition_missing",
            "Every enum semantic type requires exactly one enum declaration.",
            type.provenance.sourcePath, type.provenance.span);
    }
    for (const auto &enumeration : ir_.enums) {
      if (!enumeration.semanticType.valid())
        continue;
      const auto &type = ir_.semanticTypes.at(enumeration.semanticType.value);
      const auto &wire = ir_.wireTypes.at(type.wireType.value);
      const std::uint64_t maximum =
          wire.kind == WirePrimitiveKind::U8
              ? std::numeric_limits<std::uint8_t>::max()
          : wire.kind == WirePrimitiveKind::U16
              ? std::numeric_limits<std::uint16_t>::max()
              : std::numeric_limits<std::uint32_t>::max();
      for (const auto &value : enumeration.values) {
        if (value.valueId.value > maximum)
          AddDiagnostic("idl.enum_value_wire_overflow",
                        "Enum value ID exceeds its semantic wire width.",
                        value.provenance.sourcePath, value.provenance.span);
      }
    }
  }

  void BuildFields() {
    for (std::size_t model_index = 0; model_index < modelCandidates_.size();
         ++model_index) {
      const auto &candidate = modelCandidates_[model_index];
      auto &model = ir_.models[model_index];
      const ModelHandle model_handle{static_cast<std::uint32_t>(model_index)};
      std::vector<const FieldDeclaration *> fields;
      fields.reserve(candidate.model->fields.size());
      for (const auto &field : candidate.model->fields)
        fields.push_back(&field);
      std::sort(fields.begin(), fields.end(),
                [](const auto *lhs, const auto *rhs) {
                  if (lhs->catalogIdentity.has_value() &&
                      rhs->catalogIdentity.has_value() &&
                      lhs->catalogIdentity->declarationOrder !=
                          rhs->catalogIdentity->declarationOrder)
                    return lhs->catalogIdentity->declarationOrder <
                           rhs->catalogIdentity->declarationOrder;
                  if (lhs->fieldId != rhs->fieldId)
                    return lhs->fieldId < rhs->fieldId;
                  return lhs->name < rhs->name;
                });

      std::map<std::uint32_t, SemanticProvenance> field_ids;
      std::map<std::string, SemanticProvenance> field_names;
      for (const auto *field : fields) {
        if (nextFieldHandle_ == std::numeric_limits<std::uint32_t>::max()) {
          AddDiagnostic(
              "idl.semantic_field_limit_exceeded",
              "Semantic field count exceeds the supported handle range.",
              candidate.source->path, field->span);
          break;
        }
        const SemanticProvenance provenance{
            .sourcePath = candidate.source->path,
            .span = field->span,
        };
        if (const auto [existing, inserted] =
                field_ids.emplace(field->fieldId, provenance);
            !inserted)
          AddDuplicateDiagnostic(
              "idl.semantic_duplicate_field_id",
              "Field IDs must be unique within a semantic model.", provenance,
              existing->second);
        if (const auto [existing, inserted] =
                field_names.emplace(field->name, provenance);
            !inserted)
          AddDuplicateDiagnostic(
              "idl.semantic_duplicate_field_name",
              "Field names must be unique within a semantic model.", provenance,
              existing->second);
        model.fields.push_back(BuildField(FieldHandle{nextFieldHandle_++},
                                          model_handle, candidate, *field));
      }
    }
  }

  SemanticField BuildField(FieldHandle field_handle, ModelHandle model_handle,
                           const ModelCandidate &candidate,
                           const FieldDeclaration &field) {
    const auto serialization = ParseSerializationPolicy(field.serialization);
    if (!serialization.has_value())
      AddDiagnostic("idl.field_serialization_invalid",
                    "Field serialization policy is not supported.",
                    candidate.source->path, field.span);
    SemanticField semantic{
        .handle = field_handle,
        .fieldId = FieldId{field.fieldId},
        .name = field.name,
        .cppName = field.cppName,
        .csharpName = field.csharpName,
        .storageName = field.storageName,
        .sourceField = field.sourceField,
        .importTransform = field.importTransform,
        .sourceKind = ParseFieldSourceKind(field.sourceProvenance)
                          .value_or(FieldSourceKind::Canonical),
        .serialization = serialization.value_or(SerializationPolicy::Include),
        .required = field.required && !field.optional &&
                    !field.maximumItems.has_value(),
        .redacted = field.redacted,
        .immutable = field.immutable,
        .writeOnce = field.writeOnce,
        .primaryKey = false,
        .generatedPrimaryId = field.primary,
        .provenance =
            {
                .sourcePath = candidate.source->path,
                .span = field.span,
            },
    };
    if (field.catalogIdentity.has_value())
      semantic.catalogIdentity = SemanticCatalogFieldIdentity{
          .registryKey = field.catalogIdentity->registryKey,
          .fieldName = field.catalogIdentity->fieldName,
          .declarationOrder = field.catalogIdentity->declarationOrder,
      };
    semantic.minimum = field.minimum;
    semantic.cardinality =
        field.maximumItems.has_value()
            ? FieldCardinality::Repeated
            : (field.optional ? FieldCardinality::OptionalOne
                              : FieldCardinality::RequiredOne);
    semantic.maximumItems = field.maximumItems;
    semantic.equalityComparable = true;
    const auto type = semanticByName_.find(field.typeName);
    if (type == semanticByName_.end()) {
      AddDiagnostic("idl.semantic_type_unresolved",
                    "Field type does not resolve to a governed semantic type.",
                    candidate.source->path, field.span);
      return semantic;
    }
    const auto &definition = ir_.semanticTypes.at(type->second.value);
    semantic.type = {
        .kind = definition.kind,
        .semanticType = definition.handle,
        .semanticTypeId = definition.semanticTypeId,
        .canonicalName = definition.qualifiedName,
        .definingModel = definition.definingModel,
        .definingEnum = definition.definingEnum,
    };
    if (!field.allowedEnumValues.empty()) {
      if (definition.kind != SemanticTypeKind::Enum ||
          !definition.definingEnum.has_value()) {
        AddDiagnostic("idl.field_allowed_enum_values_type_invalid",
                      "Allowed enum values may be declared only for a resolved "
                      "enum semantic type.",
                      candidate.source->path, field.span);
      } else {
        const auto &enumeration = ir_.enums.at(definition.definingEnum->value);
        std::set<std::uint32_t> seen_value_ids;
        for (const auto &allowed_name : field.allowedEnumValues) {
          const auto found =
              std::find_if(enumeration.values.begin(), enumeration.values.end(),
                           [&allowed_name](const auto &value) {
                             return value.name == allowed_name;
                           });
          if (found == enumeration.values.end()) {
            AddDiagnostic("idl.field_allowed_enum_value_unresolved",
                          "Allowed enum value does not resolve in the field's "
                          "governed enum.",
                          candidate.source->path, field.span);
          } else if (!seen_value_ids.insert(found->valueId.value).second) {
            AddDiagnostic(
                "idl.field_allowed_enum_value_duplicate",
                "Allowed enum values must not repeat a governed enum value.",
                candidate.source->path, field.span);
          } else {
            semantic.allowedEnumValueIds.push_back(found->valueId);
          }
        }
        std::sort(semantic.allowedEnumValueIds.begin(),
                  semantic.allowedEnumValueIds.end(),
                  [](const auto lhs, const auto rhs) {
                    return lhs.value < rhs.value;
                  });
      }
    }
    const auto &default_wire = ir_.wireTypes.at(definition.wireType.value);
    const auto &default_storage =
        ir_.storageCodecs.at(definition.storageCodec.value);
    semantic.representation = {
        .fieldKind = DefaultFieldKind(definition.kind),
        .wireType = definition.wireType,
        .wireTypeId = default_wire.wireTypeId,
        .wireSize = default_wire.kind == WirePrimitiveKind::FixedBytes
                        ? definition.fixedSize
                        : 0U,
        .storageCodec = definition.storageCodec,
        .storageCodecId = default_storage.storageCodecId,
        .storageSize =
            default_storage.fixedSize != 0U
                ? default_storage.fixedSize
                : (default_storage.kind == StorageCodecKind::SqliteFixedBlob
                       ? definition.fixedSize
                       : 0U),
        .precision = definition.precision,
        .scale = definition.scale,
        .normalization = definition.normalization,
    };
    if (field.representation.has_value()) {
      const auto &declaration = *field.representation;
      if (candidate.source->document.edition == 2) {
        const auto field_kind =
            declaration.fieldKind.empty()
                ? std::optional<FieldKind>(semantic.representation.fieldKind)
                : ParseFieldKind(declaration.fieldKind);
        const auto normalization =
            declaration.normalization.empty()
                ? std::optional<NormalizationKind>(
                      semantic.representation.normalization)
                : ParseNormalization(declaration.normalization);
        if (!field_kind.has_value())
          AddDiagnostic("idl.field_representation_kind_invalid",
                        "Field representation kind is not supported.",
                        candidate.source->path, declaration.span);
        if (!normalization.has_value())
          AddDiagnostic("idl.field_representation_normalization_invalid",
                        "Field representation normalization is not supported.",
                        candidate.source->path, declaration.span);
        if (field_kind.has_value() &&
            !FieldKindCompatible(*field_kind, definition.kind))
          AddDiagnostic("idl.field_representation_binding_invalid",
                        "Field representation kind is incompatible with its "
                        "semantic type.",
                        candidate.source->path, declaration.span);
        if (field_kind.has_value() && normalization.has_value()) {
          semantic.representation.fieldKind = *field_kind;
          semantic.representation.normalization = *normalization;
        }
      } else {
        const auto field_kind = ParseFieldKind(declaration.fieldKind);
        const auto normalization =
            ParseNormalization(declaration.normalization);
        const auto wire = wireByName_.find(declaration.wireTypeName);
        const auto storage = storageByName_.find(declaration.storageCodecName);
        if (!field_kind.has_value())
          AddDiagnostic("idl.field_representation_kind_invalid",
                        "Field representation kind is not supported.",
                        candidate.source->path, declaration.span);
        if (!normalization.has_value())
          AddDiagnostic("idl.field_representation_normalization_invalid",
                        "Field representation normalization is not supported.",
                        candidate.source->path, declaration.span);
        if (wire == wireByName_.end())
          AddDiagnostic("idl.field_representation_wire_unresolved",
                        "Field representation wire type does not resolve.",
                        candidate.source->path, declaration.span);
        if (storage == storageByName_.end())
          AddDiagnostic("idl.field_representation_storage_unresolved",
                        "Field representation storage codec does not resolve.",
                        candidate.source->path, declaration.span);
        if (field_kind.has_value() && normalization.has_value() &&
            wire != wireByName_.end() && storage != storageByName_.end()) {
          const auto &wire_definition = ir_.wireTypes.at(wire->second.value);
          const auto &storage_definition =
              ir_.storageCodecs.at(storage->second.value);
          if (wire->second != definition.wireType)
            AddDiagnostic("idl.field_representation_wire_mismatch",
                          "Field representation wire type must match its "
                          "governed semantic type.",
                          candidate.source->path, declaration.span);
          if (!FieldKindCompatible(*field_kind, definition.kind) ||
              !TypeBindingCompatible(definition.kind, wire_definition.kind,
                                     storage_definition.kind))
            AddDiagnostic("idl.field_representation_binding_invalid",
                          "Field kind, semantic kind, wire type, and storage "
                          "codec do not form a supported exact binding.",
                          candidate.source->path, declaration.span);
          const bool wire_size_valid =
              wire_definition.kind == WirePrimitiveKind::FixedBytes
                  ? declaration.wireSize > 0U &&
                        declaration.wireSize == definition.fixedSize
                  : declaration.wireSize == 0U;
          const std::uint32_t expected_storage_size =
              storage_definition.fixedSize != 0U
                  ? storage_definition.fixedSize
                  : (storage_definition.kind ==
                             StorageCodecKind::SqliteFixedBlob
                         ? declaration.wireSize
                         : 0U);
          const bool storage_size_valid =
              declaration.storageSize == expected_storage_size;
          const bool decimal_shape_valid =
              definition.kind == SemanticTypeKind::Decimal
                  ? declaration.precision == definition.precision &&
                        declaration.scale == definition.scale
                  : declaration.precision == 0U && declaration.scale == 0U;
          if (!wire_size_valid || !storage_size_valid || !decimal_shape_valid)
            AddDiagnostic("idl.field_representation_shape_invalid",
                          "Field representation size, precision, or scale does "
                          "not match its exact wire/storage binding.",
                          candidate.source->path, declaration.span);
          semantic.representation = {
              .fieldKind = *field_kind,
              .wireType = wire->second,
              .wireTypeId = wire_definition.wireTypeId,
              .wireSize = declaration.wireSize,
              .storageCodec = storage->second,
              .storageCodecId = storage_definition.storageCodecId,
              .storageSize = declaration.storageSize,
              .precision = declaration.precision,
              .scale = declaration.scale,
              .normalization = *normalization,
          };
        }
      }
    }
    const auto temporal_role =
        field.temporalRole == "inherit"
            ? std::optional<TemporalRole>(definition.temporalRole)
            : ParseTemporalRole(field.temporalRole);
    if (!temporal_role.has_value())
      AddDiagnostic(
          "idl.field_temporal_role_invalid",
          "Field temporal role must be inherit or a supported concrete role.",
          candidate.source->path, field.span);
    semantic.temporalRole = temporal_role.value_or(TemporalRole::None);
    if (definition.kind != SemanticTypeKind::Timestamp &&
        semantic.temporalRole != TemporalRole::None)
      AddDiagnostic("idl.field_temporal_role_mismatch",
                    "Only timestamp fields may carry a temporal role.",
                    candidate.source->path, field.span);
    if (field.primary && (definition.kind != SemanticTypeKind::FixedId ||
                          !definition.definingModel.has_value() ||
                          *definition.definingModel != model_handle)) {
      AddDiagnostic("idl.primary_id_semantic_type_invalid",
                    "Primary ID must resolve to the fixed-ID semantic type "
                    "generated for its model.",
                    candidate.source->path, field.span);
    }

    if (field.primary) {
      semantic.defaultValue = {
          .kind = FieldDefaultKind::Boundary,
          .boundarySource = BoundaryDefaultSource::GeneratedId,
      };
    } else {
      const auto default_kind = ParseFieldDefaultKind(field.defaultValue.kind);
      if (!default_kind.has_value())
        AddDiagnostic("idl.field_default_kind_invalid",
                      "Field default kind is not supported.",
                      candidate.source->path, field.defaultValue.span);
      else if (*default_kind == FieldDefaultKind::Literal) {
        bool valid = true;
        bool specific_diagnostic = false;
        std::string canonical = field.defaultValue.literal;
        switch (definition.kind) {
        case SemanticTypeKind::Bool:
          valid = canonical == "true" || canonical == "false";
          break;
        case SemanticTypeKind::U8:
        case SemanticTypeKind::U16:
        case SemanticTypeKind::U32:
        case SemanticTypeKind::U64:
        case SemanticTypeKind::I8:
        case SemanticTypeKind::I16:
        case SemanticTypeKind::I32:
        case SemanticTypeKind::I64:
        case SemanticTypeKind::Timestamp:
          valid = IntegerLiteralFits(definition.kind, canonical);
          break;
        case SemanticTypeKind::F32:
        case SemanticTypeKind::F64:
          valid = RealLiteralValid(canonical);
          break;
        case SemanticTypeKind::Decimal:
          valid = DecimalLiteralValid(canonical, definition.precision,
                                      definition.scale);
          break;
        case SemanticTypeKind::Enum:
          if (definition.definingEnum.has_value()) {
            const auto &values =
                ir_.enums.at(definition.definingEnum->value).values;
            const auto found = std::find_if(values.begin(), values.end(),
                                            [&canonical](const auto &value) {
                                              return value.name == canonical;
                                            });
            valid = found != values.end() &&
                    (semantic.allowedEnumValueIds.empty() ||
                     std::find(semantic.allowedEnumValueIds.begin(),
                               semantic.allowedEnumValueIds.end(),
                               found->valueId) !=
                         semantic.allowedEnumValueIds.end());
          } else
            valid = false;
          break;
        case SemanticTypeKind::JsonText: {
          if (definition.jsonProfile == JsonProfile::Rfc8785Canonical) {
            valid = false;
            specific_diagnostic = true;
            AddDiagnostic("idl.field_default_json_profile_unsupported",
                          "RFC 8785 canonical JSON defaults require a "
                          "target-complete JCS encoder.",
                          candidate.source->path, field.defaultValue.span);
          } else {
            valid = detail::JsonLiteralValid(canonical, definition.jsonProfile);
          }
          break;
        }
        case SemanticTypeKind::Text:
          break;
        case SemanticTypeKind::Blob:
        case SemanticTypeKind::FixedId:
          valid = false;
          break;
        }
        if (!valid && !specific_diagnostic)
          AddDiagnostic("idl.field_default_literal_invalid",
                        "Literal default is not canonical or compatible with "
                        "its semantic type.",
                        candidate.source->path, field.defaultValue.span);
        semantic.defaultValue = {
            .kind = FieldDefaultKind::Literal,
            .literal = std::move(canonical),
        };
      } else if (*default_kind == FieldDefaultKind::Boundary) {
        const auto source =
            ParseBoundaryDefaultSource(field.defaultValue.boundarySource);
        const bool compatible =
            source.has_value() &&
            ((*source == BoundaryDefaultSource::RequestTime &&
              definition.kind == SemanticTypeKind::Timestamp) ||
             (*source == BoundaryDefaultSource::GeneratedId &&
              definition.kind == SemanticTypeKind::FixedId) ||
             (*source == BoundaryDefaultSource::GeneratedToken &&
              (definition.kind == SemanticTypeKind::Text ||
               definition.kind == SemanticTypeKind::Blob)));
        if (!compatible)
          AddDiagnostic("idl.field_boundary_default_invalid",
                        "Boundary default source is not supported by the field "
                        "semantic type.",
                        candidate.source->path, field.defaultValue.span);
        semantic.defaultValue = {
            .kind = FieldDefaultKind::Boundary,
            .boundarySource = source.value_or(BoundaryDefaultSource::None),
            .catalogValue = field.defaultValue.catalogValue,
        };
      }
    }

    std::set<std::string> check_names;
    std::set<CheckKind> check_kinds;
    std::optional<std::uint64_t> minimum_unsigned;
    std::optional<std::uint64_t> maximum_unsigned;
    std::optional<std::int64_t> minimum_signed;
    std::optional<std::int64_t> maximum_signed;
    std::optional<double> minimum_real;
    std::optional<double> maximum_real;
    std::optional<std::uint32_t> minimum_length;
    std::optional<std::uint32_t> maximum_length;
    std::optional<std::uint32_t> fixed_length;
    for (const auto &declaration : field.checks) {
      const auto kind = ParseCheckKind(declaration.kind);
      const auto unit = ParseCheckUnit(declaration.unit);
      if (!check_names.insert(declaration.name).second)
        AddDiagnostic("idl.semantic_duplicate_field_check_name",
                      "Field check names must be unique after normalization.",
                      candidate.source->path, declaration.span);
      const auto &field_wire =
          ir_.wireTypes.at(semantic.representation.wireType.value);
      if (!kind.has_value() ||
          !ExplicitCheckCompatible(kind.value_or(CheckKind::ValidJson),
                                   definition.kind, field_wire.kind)) {
        AddDiagnostic("idl.field_check_kind_mismatch",
                      "Field check kind is unknown, generated-only, or "
                      "incompatible with its semantic type.",
                      candidate.source->path, declaration.span);
        continue;
      }
      const bool length_check = *kind == CheckKind::MinimumLength ||
                                *kind == CheckKind::MaximumLength ||
                                *kind == CheckKind::FixedLength;
      const bool unit_valid =
          unit.has_value() &&
          (length_check ? (semantic.representation.fieldKind == FieldKind::Blob
                               ? *unit == CheckUnit::Bytes
                               : *unit == CheckUnit::None ||
                                     *unit == CheckUnit::Utf8Bytes)
                        : *unit == CheckUnit::None);
      if (!unit_valid)
        AddDiagnostic(
            "idl.field_check_unit_invalid",
            "Text length checks require unit none or utf8_bytes; blob length "
            "checks require bytes; numeric checks require none.",
            candidate.source->path, declaration.span);
      if (!check_kinds.insert(*kind).second)
        AddDiagnostic(
            "idl.duplicate_field_check_kind",
            "A field may declare each explicit check kind at most once.",
            candidate.source->path, declaration.span);
      bool value_valid = true;
      if (*kind == CheckKind::MinimumInteger ||
          *kind == CheckKind::MaximumInteger)
        value_valid = IntegerLiteralFits(definition.kind, declaration.value);
      else if (*kind == CheckKind::MinimumReal ||
               *kind == CheckKind::MaximumReal)
        value_valid =
            definition.kind == SemanticTypeKind::Decimal
                ? DecimalLiteralValid(declaration.value, definition.precision,
                                      definition.scale)
                : RealLiteralValid(declaration.value);
      else
        value_valid = LengthValueValid(declaration.value);
      if (!value_valid)
        AddDiagnostic("idl.field_check_value_invalid",
                      "Field check value is not canonical or does not fit its "
                      "semantic type.",
                      candidate.source->path, declaration.span);
      else if (*kind == CheckKind::MinimumInteger ||
               *kind == CheckKind::MaximumInteger) {
        if (definition.kind == SemanticTypeKind::I8 ||
            definition.kind == SemanticTypeKind::I16 ||
            definition.kind == SemanticTypeKind::I32 ||
            definition.kind == SemanticTypeKind::I64 ||
            definition.kind == SemanticTypeKind::Timestamp) {
          std::int64_t parsed = 0;
          static_cast<void>(ParseExactInteger(declaration.value, parsed));
          (*kind == CheckKind::MinimumInteger ? minimum_signed
                                              : maximum_signed) = parsed;
        } else {
          std::uint64_t parsed = 0;
          static_cast<void>(ParseExactInteger(declaration.value, parsed));
          (*kind == CheckKind::MinimumInteger ? minimum_unsigned
                                              : maximum_unsigned) = parsed;
        }
      } else if (*kind == CheckKind::MinimumReal ||
                 *kind == CheckKind::MaximumReal) {
        const auto parsed = nlohmann::json::parse(declaration.value.begin(),
                                                  declaration.value.end(),
                                                  nullptr, false, false)
                                .get<double>();
        (*kind == CheckKind::MinimumReal ? minimum_real : maximum_real) =
            parsed;
      } else if (*kind == CheckKind::MinimumLength ||
                 *kind == CheckKind::MaximumLength ||
                 *kind == CheckKind::FixedLength) {
        std::uint32_t parsed = 0;
        static_cast<void>(ParseExactInteger(declaration.value, parsed));
        if (*kind == CheckKind::MinimumLength)
          minimum_length = parsed;
        else if (*kind == CheckKind::MaximumLength)
          maximum_length = parsed;
        else
          fixed_length = parsed;
      }
      semantic.checks.push_back({
          .name = declaration.name,
          .kind = *kind,
          .unit = unit.value_or(CheckUnit::None),
          .value = declaration.value,
          .message = declaration.message,
          .targets = definition.targets,
          .generated = false,
          .catalogIdentity = CatalogMemberIdentity(declaration.catalogIdentity),
      });
    }
    if (definition.kind == SemanticTypeKind::FixedId) {
      if (fixed_length && *fixed_length != definition.fixedSize)
        AddDiagnostic("idl.field_fixed_length_type_mismatch",
                      "Explicit fixed length must match the governed fixed-ID "
                      "semantic type.",
                      candidate.source->path, field.span);
      fixed_length = definition.fixedSize;
    }
    if ((minimum_unsigned && maximum_unsigned &&
         *minimum_unsigned > *maximum_unsigned) ||
        (minimum_signed && maximum_signed &&
         *minimum_signed > *maximum_signed) ||
        (minimum_real && maximum_real && *minimum_real > *maximum_real) ||
        (minimum_length && maximum_length &&
         *minimum_length > *maximum_length) ||
        (fixed_length && minimum_length && *fixed_length < *minimum_length) ||
        (fixed_length && maximum_length && *fixed_length > *maximum_length))
      AddDiagnostic("idl.field_check_range_invalid",
                    "Field minimum cannot exceed its matching maximum, and "
                    "fixed length must satisfy both bounds.",
                    candidate.source->path, field.span);

    const auto add_generated_check = [&](std::string name, CheckKind kind,
                                         CheckUnit unit, std::string value) {
      if (!check_names.insert(name).second)
        AddDiagnostic(
            "idl.generated_field_check_name_collision",
            "Declared field check collides with a generated semantic check.",
            candidate.source->path, field.span);
      semantic.checks.push_back({
          .name = std::move(name),
          .kind = kind,
          .unit = unit,
          .value = std::move(value),
          .targets = definition.targets,
          .generated = true,
      });
    };
    if (definition.kind == SemanticTypeKind::Enum &&
        definition.definingEnum.has_value())
      add_generated_check(
          "enum_domain", CheckKind::EnumDomain, CheckUnit::None,
          ir_.enums.at(definition.definingEnum->value).qualifiedName);
    if (definition.kind == SemanticTypeKind::JsonText)
      add_generated_check("valid_json", CheckKind::ValidJson, CheckUnit::None,
                          std::string(JsonProfileName(definition.jsonProfile)));
    if (definition.kind == SemanticTypeKind::Decimal) {
      add_generated_check("decimal_precision", CheckKind::DecimalPrecision,
                          CheckUnit::None,
                          std::to_string(definition.precision));
      add_generated_check("decimal_scale", CheckKind::DecimalScale,
                          CheckUnit::None, std::to_string(definition.scale));
    }
    if (definition.kind == SemanticTypeKind::FixedId)
      add_generated_check("fixed_length", CheckKind::FixedLength,
                          CheckUnit::Bytes,
                          std::to_string(definition.fixedSize));
    std::sort(semantic.checks.begin(), semantic.checks.end(),
              [](const auto &lhs, const auto &rhs) {
                return CatalogMemberLess(lhs, rhs);
              });

    std::set<QueryOperator> query_operators;
    for (const auto &operation_name : field.query.operators) {
      const auto operation = ParseQueryOperator(operation_name);
      if (!operation.has_value() ||
          !QueryOperatorCompatible(operation.value_or(QueryOperator::Equal),
                                   definition.kind, field.required)) {
        AddDiagnostic("idl.field_query_operator_invalid",
                      "Query operator is unknown or incompatible with the "
                      "field semantic contract.",
                      candidate.source->path, field.query.span);
        continue;
      }
      if (!query_operators.insert(*operation).second)
        AddDiagnostic("idl.semantic_duplicate_query_operator",
                      "Query operators must be unique after normalization.",
                      candidate.source->path, field.query.span);
    }
    semantic.query.operators.assign(query_operators.begin(),
                                    query_operators.end());
    semantic.query.sortable = field.query.sortable;
    semantic.query.cursor = field.query.cursor;
    const auto &storage =
        ir_.storageCodecs.at(semantic.representation.storageCodec.value);
    if ((semantic.query.sortable || semantic.query.cursor) &&
        !storage.orderPreserving)
      AddDiagnostic("idl.field_query_storage_order_invalid",
                    "Sortable and cursor fields require an order-preserving "
                    "storage codec.",
                    candidate.source->path, field.query.span);
    if (semantic.query.cursor && !semantic.query.sortable)
      AddDiagnostic("idl.field_query_cursor_without_sort",
                    "Cursor capability requires sortability.",
                    candidate.source->path, field.query.span);
    if (field.redacted &&
        (semantic.serialization != SerializationPolicy::Omit ||
         !semantic.query.operators.empty() || semantic.query.sortable ||
         semantic.query.cursor))
      AddDiagnostic("idl.redacted_field_policy_invalid",
                    "Redacted fields must be omitted from serialization and "
                    "prohibit all query capabilities.",
                    candidate.source->path, field.span);
    const bool policy_bearing =
        semantic.defaultValue.kind != FieldDefaultKind::None ||
        !semantic.checks.empty() || !semantic.query.operators.empty() ||
        semantic.query.sortable || semantic.query.cursor;
    if (policy_bearing && !HasAllValidationTargets(definition.targets))
      AddDiagnostic("idl.policy_target_profile_incomplete",
                    "Field policies require a complete canonical or Fabric "
                    "target profile family.",
                    candidate.source->path, field.span);
    return semantic;
  }

  static const SemanticField *FindField(const SemanticModel &model,
                                        std::string_view name) {
    const auto found =
        std::find_if(model.fields.begin(), model.fields.end(),
                     [name](const auto &field) { return field.name == name; });
    return found == model.fields.end() ? nullptr : &*found;
  }

  static SemanticFieldReference FieldReference(const SemanticField &field) {
    return {.handle = field.handle,
            .fieldId = field.fieldId,
            .canonicalName = field.name};
  }

  std::optional<std::vector<SemanticFieldReference>>
  ResolveFields(const SemanticModel &model,
                const std::vector<std::string> &names,
                const SemanticProvenance &provenance, bool allow_empty) {
    if ((!allow_empty && names.empty()) ||
        names.size() > limits_.maximumStructuralFields) {
      AddDiagnostic(
          "idl.semantic_structural_field_count_invalid",
          "Structural field list is empty or exceeds the configured limit.",
          provenance.sourcePath, provenance.span);
      return std::nullopt;
    }
    std::vector<SemanticFieldReference> fields;
    std::set<FieldHandle> seen;
    for (const auto &name : names) {
      const auto *field = FindField(model, name);
      if (field == nullptr) {
        AddDiagnostic("idl.semantic_structural_field_unresolved",
                      "Structural field does not resolve in its model.",
                      provenance.sourcePath, provenance.span);
        return std::nullopt;
      }
      if (!seen.insert(field->handle).second) {
        AddDiagnostic("idl.semantic_structural_field_duplicate",
                      "Structural field list repeats a field.",
                      provenance.sourcePath, provenance.span);
        return std::nullopt;
      }
      fields.push_back(FieldReference(*field));
    }
    return fields;
  }

  static bool FieldsEqual(const std::vector<SemanticFieldReference> &lhs,
                          const std::vector<SemanticFieldReference> &rhs) {
    if (lhs.size() != rhs.size())
      return false;
    for (std::size_t index = 0; index < lhs.size(); ++index)
      if (lhs[index].handle != rhs[index].handle)
        return false;
    return true;
  }

  static bool FieldsPrefix(const std::vector<SemanticFieldReference> &prefix,
                           const std::vector<SemanticFieldReference> &values) {
    if (prefix.size() > values.size())
      return false;
    for (std::size_t index = 0; index < prefix.size(); ++index)
      if (prefix[index].handle != values[index].handle)
        return false;
    return true;
  }

  static bool
  HasIndexPrefix(const SemanticModel &model,
                 const std::vector<SemanticFieldReference> &fields) {
    return std::any_of(model.keys.begin(), model.keys.end(),
                       [&fields](const auto &key) {
                         return FieldsPrefix(fields, key.fields);
                       }) ||
           std::any_of(model.indexes.begin(), model.indexes.end(),
                       [&fields](const auto &index) {
                         return FieldsPrefix(fields, index.fields);
                       });
  }

  static bool
  IsUniqueFieldSet(const SemanticModel &model,
                   const std::vector<SemanticFieldReference> &fields) {
    return std::any_of(model.keys.begin(), model.keys.end(),
                       [&fields](const auto &key) {
                         return FieldsEqual(fields, key.fields);
                       }) ||
           std::any_of(model.indexes.begin(), model.indexes.end(),
                       [&fields](const auto &index) {
                         return index.unique &&
                                FieldsEqual(fields, index.fields);
                       });
  }

  void BuildStructures() {
    std::map<std::string, ModelHandle> model_by_qualified_name;
    for (const auto &model : ir_.models)
      model_by_qualified_name.emplace(model.qualifiedName, model.handle);

    for (std::size_t model_index = 0; model_index < modelCandidates_.size();
         ++model_index) {
      const auto &candidate = modelCandidates_[model_index];
      auto &model = ir_.models[model_index];
      const SemanticProvenance model_provenance{candidate.source->path,
                                                candidate.model->span};
      const auto generated_primary_field = std::find_if(
          model.fields.begin(), model.fields.end(),
          [](const auto &field) { return field.generatedPrimaryId; });

      std::set<std::string> key_names;
      std::size_t primary_count = 0;
      for (const auto &declaration : candidate.model->keys) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        const auto kind = ParseKeyKind(declaration.kind);
        const auto fields =
            ResolveFields(model, declaration.fields, provenance, false);
        if (!kind.has_value())
          AddDiagnostic("idl.key_kind_invalid",
                        "Key kind must be primary or alternate.",
                        provenance.sourcePath, provenance.span);
        if (!key_names.insert(declaration.name).second)
          AddDiagnostic("idl.semantic_duplicate_key_name",
                        "Key names must be unique within a model.",
                        provenance.sourcePath, provenance.span);
        if (!kind.has_value() || !fields.has_value())
          continue;
        if (*kind == KeyKind::Primary)
          ++primary_count;
        model.keys.push_back({
            .name = declaration.name,
            .kind = *kind,
            .fields = *fields,
            .generated = false,
            .catalogIdentity =
                CatalogMemberIdentity(declaration.catalogIdentity),
            .provenance = provenance,
        });
      }
      if (primary_count == 0 && generated_primary_field != model.fields.end()) {
        const std::string generated_name = model.name + "_pk";
        if (!key_names.insert(generated_name).second)
          AddDiagnostic(
              "idl.generated_primary_key_name_collision",
              "Generated primary key name collides with a declared key.",
              model_provenance.sourcePath, model_provenance.span);
        else
          model.keys.push_back({
              .name = generated_name,
              .kind = KeyKind::Primary,
              .fields = {FieldReference(*generated_primary_field)},
              .generated = true,
              .provenance = model_provenance,
          });
        primary_count = 1;
      }
      if (primary_count != 1)
        AddDiagnostic("idl.semantic_primary_key_count_invalid",
                      "Every model requires exactly one primary key.",
                      model_provenance.sourcePath, model_provenance.span);
      for (const auto &key : model.keys) {
        if (key.kind != KeyKind::Primary)
          continue;
        if (generated_primary_field != model.fields.end() &&
            (key.fields.size() != 1 ||
             key.fields.front().handle != generated_primary_field->handle))
          AddDiagnostic("idl.semantic_primary_key_shape_invalid",
                        "A model using primary_id shorthand must key exactly "
                        "that generated field.",
                        key.provenance.sourcePath, key.provenance.span);
        for (const auto &reference : key.fields) {
          const auto field =
              std::find_if(model.fields.begin(), model.fields.end(),
                           [&reference](const auto &value) {
                             return value.handle == reference.handle;
                           });
          if (field == model.fields.end())
            continue;
          field->primaryKey = true;
          if (field->cardinality != FieldCardinality::RequiredOne ||
              !field->required || field->redacted || !field->immutable ||
              !field->equalityComparable)
            AddDiagnostic("idl.semantic_primary_key_field_traits_invalid",
                          "Primary-key fields must be required, scalar, "
                          "non-redacted, immutable, and equality-comparable.",
                          field->provenance.sourcePath, field->provenance.span);
        }
      }
      std::sort(model.keys.begin(), model.keys.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });

      std::set<std::string> index_names;
      for (const auto &declaration : candidate.model->indexes) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        const auto fields =
            ResolveFields(model, declaration.fields, provenance, false);
        if (!index_names.insert(declaration.name).second)
          AddDiagnostic("idl.semantic_duplicate_index_name",
                        "Index names must be unique within a model.",
                        provenance.sourcePath, provenance.span);
        if (!fields.has_value())
          continue;
        model.indexes.push_back({
            .name = declaration.name,
            .fields = *fields,
            .unique = declaration.unique,
            .generated = false,
            .catalogIdentity =
                CatalogMemberIdentity(declaration.catalogIdentity),
            .provenance = provenance,
        });
      }
      std::sort(model.indexes.begin(), model.indexes.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });

      std::set<std::string> one_of_names;
      for (const auto &declaration : candidate.model->oneOfs) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        if (!one_of_names.insert(declaration.name).second)
          AddDiagnostic("idl.semantic_duplicate_one_of_name",
                        "One_of names must be unique within a model.",
                        provenance.sourcePath, provenance.span);
        const auto *discriminator =
            FindField(model, declaration.discriminatorField);
        if (discriminator == nullptr ||
            discriminator->type.kind != SemanticTypeKind::Enum ||
            !discriminator->type.definingEnum.has_value()) {
          AddDiagnostic(
              "idl.one_of_discriminator_invalid",
              "One_of discriminator must resolve to a governed enum field.",
              provenance.sourcePath, provenance.span);
          continue;
        }
        const auto &enumeration =
            ir_.enums.at(discriminator->type.definingEnum->value);
        std::set<std::string> expected_tags;
        for (const auto &value : enumeration.values)
          expected_tags.insert(value.name);
        std::set<std::string> tags;
        SemanticOneOf one_of{
            .name = declaration.name,
            .discriminator = FieldReference(*discriminator),
            .enumeration = enumeration.handle,
            .catalogIdentity =
                CatalogMemberIdentity(declaration.catalogIdentity),
            .provenance = provenance,
        };
        for (const auto &variant_declaration : declaration.variants) {
          const SemanticProvenance variant_provenance{
              candidate.source->path,
              variant_declaration.span,
          };
          const auto fields = ResolveFields(model, variant_declaration.fields,
                                            variant_provenance, true);
          const auto optional_fields =
              ResolveFields(model, variant_declaration.optionalFields,
                            variant_provenance, true);
          if (!tags.insert(variant_declaration.tag).second ||
              !expected_tags.contains(variant_declaration.tag))
            AddDiagnostic("idl.one_of_variant_tag_invalid",
                          "One_of variant tag must be a unique value from the "
                          "discriminator enum.",
                          variant_provenance.sourcePath,
                          variant_provenance.span);
          if (!fields.has_value() || !optional_fields.has_value())
            continue;
          std::set<FieldHandle> payload;
          for (const auto &field : *fields)
            payload.insert(field.handle);
          for (const auto &field : *optional_fields) {
            if (!payload.insert(field.handle).second)
              AddDiagnostic("idl.one_of_payload_overlap",
                            "One_of required and optional payload fields must "
                            "be disjoint.",
                            variant_provenance.sourcePath,
                            variant_provenance.span);
          }
          if (payload.contains(discriminator->handle))
            AddDiagnostic("idl.one_of_discriminator_in_payload",
                          "One_of payload cannot contain its discriminator.",
                          variant_provenance.sourcePath,
                          variant_provenance.span);
          for (const auto handle : payload) {
            const auto field = std::find_if(
                model.fields.begin(), model.fields.end(),
                [handle](const auto &value) { return value.handle == handle; });
            if (field != model.fields.end() && field->required)
              AddDiagnostic("idl.one_of_payload_required",
                            "One_of payload fields must be optional outside "
                            "their active variant.",
                            variant_provenance.sourcePath,
                            variant_provenance.span);
          }
          one_of.variants.push_back({
              .tag = variant_declaration.tag,
              .fields = *fields,
              .optionalFields = *optional_fields,
              .catalogIdentity =
                  CatalogMemberIdentity(variant_declaration.catalogIdentity),
              .provenance = variant_provenance,
          });
        }
        if (tags != expected_tags)
          AddDiagnostic(
              "idl.one_of_variant_coverage_incomplete",
              "Every discriminator enum value requires exactly one variant.",
              provenance.sourcePath, provenance.span);
        std::sort(one_of.variants.begin(), one_of.variants.end(),
                  [](const auto &lhs, const auto &rhs) {
                    if (lhs.catalogIdentity.has_value() !=
                        rhs.catalogIdentity.has_value())
                      return lhs.catalogIdentity.has_value();
                    if (lhs.catalogIdentity.has_value() &&
                        lhs.catalogIdentity->declarationOrder !=
                            rhs.catalogIdentity->declarationOrder)
                      return lhs.catalogIdentity->declarationOrder <
                             rhs.catalogIdentity->declarationOrder;
                    return lhs.tag < rhs.tag;
                  });
        model.oneOfs.push_back(std::move(one_of));
      }
      std::sort(model.oneOfs.begin(), model.oneOfs.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });
    }

    for (std::size_t model_index = 0; model_index < modelCandidates_.size();
         ++model_index) {
      const auto &candidate = modelCandidates_[model_index];
      auto &model = ir_.models[model_index];
      std::set<std::string> relation_names;
      std::set<std::string> index_names;
      for (const auto &index : model.indexes)
        index_names.insert(index.name);
      for (const auto &declaration : candidate.model->relations) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        const auto cardinality =
            ParseRelationCardinality(declaration.cardinality);
        const auto on_delete = ParseReferentialAction(declaration.onDelete);
        const auto on_update = ParseReferentialAction(declaration.onUpdate);
        const auto target_found =
            model_by_qualified_name.find(declaration.targetModel);
        if (!relation_names.insert(declaration.name).second)
          AddDiagnostic("idl.semantic_duplicate_relation_name",
                        "Relation names must be unique within a model.",
                        provenance.sourcePath, provenance.span);
        if (!cardinality.has_value() || !on_delete.has_value() ||
            !on_update.has_value())
          AddDiagnostic(
              "idl.relation_policy_invalid",
              "Relation cardinality or referential action is invalid.",
              provenance.sourcePath, provenance.span);
        if (target_found == model_by_qualified_name.end()) {
          AddDiagnostic("idl.relation_target_unresolved",
                        "Relation target model does not resolve.",
                        provenance.sourcePath, provenance.span);
          continue;
        }
        auto &target = ir_.models.at(target_found->second.value);
        const auto source_fields =
            ResolveFields(model, declaration.sourceFields, provenance, false);
        const auto target_fields =
            ResolveFields(target, declaration.targetFields, provenance, false);
        if (!cardinality.has_value() || !on_delete.has_value() ||
            !on_update.has_value() || !source_fields.has_value() ||
            !target_fields.has_value())
          continue;
        if (source_fields->size() != target_fields->size())
          AddDiagnostic("idl.relation_field_arity_mismatch",
                        "Relation source and target field counts must match.",
                        provenance.sourcePath, provenance.span);
        else
          for (std::size_t index = 0; index < source_fields->size(); ++index) {
            const auto *source_field =
                FindField(model, (*source_fields)[index].canonicalName);
            const auto *target_field =
                FindField(target, (*target_fields)[index].canonicalName);
            if (source_field != nullptr && target_field != nullptr &&
                source_field->type.semanticTypeId !=
                    target_field->type.semanticTypeId)
              AddDiagnostic("idl.relation_semantic_type_mismatch",
                            "Relation source and target fields must share "
                            "semantic type IDs in exact order.",
                            provenance.sourcePath, provenance.span);
          }
        if (!HasIndexPrefix(target, *target_fields))
          AddDiagnostic("idl.relation_target_not_keyed",
                        "Relation target fields require a key or index prefix.",
                        provenance.sourcePath, provenance.span);
        if (*cardinality != RelationCardinality::Many &&
            !IsUniqueFieldSet(target, *target_fields))
          AddDiagnostic(
              "idl.singular_relation_target_not_unique",
              "Singular relation target fields must identify a unique row.",
              provenance.sourcePath, provenance.span);
        if (*cardinality == RelationCardinality::One)
          for (const auto &field : *source_fields)
            if (const auto *value = FindField(model, field.canonicalName);
                value != nullptr && !value->required)
              AddDiagnostic(
                  "idl.required_relation_optional",
                  "Required one relations cannot use optional source fields.",
                  provenance.sourcePath, provenance.span);
        if (*cardinality == RelationCardinality::OptionalOne) {
          const bool has_optional =
              std::any_of(source_fields->begin(), source_fields->end(),
                          [&model](const auto &field) {
                            const auto *value =
                                FindField(model, field.canonicalName);
                            return value != nullptr && !value->required;
                          });
          if (!has_optional)
            AddDiagnostic("idl.optional_relation_field_required",
                          "Optional relations require at least one optional "
                          "source field.",
                          provenance.sourcePath, provenance.span);
        }
        if (*on_delete == ReferentialAction::SetNull ||
            *on_update == ReferentialAction::SetNull)
          for (const auto &field : *source_fields)
            if (const auto *value = FindField(model, field.canonicalName);
                value != nullptr && value->required)
              AddDiagnostic(
                  "idl.set_null_relation_field_required",
                  "Set-null actions require every source field to be optional.",
                  provenance.sourcePath, provenance.span);
        if (*on_delete == ReferentialAction::SetNull ||
            *on_update != ReferentialAction::Restrict)
          for (const auto &field : *source_fields)
            if (const auto *value = FindField(model, field.canonicalName);
                value != nullptr && (value->immutable || value->writeOnce))
              AddDiagnostic("idl.immutable_relation_field_action_invalid",
                            "Referential actions cannot rewrite immutable or "
                            "write-once source fields.",
                            provenance.sourcePath, provenance.span);
        if (model.retention == RetentionPolicy::Immutable &&
            (*on_delete != ReferentialAction::Restrict ||
             *on_update != ReferentialAction::Restrict))
          AddDiagnostic("idl.immutable_relation_action_invalid",
                        "Immutable models require restrict relation actions.",
                        provenance.sourcePath, provenance.span);

        model.relations.push_back({
            .name = declaration.name,
            .targetModel = target.handle,
            .targetTypeId = target.typeId,
            .targetQualifiedName = target.qualifiedName,
            .sourceFields = *source_fields,
            .targetFields = *target_fields,
            .cardinality = *cardinality,
            .onDelete = *on_delete,
            .onUpdate = *on_update,
            .catalogIdentity =
                CatalogMemberIdentity(declaration.catalogIdentity),
            .provenance = provenance,
        });
        if (!HasIndexPrefix(model, *source_fields)) {
          const std::string generated_name =
              model.name + "_relation_" + declaration.name;
          if (!index_names.insert(generated_name).second)
            AddDiagnostic(
                "idl.generated_relation_index_name_collision",
                "Generated relation index name collides with a declared index.",
                provenance.sourcePath, provenance.span);
          else
            model.indexes.push_back({
                .name = generated_name,
                .fields = *source_fields,
                .unique = false,
                .generated = true,
                .provenance = provenance,
            });
        }
      }
      std::sort(model.relations.begin(), model.relations.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });
      std::sort(model.indexes.begin(), model.indexes.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });
    }

    for (std::size_t model_index = 0; model_index < modelCandidates_.size();
         ++model_index) {
      const auto &candidate = modelCandidates_[model_index];
      auto &model = ir_.models[model_index];
      std::set<std::string> include_names;
      for (const auto &relation : model.relations)
        include_names.insert(relation.name);
      for (const auto &declaration : candidate.model->joins) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        if (!include_names.insert(declaration.name).second)
          AddDiagnostic(
              "idl.semantic_duplicate_include_name",
              "Relation and join names share one namespace within a model.",
              provenance.sourcePath, provenance.span);
        if (declaration.sourceRelation == declaration.targetRelation)
          AddDiagnostic("idl.join_relations_must_differ",
                        "Join source and target relations must differ.",
                        provenance.sourcePath, provenance.span);
        const auto through_found =
            model_by_qualified_name.find(declaration.throughModel);
        if (through_found == model_by_qualified_name.end()) {
          AddDiagnostic("idl.join_through_model_unresolved",
                        "Join through model does not resolve.",
                        provenance.sourcePath, provenance.span);
          continue;
        }
        const auto &through = ir_.models.at(through_found->second.value);
        const auto source_relation =
            std::find_if(through.relations.begin(), through.relations.end(),
                         [&declaration](const auto &relation) {
                           return relation.name == declaration.sourceRelation;
                         });
        const auto target_relation =
            std::find_if(through.relations.begin(), through.relations.end(),
                         [&declaration](const auto &relation) {
                           return relation.name == declaration.targetRelation;
                         });
        if (source_relation == through.relations.end() ||
            target_relation == through.relations.end()) {
          AddDiagnostic("idl.join_relation_unresolved",
                        "Join relations must resolve on the through model.",
                        provenance.sourcePath, provenance.span);
          continue;
        }
        if (source_relation->targetModel != model.handle)
          AddDiagnostic("idl.join_source_target_mismatch",
                        "Join source relation must target the declaring model.",
                        provenance.sourcePath, provenance.span);
        model.joins.push_back({
            .name = declaration.name,
            .throughModel = through.handle,
            .throughTypeId = through.typeId,
            .throughQualifiedName = through.qualifiedName,
            .sourceRelation = declaration.sourceRelation,
            .targetRelation = declaration.targetRelation,
            .catalogIdentity =
                CatalogMemberIdentity(declaration.catalogIdentity),
            .provenance = provenance,
        });
      }
      std::sort(model.joins.begin(), model.joins.end(),
                [](const auto &lhs, const auto &rhs) {
                  return CatalogMemberLess(lhs, rhs);
                });
    }
  }

  void BuildTemporalContractsAndValidateQueries() {
    for (std::size_t model_index = 0; model_index < modelCandidates_.size();
         ++model_index) {
      const auto &candidate = modelCandidates_[model_index];
      auto &model = ir_.models[model_index];
      for (const auto &field : model.fields) {
        if (!field.query.sortable && !field.query.cursor)
          continue;
        if (!HasIndexPrefix(model, {FieldReference(field)}))
          AddDiagnostic("idl.field_query_index_prefix_missing",
                        "Sortable and cursor fields require a key or index "
                        "with that field as its first component.",
                        field.provenance.sourcePath, field.provenance.span);
      }

      std::set<std::string> names;
      for (const auto &declaration : candidate.model->temporalContracts) {
        const SemanticProvenance provenance{candidate.source->path,
                                            declaration.span};
        const auto ordering = ParseTemporalOrdering(declaration.ordering);
        const auto open_end = ParseOpenEndPolicy(declaration.openEnd);
        const auto *start = FindField(model, declaration.startField);
        const auto *end = FindField(model, declaration.endField);
        if (!names.insert(declaration.name).second)
          AddDiagnostic(
              "idl.semantic_duplicate_temporal_contract_name",
              "Temporal contract names must be unique within a model.",
              provenance.sourcePath, provenance.span);
        if (!ordering.has_value() || !open_end.has_value())
          AddDiagnostic("idl.temporal_contract_policy_invalid",
                        "Temporal ordering must be strict or nondecreasing and "
                        "open_end must be allowed or forbidden.",
                        provenance.sourcePath, provenance.span);
        if (start == nullptr || end == nullptr || start == end) {
          AddDiagnostic("idl.temporal_contract_field_invalid",
                        "Temporal contract endpoints must resolve to two "
                        "distinct fields.",
                        provenance.sourcePath, provenance.span);
          continue;
        }
        const auto &start_type =
            ir_.semanticTypes.at(start->type.semanticType.value);
        const auto &end_type =
            ir_.semanticTypes.at(end->type.semanticType.value);
        if (start_type.kind != SemanticTypeKind::Timestamp ||
            end_type.kind != SemanticTypeKind::Timestamp ||
            start_type.semanticTypeId != end_type.semanticTypeId ||
            start->temporalRole == TemporalRole::None ||
            start->temporalRole != end->temporalRole)
          AddDiagnostic("idl.temporal_contract_type_invalid",
                        "Temporal endpoints must share one governed timestamp "
                        "semantic type and field role.",
                        provenance.sourcePath, provenance.span);
        if (!start->required)
          AddDiagnostic("idl.temporal_contract_start_optional",
                        "Temporal contract start fields must be required.",
                        provenance.sourcePath, provenance.span);
        if (open_end.has_value() &&
            ((*open_end == OpenEndPolicy::Allowed) == end->required))
          AddDiagnostic("idl.temporal_contract_open_end_mismatch",
                        "Open-ended contracts require an optional end; closed "
                        "contracts require a required end.",
                        provenance.sourcePath, provenance.span);
        if (!HasAllValidationTargets(start_type.targets) ||
            !HasAllValidationTargets(end_type.targets))
          AddDiagnostic("idl.policy_target_profile_incomplete",
                        "Temporal contracts require a complete canonical or "
                        "Fabric target profile family.",
                        provenance.sourcePath, provenance.span);
        if (!ordering.has_value() || !open_end.has_value())
          continue;
        model.temporalContracts.push_back({
            .name = declaration.name,
            .startField = FieldReference(*start),
            .endField = FieldReference(*end),
            .ordering = *ordering,
            .openEnd = *open_end,
            .startInclusive = declaration.startInclusive,
            .endInclusive = declaration.endInclusive,
            .provenance = provenance,
        });
      }
      std::sort(
          model.temporalContracts.begin(), model.temporalContracts.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
    }
  }

  std::optional<SemanticModelReference>
  ResolveModelReference(const ParsedSource &source, std::string_view name,
                        SourceSpan span, bool allow_none) {
    if (allow_none && name == "none")
      return std::nullopt;
    const SourceHandle source_handle = sourceHandles_.at(source.path);
    std::vector<ModelHandle> matches;
    if (const auto local =
            modelBySourceAndName_.find({source_handle, std::string(name)});
        local != modelBySourceAndName_.end()) {
      matches.push_back(local->second);
    } else {
      for (const auto &import : ir_.sources.at(source_handle.value).imports) {
        const auto imported =
            modelBySourceAndName_.find({import.source, std::string(name)});
        if (imported != modelBySourceAndName_.end())
          matches.push_back(imported->second);
      }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    if (matches.empty()) {
      AddDiagnostic("idl.operation_model_unresolved",
                    "Operation model reference does not resolve in its source "
                    "or direct imports.",
                    source.path, span);
      return std::nullopt;
    }
    if (matches.size() != 1) {
      AddDiagnostic(
          "idl.operation_model_ambiguous",
          "Operation model reference is ambiguous across direct imports.",
          source.path, span);
      return std::nullopt;
    }
    const auto &model = ir_.models.at(matches.front().value);
    return SemanticModelReference{
        .model = model.handle,
        .typeId = model.typeId,
        .canonicalName = model.qualifiedName,
    };
  }

  std::vector<OperationTargetProfile>
  ResolveOperationTargets(const std::vector<std::string> &names,
                          const ParsedSource &source, SourceSpan span) {
    std::set<OperationTargetProfile> targets;
    for (const auto &name : names) {
      const auto target = ParseOperationTargetProfile(name);
      if (!target.has_value())
        AddDiagnostic("idl.operation_target_invalid",
                      "Operation target must be cpp, fabric, or http_json.",
                      source.path, span);
      else if (!targets.insert(*target).second)
        AddDiagnostic("idl.operation_target_duplicate",
                      "Operation target profiles must be unique.", source.path,
                      span);
    }
    if (targets.empty())
      AddDiagnostic("idl.operation_targets_empty",
                    "Operations require at least one target profile.",
                    source.path, span);
    return {targets.begin(), targets.end()};
  }

  std::optional<FabricOperationProfile> ResolveFabricProfile(
      const std::vector<OperationTargetProfile> &targets,
      const std::optional<std::string> &lane_name,
      const std::optional<std::uint32_t> &maximum_encoded_bytes,
      const std::optional<bool> &direct_eligible, const ParsedSource &source,
      SourceSpan span) {
    const bool has_fabric =
        std::find(targets.begin(), targets.end(),
                  OperationTargetProfile::Fabric) != targets.end();
    if (!has_fabric) {
      if (lane_name.has_value() || maximum_encoded_bytes.has_value() ||
          direct_eligible.has_value())
        AddDiagnostic(
            "idl.fabric_profile_without_target",
            "Fabric operation properties require the fabric target profile.",
            source.path, span);
      return std::nullopt;
    }
    if (!lane_name.has_value() || !maximum_encoded_bytes.has_value() ||
        !direct_eligible.has_value()) {
      AddDiagnostic("idl.fabric_profile_incomplete",
                    "Fabric operation targets require lane, maximum encoded "
                    "bytes, and direct eligibility.",
                    source.path, span);
      return std::nullopt;
    }
    const auto lane = ParseFabricLane(*lane_name);
    if (!lane.has_value())
      AddDiagnostic(
          "idl.fabric_lane_invalid",
          "Fabric lane is not part of the governed Fabric v1 lane vocabulary.",
          source.path, span);
    if (*maximum_encoded_bytes == 0)
      AddDiagnostic("idl.fabric_maximum_encoded_bytes_invalid",
                    "Fabric maximum encoded bytes must be nonzero.",
                    source.path, span);
    if (!lane.has_value() || *maximum_encoded_bytes == 0)
      return std::nullopt;
    return FabricOperationProfile{
        .lane = *lane,
        .maximumEncodedBytes = *maximum_encoded_bytes,
        .directEligible = *direct_eligible,
    };
  }

  void ValidateOperationTargetSupport(
      const std::vector<SemanticModelReference> &models,
      const std::vector<OperationTargetProfile> &operation_targets,
      const SemanticProvenance &provenance) {
    std::set<std::pair<SemanticTypeHandle, TargetProfile>> checked;
    for (const auto &model_reference : models) {
      const auto &model = ir_.models.at(model_reference.model.value);
      for (const auto &field : model.fields) {
        if (!field.type.semanticType.valid() ||
            field.type.semanticType.value >= ir_.semanticTypes.size()) {
          AddDiagnostic("idl.operation_field_type_unresolved",
                        "Operation model field " + model.qualifiedName + "." +
                            field.name +
                            " does not have a resolved semantic type.",
                        field.provenance.sourcePath, field.provenance.span);
          continue;
        }
        for (const auto operation_target : operation_targets) {
          const TargetProfile target =
              operation_target == OperationTargetProfile::Cpp
                  ? TargetProfile::Cpp
              : operation_target == OperationTargetProfile::Fabric
                  ? TargetProfile::Fabric
                  : TargetProfile::HttpJson;
          if (!checked.emplace(field.type.semanticType, target).second)
            continue;
          const auto &semantic_type =
              ir_.semanticTypes.at(field.type.semanticType.value);
          if (std::find(semantic_type.targets.begin(),
                        semantic_type.targets.end(),
                        target) != semantic_type.targets.end())
            continue;
          AddDiagnostic("idl.operation_target_mapping_unsupported",
                        "Operation model uses semantic type " +
                            semantic_type.qualifiedName + " without a " +
                            std::string(TargetProfileName(target)) +
                            " mapping.",
                        provenance.sourcePath, provenance.span);
        }
      }
    }
  }

  void BuildCommands(const Project &project) {
    std::vector<CommandCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.commands) {
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
        });
      }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declaration->commandId != rhs.declaration->commandId)
                  return lhs.declaration->commandId <
                         rhs.declaration->commandId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });
    if (candidates.size() > limits_.maximumCommands ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic(
          "idl.semantic_command_limit_exceeded",
          "Project command count exceeds the configured semantic limit.",
          "<semantic>", {});
      return;
    }

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    for (const auto &candidate : candidates) {
      const auto &declaration = *candidate.declaration;
      const SemanticProvenance provenance{candidate.source->path,
                                          declaration.span};
      if (const auto [existing, inserted] =
              ids.emplace(declaration.commandId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_command_id",
                               "Command IDs must be globally unique.",
                               provenance, existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.qualifiedName, provenance);
          !inserted)
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_command_name",
            "Qualified command names must be globally unique.", provenance,
            existing->second);
      const auto idempotency = ParseCommandIdempotency(declaration.idempotency);
      const auto streaming = ParseCommandStreaming(declaration.streaming);
      const auto targets = ResolveOperationTargets(
          declaration.targets, *candidate.source, declaration.span);
      const auto fabric = ResolveFabricProfile(
          targets, declaration.fabricLane, declaration.maximumEncodedBytes,
          declaration.directEligible, *candidate.source, declaration.span);
      if (!idempotency.has_value())
        AddDiagnostic("idl.command_idempotency_invalid",
                      "Command idempotency must be none, idempotent, or safe.",
                      candidate.source->path, declaration.span);
      if (!streaming.has_value())
        AddDiagnostic("idl.command_streaming_invalid",
                      "Command streaming must be unary, server_stream, "
                      "client_stream, or bidirectional.",
                      candidate.source->path, declaration.span);

      SemanticCommand command{
          .handle =
              CommandHandle{static_cast<std::uint32_t>(ir_.commands.size())},
          .source = sourceHandles_.at(candidate.source->path),
          .commandId = CommandId{declaration.commandId},
          .name = declaration.name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .request = ResolveModelReference(*candidate.source,
                                           declaration.requestModelName,
                                           declaration.span, true),
          .response = ResolveModelReference(*candidate.source,
                                            declaration.responseModelName,
                                            declaration.span, true),
          .idempotency = idempotency.value_or(CommandIdempotency::None),
          .streaming = streaming.value_or(CommandStreaming::Unary),
          .targets = targets,
          .fabric = fabric,
          .provenance = provenance,
      };
      if (!(declaration.errorModelNames.size() == 1 &&
            declaration.errorModelNames.front() == "none")) {
        for (const auto &error_name : declaration.errorModelNames) {
          const auto error = ResolveModelReference(
              *candidate.source, error_name, declaration.span, false);
          if (error.has_value())
            command.errors.push_back(*error);
        }
        std::sort(command.errors.begin(), command.errors.end(),
                  [](const auto &lhs, const auto &rhs) {
                    return lhs.canonicalName < rhs.canonicalName;
                  });
      }
      std::vector<SemanticModelReference> operation_models;
      if (command.request.has_value())
        operation_models.push_back(*command.request);
      if (command.response.has_value())
        operation_models.push_back(*command.response);
      operation_models.insert(operation_models.end(), command.errors.begin(),
                              command.errors.end());
      ValidateOperationTargetSupport(operation_models, command.targets,
                                     command.provenance);
      ir_.sources.at(command.source.value).commands.push_back(command.handle);
      ir_.commands.push_back(std::move(command));
    }
  }

  void BuildEvents(const Project &project) {
    std::vector<EventCandidate> candidates;
    for (const auto &source : project.sources) {
      for (const auto &declaration : source.document.events) {
        candidates.push_back({
            .source = &source,
            .declaration = &declaration,
            .qualifiedName =
                QualifiedDeclarationName(source.document, declaration.name),
        });
      }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.declaration->eventId != rhs.declaration->eventId)
                  return lhs.declaration->eventId < rhs.declaration->eventId;
                return lhs.qualifiedName < rhs.qualifiedName;
              });
    if (candidates.size() > limits_.maximumEvents ||
        candidates.size() > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic(
          "idl.semantic_event_limit_exceeded",
          "Project event count exceeds the configured semantic limit.",
          "<semantic>", {});
      return;
    }

    std::map<std::uint32_t, SemanticProvenance> ids;
    std::map<std::string, SemanticProvenance> names;
    for (const auto &candidate : candidates) {
      const auto &declaration = *candidate.declaration;
      const SemanticProvenance provenance{candidate.source->path,
                                          declaration.span};
      if (const auto [existing, inserted] =
              ids.emplace(declaration.eventId, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_event_id",
                               "Event IDs must be globally unique.", provenance,
                               existing->second);
      if (const auto [existing, inserted] =
              names.emplace(candidate.qualifiedName, provenance);
          !inserted)
        AddDuplicateDiagnostic("idl.semantic_duplicate_event_name",
                               "Qualified event names must be globally unique.",
                               provenance, existing->second);
      const auto delivery = ParseEventDelivery(declaration.delivery);
      const auto ordering = ParseEventOrdering(declaration.ordering);
      const auto replay = ParseEventReplay(declaration.replay);
      const auto targets = ResolveOperationTargets(
          declaration.targets, *candidate.source, declaration.span);
      const auto fabric = ResolveFabricProfile(
          targets, declaration.fabricLane, declaration.maximumEncodedBytes,
          declaration.directEligible, *candidate.source, declaration.span);
      if (!delivery.has_value())
        AddDiagnostic("idl.event_delivery_invalid",
                      "Event delivery must be best_effort or at_least_once.",
                      candidate.source->path, declaration.span);
      if (!ordering.has_value())
        AddDiagnostic(
            "idl.event_ordering_invalid",
            "Event ordering must be none, source, aggregate, or global.",
            candidate.source->path, declaration.span);
      if (!replay.has_value())
        AddDiagnostic("idl.event_replay_invalid",
                      "Event replay must be none, retained, or resumable.",
                      candidate.source->path, declaration.span);
      if (delivery == EventDelivery::BestEffort && replay.has_value() &&
          *replay != EventReplay::None)
        AddDiagnostic(
            "idl.event_replay_delivery_mismatch",
            "Best-effort events cannot promise retained or resumable replay.",
            candidate.source->path, declaration.span);

      const auto payload =
          ResolveModelReference(*candidate.source, declaration.payloadModelName,
                                declaration.span, false);
      SemanticEvent event{
          .handle = EventHandle{static_cast<std::uint32_t>(ir_.events.size())},
          .source = sourceHandles_.at(candidate.source->path),
          .eventId = EventId{declaration.eventId},
          .name = declaration.name,
          .qualifiedName = candidate.qualifiedName,
          .ownerCapsule = candidate.source->document.ownerCapsule,
          .payload = payload.value_or(SemanticModelReference{}),
          .delivery = delivery.value_or(EventDelivery::BestEffort),
          .ordering = ordering.value_or(EventOrdering::None),
          .replay = replay.value_or(EventReplay::None),
          .targets = targets,
          .fabric = fabric,
          .provenance = provenance,
      };
      if (payload.has_value())
        ValidateOperationTargetSupport({*payload}, event.targets,
                                       event.provenance);
      ir_.sources.at(event.source.value).events.push_back(event.handle);
      ir_.events.push_back(std::move(event));
    }
  }

  void BuildReservations(const Project &project) {
    std::size_t reservation_count = 0;
    for (const auto &source : project.sources) {
      reservation_count += source.document.reservedIds.size() +
                           source.document.reservedNames.size();
      for (const auto &model : source.document.models)
        reservation_count += model.reservedIds.size() +
                             model.reservedNames.size() +
                             model.retiredNames.size();
      for (const auto &declaration : source.document.enums)
        reservation_count +=
            declaration.reservedIds.size() + declaration.reservedNames.size();
    }
    if (reservation_count > limits_.maximumReservations) {
      AddDiagnostic("idl.semantic_reservation_limit_exceeded",
                    "Semantic reservation count exceeds the configured limit.",
                    "<semantic>", {});
      return;
    }
    for (const auto &source : project.sources) {
      for (const auto &reserved : source.document.reservedIds) {
        ir_.reservedIds.push_back({
            .scope = {.kind = reserved.registry},
            .value = reserved.value,
            .ownerCapsule = source.document.ownerCapsule,
            .provenance = {.sourcePath = source.path, .span = reserved.span},
        });
      }
      for (const auto &reserved : source.document.reservedNames) {
        ir_.reservedNames.push_back({
            .scope = {.kind = reserved.registry},
            .canonicalName =
                QualifiedReservationName(source.document, reserved.value),
            .ownerCapsule = source.document.ownerCapsule,
            .provenance = {.sourcePath = source.path, .span = reserved.span},
        });
      }
      for (const auto &model : source.document.models) {
        const RegistryScope field_scope{
            .kind = RegistryKind::Field,
            .modelTypeId = ModelTypeId{model.typeId},
        };
        for (const auto &reserved : model.reservedIds) {
          ir_.reservedIds.push_back({
              .scope = field_scope,
              .value = reserved.value,
              .ownerCapsule = source.document.ownerCapsule,
              .provenance = {.sourcePath = source.path, .span = reserved.span},
          });
        }
        for (const auto &reserved : model.reservedNames) {
          ir_.reservedNames.push_back({
              .scope = field_scope,
              .canonicalName = reserved.value,
              .ownerCapsule = source.document.ownerCapsule,
              .provenance = {.sourcePath = source.path, .span = reserved.span},
          });
        }
        for (const auto &retired_name : model.retiredNames) {
          ir_.reservedNames.push_back({
              .scope = {.kind = RegistryKind::Model},
              .canonicalName =
                  QualifiedReservationName(source.document, retired_name),
              .ownerCapsule = source.document.ownerCapsule,
              .provenance = {.sourcePath = source.path, .span = model.span},
          });
        }
      }
      for (const auto &declaration : source.document.enums) {
        const RegistryScope value_scope{
            .kind = RegistryKind::EnumValue,
            .enumId = EnumId{declaration.enumId},
        };
        for (const auto &reserved : declaration.reservedIds) {
          ir_.reservedIds.push_back({
              .scope = value_scope,
              .value = reserved.value,
              .ownerCapsule = source.document.ownerCapsule,
              .provenance = {.sourcePath = source.path, .span = reserved.span},
          });
        }
        for (const auto &reserved : declaration.reservedNames) {
          ir_.reservedNames.push_back({
              .scope = value_scope,
              .canonicalName = reserved.value,
              .ownerCapsule = source.document.ownerCapsule,
              .provenance = {.sourcePath = source.path, .span = reserved.span},
          });
        }
      }
    }

    std::sort(ir_.reservedIds.begin(), ir_.reservedIds.end(),
              [](const auto &lhs, const auto &rhs) {
                return std::tie(lhs.scope, lhs.value, lhs.ownerCapsule) <
                       std::tie(rhs.scope, rhs.value, rhs.ownerCapsule);
              });
    std::sort(ir_.reservedNames.begin(), ir_.reservedNames.end(),
              [](const auto &lhs, const auto &rhs) {
                return std::tie(lhs.scope, lhs.canonicalName,
                                lhs.ownerCapsule) <
                       std::tie(rhs.scope, rhs.canonicalName, rhs.ownerCapsule);
              });
    ValidateReservations();
  }

  void ValidateReservations() {
    std::map<std::pair<RegistryScope, std::uint32_t>, SemanticProvenance>
        reserved_ids;
    std::map<std::pair<RegistryScope, std::string>, SemanticProvenance>
        reserved_names;
    std::set<std::pair<RegistryScope, std::uint32_t>> active_ids;
    std::set<std::pair<RegistryScope, std::string>> active_names;
    for (const auto &wire : ir_.wireTypes) {
      const RegistryScope scope{.kind = RegistryKind::WireType};
      active_ids.emplace(scope, wire.wireTypeId.value);
      active_names.emplace(scope, wire.qualifiedName);
    }
    for (const auto &codec : ir_.storageCodecs) {
      const RegistryScope scope{.kind = RegistryKind::StorageCodec};
      active_ids.emplace(scope, codec.storageCodecId.value);
      active_names.emplace(scope, codec.qualifiedName);
    }
    for (const auto &type : ir_.semanticTypes) {
      const RegistryScope scope{.kind = RegistryKind::SemanticType};
      active_ids.emplace(scope, type.semanticTypeId.value);
      active_names.emplace(scope, type.qualifiedName);
    }
    for (const auto &command : ir_.commands) {
      const RegistryScope scope{.kind = RegistryKind::Command};
      active_ids.emplace(scope, command.commandId.value);
      active_names.emplace(scope, command.qualifiedName);
    }
    for (const auto &event : ir_.events) {
      const RegistryScope scope{.kind = RegistryKind::Event};
      active_ids.emplace(scope, event.eventId.value);
      active_names.emplace(scope, event.qualifiedName);
    }
    for (const auto &enumeration : ir_.enums) {
      const RegistryScope enum_scope{.kind = RegistryKind::Enum};
      active_ids.emplace(enum_scope, enumeration.enumId.value);
      active_names.emplace(enum_scope, enumeration.qualifiedName);
      const RegistryScope value_scope{
          .kind = RegistryKind::EnumValue,
          .enumId = enumeration.enumId,
      };
      for (const auto &value : enumeration.values) {
        active_ids.emplace(value_scope, value.valueId.value);
        active_names.emplace(value_scope, value.name);
      }
    }
    const RegistryScope model_scope{.kind = RegistryKind::Model};
    for (const auto &model : ir_.models) {
      active_ids.emplace(model_scope, model.typeId.value);
      active_names.emplace(model_scope, model.qualifiedName);
      const RegistryScope field_scope{
          .kind = RegistryKind::Field,
          .modelTypeId = model.typeId,
      };
      for (const auto &field : model.fields) {
        active_ids.emplace(field_scope, field.fieldId.value);
        active_names.emplace(field_scope, field.name);
      }
    }
    for (const auto &reserved : ir_.reservedIds) {
      if (!reserved.scope.valid() || reserved.value == 0) {
        AddDiagnostic("idl.semantic_reservation_invalid",
                      "Reserved numeric identity has an invalid registry scope "
                      "or zero value.",
                      reserved.provenance.sourcePath, reserved.provenance.span);
        continue;
      }
      const auto key = std::pair{reserved.scope, reserved.value};
      if (const auto [existing, inserted] =
              reserved_ids.emplace(key, reserved.provenance);
          !inserted) {
        AddDuplicateDiagnostic("idl.semantic_duplicate_reserved_id",
                               "Reserved numeric identity must be unique "
                               "within its registry scope.",
                               reserved.provenance, existing->second);
      }
      if (active_ids.contains(key)) {
        AddDiagnostic("idl.semantic_reserved_id_active",
                      "Active numeric identity cannot also be retired.",
                      reserved.provenance.sourcePath, reserved.provenance.span);
      }
    }
    for (const auto &reserved : ir_.reservedNames) {
      if (!reserved.scope.valid() || reserved.canonicalName.empty()) {
        AddDiagnostic("idl.semantic_reservation_invalid",
                      "Reserved canonical name has an invalid registry scope "
                      "or empty value.",
                      reserved.provenance.sourcePath, reserved.provenance.span);
        continue;
      }
      const auto key = std::pair{reserved.scope, reserved.canonicalName};
      if (const auto [existing, inserted] =
              reserved_names.emplace(key, reserved.provenance);
          !inserted) {
        AddDuplicateDiagnostic(
            "idl.semantic_duplicate_reserved_name",
            "Reserved canonical name must be unique within its registry scope.",
            reserved.provenance, existing->second);
      }
      if (active_names.contains(key)) {
        AddDiagnostic("idl.semantic_reserved_name_active",
                      "Active canonical name cannot also be retired.",
                      reserved.provenance.sourcePath, reserved.provenance.span);
      }
    }
  }

  void ResolveImports(const Project &project) {
    for (const auto &parsed_source : project.sources) {
      const auto source_found = sourceHandles_.find(parsed_source.path);
      if (source_found == sourceHandles_.end())
        continue;
      auto &source = ir_.sources.at(source_found->second.value);
      std::vector<const ImportDeclaration *> imports;
      imports.reserve(parsed_source.document.imports.size());
      for (const auto &import : parsed_source.document.imports)
        imports.push_back(&import);
      std::sort(imports.begin(), imports.end(),
                [](const auto *lhs, const auto *rhs) {
                  return lhs->path < rhs->path;
                });
      for (const auto *import : imports) {
        const auto target = sourceHandles_.find(import->path);
        if (target == sourceHandles_.end()) {
          AddDiagnostic("idl.semantic_import_unresolved",
                        "Import does not resolve to a semantic source handle.",
                        parsed_source.path, import->pathSpan);
          continue;
        }
        source.imports.push_back({
            .source = target->second,
            .sourcePath = import->path,
            .provenance =
                {
                    .sourcePath = parsed_source.path,
                    .span = import->span,
                },
        });
      }
    }
  }

  void ResolveEntry(const Project &project) {
    const auto entry = sourceHandles_.find(project.entryPath);
    if (entry == sourceHandles_.end()) {
      AddDiagnostic(
          "idl.semantic_entry_unresolved",
          "Project entry path does not resolve to a semantic source handle.",
          project.entryPath, {});
      return;
    }
    ir_.entrySource = entry->second;
  }

  static std::string QualifiedName(const Document &document,
                                   const ModelDeclaration &model) {
    return document.schemaName + ".v" + std::to_string(document.schemaVersion) +
           "." + model.name;
  }

  static std::string QualifiedReservationName(const Document &document,
                                              std::string_view name) {
    return document.schemaName + ".v" + std::to_string(document.schemaVersion) +
           "." + std::string(name);
  }

  void AddDuplicateDiagnostic(std::string code, std::string message,
                              const SemanticProvenance &current,
                              const SemanticProvenance &first) {
    AddDiagnostic(std::move(code), std::move(message), current.sourcePath,
                  current.span,
                  {{
                      .message = "The first declaration is here.",
                      .sourceName = first.sourcePath,
                      .span = first.span,
                  }});
  }

  void AddDiagnostic(std::string code, std::string message,
                     std::string source_name, SourceSpan span,
                     std::vector<RelatedDiagnosticSpan> related_spans = {}) {
    hadError_ = true;
    if (diagnostics_.size() >= limits_.maximumDiagnostics)
      return;
    diagnostics_.push_back({
        .code = std::move(code),
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .sourceName = std::move(source_name),
        .span = span,
        .relatedSpans = std::move(related_spans),
    });
  }

  SemanticManifestResult Finish() {
    std::sort(diagnostics_.begin(), diagnostics_.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.sourceName != rhs.sourceName)
                  return lhs.sourceName < rhs.sourceName;
                if (lhs.span.begin.offset != rhs.span.begin.offset)
                  return lhs.span.begin.offset < rhs.span.begin.offset;
                if (lhs.code != rhs.code)
                  return lhs.code < rhs.code;
                return lhs.message < rhs.message;
              });
    if (hadError_)
      return {.manifest = std::nullopt, .diagnostics = std::move(diagnostics_)};
    return {
        .manifest = detail::CreateSemanticManifest(std::move(ir_)),
        .diagnostics = std::move(diagnostics_),
    };
  }

  const ParseLimits &limits_;
  SemanticIr ir_;
  std::map<std::string, SourceHandle> sourceHandles_;
  std::vector<const ParsedSource *> sourceByHandle_;
  std::map<const ModelDeclaration *, ModelHandle> modelHandles_;
  std::map<std::pair<SourceHandle, std::string>, ModelHandle>
      modelBySourceAndName_;
  std::map<std::pair<SourceHandle, std::string>, EnumHandle>
      enumBySourceAndName_;
  std::vector<ModelCandidate> modelCandidates_;
  std::map<std::string, WireTypeHandle> wireByName_;
  std::map<WirePrimitiveKind, WireTypeHandle> wireByKind_;
  std::map<std::string, StorageCodecHandle> storageByName_;
  std::map<StorageCodecKind, StorageCodecHandle> storageByKind_;
  std::map<std::string, SemanticTypeHandle> semanticByName_;
  std::map<std::pair<SourceHandle, std::string>, SemanticTypeHandle>
      semanticBySourceAndName_;
  std::vector<Diagnostic> diagnostics_;
  std::uint32_t nextFieldHandle_ = 0;
  bool hadError_ = false;
};
} // namespace

bool SourceHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool ModelHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool FieldHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool EnumHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool WireTypeHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool StorageCodecHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool SemanticTypeHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool CommandHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool EventHandle::valid() const {
  return value != std::numeric_limits<std::uint32_t>::max();
}

bool SemanticManifestResult::ok() const {
  return manifest.has_value() &&
         std::none_of(diagnostics.begin(), diagnostics.end(),
                      [](const Diagnostic &diagnostic) {
                        return diagnostic.severity == DiagnosticSeverity::Error;
                      });
}

bool TargetSupportResult::ok() const { return diagnostics.empty(); }

std::string_view WirePrimitiveKindName(WirePrimitiveKind kind) {
  switch (kind) {
  case WirePrimitiveKind::Bool:
    return "bool";
  case WirePrimitiveKind::U8:
    return "u8";
  case WirePrimitiveKind::U16:
    return "u16";
  case WirePrimitiveKind::U32:
    return "u32";
  case WirePrimitiveKind::U64:
    return "u64";
  case WirePrimitiveKind::I8:
    return "i8";
  case WirePrimitiveKind::I16:
    return "i16";
  case WirePrimitiveKind::I32:
    return "i32";
  case WirePrimitiveKind::I64:
    return "i64";
  case WirePrimitiveKind::F32:
    return "f32";
  case WirePrimitiveKind::F64:
    return "f64";
  case WirePrimitiveKind::Utf8:
    return "utf8";
  case WirePrimitiveKind::Bytes:
    return "bytes";
  case WirePrimitiveKind::FixedBytes:
    return "fixed_bytes";
  case WirePrimitiveKind::Decimal128:
    return "decimal128";
  }
  return "unknown";
}

std::string_view WireByteOrderName(WireByteOrder order) {
  switch (order) {
  case WireByteOrder::None:
    return "none";
  case WireByteOrder::BigEndian:
    return "big_endian";
  }
  return "unknown";
}

std::string_view
WireCompatibilityClassName(WireCompatibilityClass compatibility) {
  switch (compatibility) {
  case WireCompatibilityClass::Boolean:
    return "boolean";
  case WireCompatibilityClass::FixedUnsigned:
    return "fixed_unsigned";
  case WireCompatibilityClass::FixedSigned:
    return "fixed_signed";
  case WireCompatibilityClass::Ieee754:
    return "ieee754";
  case WireCompatibilityClass::Utf8:
    return "utf8";
  case WireCompatibilityClass::Bytes:
    return "bytes";
  case WireCompatibilityClass::FixedBytes:
    return "fixed_bytes";
  case WireCompatibilityClass::Decimal128:
    return "decimal128";
  }
  return "unknown";
}

WireCompatibilityClass WireCompatibilityClassOf(WirePrimitiveKind kind) {
  switch (kind) {
  case WirePrimitiveKind::Bool:
    return WireCompatibilityClass::Boolean;
  case WirePrimitiveKind::U8:
  case WirePrimitiveKind::U16:
  case WirePrimitiveKind::U32:
  case WirePrimitiveKind::U64:
    return WireCompatibilityClass::FixedUnsigned;
  case WirePrimitiveKind::I8:
  case WirePrimitiveKind::I16:
  case WirePrimitiveKind::I32:
  case WirePrimitiveKind::I64:
    return WireCompatibilityClass::FixedSigned;
  case WirePrimitiveKind::F32:
  case WirePrimitiveKind::F64:
    return WireCompatibilityClass::Ieee754;
  case WirePrimitiveKind::Utf8:
    return WireCompatibilityClass::Utf8;
  case WirePrimitiveKind::Bytes:
    return WireCompatibilityClass::Bytes;
  case WirePrimitiveKind::FixedBytes:
    return WireCompatibilityClass::FixedBytes;
  case WirePrimitiveKind::Decimal128:
    return WireCompatibilityClass::Decimal128;
  }
  return WireCompatibilityClass::Bytes;
}

std::string_view StorageCodecKindName(StorageCodecKind kind) {
  switch (kind) {
  case StorageCodecKind::SqliteInteger:
    return "sqlite_integer";
  case StorageCodecKind::SqliteReal:
    return "sqlite_real";
  case StorageCodecKind::SqliteText:
    return "sqlite_text";
  case StorageCodecKind::SqliteBlob:
    return "sqlite_blob";
  case StorageCodecKind::SqliteFixedBlob:
    return "sqlite_fixed_blob";
  case StorageCodecKind::SqliteOrderedU64:
    return "sqlite_ordered_u64";
  case StorageCodecKind::SqliteOrderedDecimal128:
    return "sqlite_ordered_decimal128";
  }
  return "unknown";
}

std::string_view SqliteStorageClassName(SqliteStorageClass storage_class) {
  switch (storage_class) {
  case SqliteStorageClass::Integer:
    return "integer";
  case SqliteStorageClass::Real:
    return "real";
  case SqliteStorageClass::Text:
    return "text";
  case SqliteStorageClass::Blob:
    return "blob";
  }
  return "unknown";
}

std::string_view SemanticTypeKindName(SemanticTypeKind kind) {
  switch (kind) {
  case SemanticTypeKind::Bool:
    return "bool";
  case SemanticTypeKind::U8:
    return "u8";
  case SemanticTypeKind::U16:
    return "u16";
  case SemanticTypeKind::U32:
    return "u32";
  case SemanticTypeKind::U64:
    return "u64";
  case SemanticTypeKind::I8:
    return "i8";
  case SemanticTypeKind::I16:
    return "i16";
  case SemanticTypeKind::I32:
    return "i32";
  case SemanticTypeKind::I64:
    return "i64";
  case SemanticTypeKind::F32:
    return "f32";
  case SemanticTypeKind::F64:
    return "f64";
  case SemanticTypeKind::Text:
    return "text";
  case SemanticTypeKind::Enum:
    return "enum";
  case SemanticTypeKind::Blob:
    return "blob";
  case SemanticTypeKind::FixedId:
    return "fixed_id";
  case SemanticTypeKind::Decimal:
    return "decimal";
  case SemanticTypeKind::Timestamp:
    return "timestamp";
  case SemanticTypeKind::JsonText:
    return "json_text";
  }
  return "unknown";
}

std::string_view FieldKindName(FieldKind kind) {
  switch (kind) {
  case FieldKind::ObjectId:
    return "object_id";
  case FieldKind::InternalId:
    return "internal_id";
  case FieldKind::Text:
    return "text";
  case FieldKind::Slug:
    return "slug";
  case FieldKind::DisplayName:
    return "display_name";
  case FieldKind::Url:
    return "url";
  case FieldKind::Email:
    return "email";
  case FieldKind::Enum:
    return "enum";
  case FieldKind::Bool:
    return "bool";
  case FieldKind::TimestampMs:
    return "timestamp_ms";
  case FieldKind::JsonText:
    return "json_text";
  case FieldKind::Secret:
    return "secret";
  case FieldKind::EthereumAddress:
    return "ethereum_address";
  case FieldKind::HashHex:
    return "hash_hex";
  case FieldKind::TokenHash:
    return "token_hash";
  case FieldKind::KeyReference:
    return "key_reference";
  case FieldKind::Integer:
    return "integer";
  case FieldKind::Real:
    return "real";
  case FieldKind::Decimal:
    return "decimal";
  case FieldKind::Blob:
    return "blob";
  }
  return "unknown";
}

std::string_view NormalizationKindName(NormalizationKind kind) {
  switch (kind) {
  case NormalizationKind::None:
    return "none";
  case NormalizationKind::Trim:
    return "trim";
  case NormalizationKind::Lowercase:
    return "lowercase";
  case NormalizationKind::LowercaseTrim:
    return "lowercase_trim";
  case NormalizationKind::EthereumAddress:
    return "ethereum_address";
  case NormalizationKind::Url:
    return "url";
  }
  return "unknown";
}

std::string_view PresencePolicyName(PresencePolicy policy) {
  return policy == PresencePolicy::Explicit ? "explicit" : "unknown";
}

std::string_view DefaultPolicyName(DefaultPolicy policy) {
  return policy == DefaultPolicy::None ? "none" : "unknown";
}

std::string_view TemporalRoleName(TemporalRole role) {
  switch (role) {
  case TemporalRole::None:
    return "none";
  case TemporalRole::ValidTime:
    return "valid_time";
  case TemporalRole::TransactionTime:
    return "transaction_time";
  case TemporalRole::OccurrenceTime:
    return "occurrence_time";
  case TemporalRole::ObservationTime:
    return "observation_time";
  case TemporalRole::PublicationTime:
    return "publication_time";
  case TemporalRole::BoundaryCreationTime:
    return "boundary_creation_time";
  }
  return "unknown";
}

std::string_view NarrowingPolicyName(NarrowingPolicy policy) {
  return policy == NarrowingPolicy::Reject ? "reject" : "unknown";
}

std::string_view TargetProfileName(TargetProfile profile) {
  switch (profile) {
  case TargetProfile::Cpp:
    return "cpp";
  case TargetProfile::Sqlite:
    return "sqlite";
  case TargetProfile::CSharp:
    return "csharp";
  case TargetProfile::JavaScript:
    return "javascript";
  case TargetProfile::Fabric:
    return "fabric";
  case TargetProfile::HttpJson:
    return "http_json";
  }
  return "unknown";
}

std::string_view OperationTargetProfileName(OperationTargetProfile profile) {
  switch (profile) {
  case OperationTargetProfile::Cpp:
    return "cpp";
  case OperationTargetProfile::Fabric:
    return "fabric";
  case OperationTargetProfile::HttpJson:
    return "http_json";
  }
  return "unknown";
}

std::string_view FieldCardinalityName(FieldCardinality cardinality) {
  switch (cardinality) {
  case FieldCardinality::RequiredOne:
    return "required_one";
  case FieldCardinality::OptionalOne:
    return "optional_one";
  case FieldCardinality::Repeated:
    return "repeated";
  }
  return "unknown";
}

std::string_view FabricLaneName(FabricLane lane) {
  switch (lane) {
  case FabricLane::ControlReliable:
    return "control_reliable";
  case FabricLane::StateReliable:
    return "state_reliable";
  case FabricLane::BulkReliable:
    return "bulk_reliable";
  case FabricLane::RealtimeUnreliable:
    return "realtime_unreliable";
  case FabricLane::Compatibility:
    return "compatibility";
  }
  return "unknown";
}

std::string_view CommandIdempotencyName(CommandIdempotency policy) {
  switch (policy) {
  case CommandIdempotency::None:
    return "none";
  case CommandIdempotency::Idempotent:
    return "idempotent";
  case CommandIdempotency::Safe:
    return "safe";
  }
  return "unknown";
}

std::string_view CommandStreamingName(CommandStreaming streaming) {
  switch (streaming) {
  case CommandStreaming::Unary:
    return "unary";
  case CommandStreaming::ServerStream:
    return "server_stream";
  case CommandStreaming::ClientStream:
    return "client_stream";
  case CommandStreaming::Bidirectional:
    return "bidirectional";
  }
  return "unknown";
}

std::string_view EventDeliveryName(EventDelivery delivery) {
  switch (delivery) {
  case EventDelivery::BestEffort:
    return "best_effort";
  case EventDelivery::AtLeastOnce:
    return "at_least_once";
  }
  return "unknown";
}

std::string_view EventOrderingName(EventOrdering ordering) {
  switch (ordering) {
  case EventOrdering::None:
    return "none";
  case EventOrdering::Source:
    return "source";
  case EventOrdering::Aggregate:
    return "aggregate";
  case EventOrdering::Global:
    return "global";
  }
  return "unknown";
}

std::string_view EventReplayName(EventReplay replay) {
  switch (replay) {
  case EventReplay::None:
    return "none";
  case EventReplay::Retained:
    return "retained";
  case EventReplay::Resumable:
    return "resumable";
  }
  return "unknown";
}

std::string_view ModelKindName(ModelKind kind) {
  switch (kind) {
  case ModelKind::Entity:
    return "entity";
  case ModelKind::Join:
    return "join";
  case ModelKind::Credential:
    return "credential";
  case ModelKind::Ephemeral:
    return "ephemeral";
  case ModelKind::AuditLedger:
    return "audit_ledger";
  case ModelKind::Provenance:
    return "provenance";
  case ModelKind::ImportLedger:
    return "import_ledger";
  }
  return "unknown";
}

std::string_view ModelRoleName(ModelRole role) {
  switch (role) {
  case ModelRole::Unspecified:
    return "unspecified";
  case ModelRole::UniversalSpine:
    return "universal_spine";
  case ModelRole::DomainFacet:
    return "domain_facet";
  case ModelRole::Definition:
    return "definition";
  case ModelRole::Revision:
    return "revision";
  case ModelRole::Instance:
    return "instance";
  case ModelRole::Occurrence:
    return "occurrence";
  case ModelRole::Manifestation:
    return "manifestation";
  case ModelRole::Representation:
    return "representation";
  case ModelRole::Assertion:
    return "assertion";
  case ModelRole::Observation:
    return "observation";
  case ModelRole::AuthoritativeAggregate:
    return "authoritative_aggregate";
  case ModelRole::Ledger:
    return "ledger";
  case ModelRole::Registry:
    return "registry";
  case ModelRole::Projection:
    return "projection";
  case ModelRole::ImportRecord:
    return "import_record";
  case ModelRole::Receipt:
    return "receipt";
  case ModelRole::Membership:
    return "membership";
  case ModelRole::ValueObject:
    return "value_object";
  }
  return "unknown";
}

std::string_view RetentionPolicyName(RetentionPolicy policy) {
  switch (policy) {
  case RetentionPolicy::Durable:
    return "durable";
  case RetentionPolicy::UntilExpiry:
    return "until_expiry";
  case RetentionPolicy::Immutable:
    return "immutable";
  case RetentionPolicy::UntilPromotion:
    return "until_promotion";
  }
  return "unknown";
}

std::string_view KeyKindName(KeyKind kind) {
  switch (kind) {
  case KeyKind::Primary:
    return "primary";
  case KeyKind::Alternate:
    return "alternate";
  }
  return "unknown";
}

std::string_view RelationCardinalityName(RelationCardinality cardinality) {
  switch (cardinality) {
  case RelationCardinality::One:
    return "one";
  case RelationCardinality::OptionalOne:
    return "optional_one";
  case RelationCardinality::Many:
    return "many";
  }
  return "unknown";
}

std::string_view ReferentialActionName(ReferentialAction action) {
  switch (action) {
  case ReferentialAction::Restrict:
    return "restrict";
  case ReferentialAction::Cascade:
    return "cascade";
  case ReferentialAction::SetNull:
    return "set_null";
  }
  return "unknown";
}

std::string_view FieldDefaultKindName(FieldDefaultKind kind) {
  switch (kind) {
  case FieldDefaultKind::None:
    return "none";
  case FieldDefaultKind::Literal:
    return "literal";
  case FieldDefaultKind::Boundary:
    return "boundary";
  }
  return "unknown";
}

std::string_view BoundaryDefaultSourceName(BoundaryDefaultSource source) {
  switch (source) {
  case BoundaryDefaultSource::None:
    return "none";
  case BoundaryDefaultSource::RequestTime:
    return "request_time";
  case BoundaryDefaultSource::GeneratedId:
    return "generated_id";
  case BoundaryDefaultSource::GeneratedToken:
    return "generated_token";
  }
  return "unknown";
}

std::string_view CheckKindName(CheckKind kind) {
  switch (kind) {
  case CheckKind::MinimumInteger:
    return "minimum_integer";
  case CheckKind::MaximumInteger:
    return "maximum_integer";
  case CheckKind::MinimumReal:
    return "minimum_real";
  case CheckKind::MaximumReal:
    return "maximum_real";
  case CheckKind::MinimumLength:
    return "minimum_length";
  case CheckKind::MaximumLength:
    return "maximum_length";
  case CheckKind::FixedLength:
    return "fixed_length";
  case CheckKind::EnumDomain:
    return "enum_domain";
  case CheckKind::ValidJson:
    return "valid_json";
  case CheckKind::DecimalPrecision:
    return "decimal_precision";
  case CheckKind::DecimalScale:
    return "decimal_scale";
  }
  return "unknown";
}

std::string_view CheckUnitName(CheckUnit unit) {
  switch (unit) {
  case CheckUnit::None:
    return "none";
  case CheckUnit::Utf8Bytes:
    return "utf8_bytes";
  case CheckUnit::Bytes:
    return "bytes";
  }
  return "unknown";
}

std::string_view QueryOperatorName(QueryOperator operation) {
  switch (operation) {
  case QueryOperator::Equal:
    return "equal";
  case QueryOperator::NotEqual:
    return "not_equal";
  case QueryOperator::In:
    return "in";
  case QueryOperator::NotIn:
    return "not_in";
  case QueryOperator::LessThan:
    return "less_than";
  case QueryOperator::LessThanOrEqual:
    return "less_than_or_equal";
  case QueryOperator::GreaterThan:
    return "greater_than";
  case QueryOperator::GreaterThanOrEqual:
    return "greater_than_or_equal";
  case QueryOperator::Prefix:
    return "prefix";
  case QueryOperator::Suffix:
    return "suffix";
  case QueryOperator::Contains:
    return "contains";
  case QueryOperator::IsNull:
    return "is_null";
  }
  return "unknown";
}

std::string_view SerializationPolicyName(SerializationPolicy policy) {
  switch (policy) {
  case SerializationPolicy::Include:
    return "include";
  case SerializationPolicy::Omit:
    return "omit";
  }
  return "unknown";
}

std::string_view TemporalOrderingName(TemporalOrdering ordering) {
  switch (ordering) {
  case TemporalOrdering::Strict:
    return "strict";
  case TemporalOrdering::Nondecreasing:
    return "nondecreasing";
  }
  return "unknown";
}

std::string_view OpenEndPolicyName(OpenEndPolicy policy) {
  switch (policy) {
  case OpenEndPolicy::Forbidden:
    return "forbidden";
  case OpenEndPolicy::Allowed:
    return "allowed";
  }
  return "unknown";
}

std::string_view NameReviewStatusName(NameReviewStatus status) {
  switch (status) {
  case NameReviewStatus::Canonical:
    return "canonical";
  case NameReviewStatus::ReviewRequired:
    return "review_required";
  case NameReviewStatus::ApprovedException:
    return "approved_exception";
  }
  return "unknown";
}

std::string_view FieldSourceKindName(FieldSourceKind kind) {
  switch (kind) {
  case FieldSourceKind::Canonical:
    return "canonical";
  case FieldSourceKind::Imported:
    return "imported";
  case FieldSourceKind::Derived:
    return "derived";
  case FieldSourceKind::BoundaryGenerated:
    return "boundary_generated";
  }
  return "unknown";
}

bool SemanticTypesMatch(const SemanticTypeReference &lhs,
                        const SemanticTypeReference &rhs) {
  return lhs.semanticTypeId.valid() && lhs.semanticTypeId == rhs.semanticTypeId;
}

SemanticManifestResult BuildSemanticManifest(const Project &project,
                                             const ParseLimits &limits) {
  return SemanticBuilder(limits).Build(project);
}
} // namespace arken::schema::idl::v1
