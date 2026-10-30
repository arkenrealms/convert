#pragma once

#include <optional>
#include <string_view>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::idl::v1::detail
{
[[nodiscard]] std::optional<TargetProfile> ParseTargetProfile(std::string_view value);
[[nodiscard]] std::optional<OperationTargetProfile> ParseOperationTargetProfile(std::string_view value);
[[nodiscard]] std::optional<FabricLane> ParseFabricLane(std::string_view value);
[[nodiscard]] std::optional<ModelKind> ParseModelKind(std::string_view value);
[[nodiscard]] std::optional<ModelRole> ParseModelRole(std::string_view value);
[[nodiscard]] std::optional<RetentionPolicy> ParseRetentionPolicy(std::string_view value);
[[nodiscard]] std::optional<NameReviewStatus> ParseNameReviewStatus(std::string_view value);
[[nodiscard]] std::optional<FieldSourceKind> ParseFieldSourceKind(std::string_view value);
[[nodiscard]] std::optional<KeyKind> ParseKeyKind(std::string_view value);
[[nodiscard]] std::optional<RelationCardinality> ParseRelationCardinality(std::string_view value);
[[nodiscard]] std::optional<ReferentialAction> ParseReferentialAction(std::string_view value);
[[nodiscard]] std::optional<TemporalRole> ParseTemporalRole(std::string_view value);
[[nodiscard]] std::optional<FieldDefaultKind> ParseFieldDefaultKind(std::string_view value);
[[nodiscard]] std::optional<BoundaryDefaultSource> ParseBoundaryDefaultSource(std::string_view value);
[[nodiscard]] std::optional<CheckKind> ParseCheckKind(std::string_view value);
[[nodiscard]] std::optional<CheckUnit> ParseCheckUnit(std::string_view value);
[[nodiscard]] std::optional<QueryOperator> ParseQueryOperator(std::string_view value);
[[nodiscard]] std::optional<SerializationPolicy> ParseSerializationPolicy(std::string_view value);
[[nodiscard]] std::optional<TemporalOrdering> ParseTemporalOrdering(std::string_view value);
[[nodiscard]] std::optional<OpenEndPolicy> ParseOpenEndPolicy(std::string_view value);
[[nodiscard]] std::optional<WirePrimitiveKind> ParseWirePrimitiveKind(std::string_view value);
[[nodiscard]] std::optional<WireByteOrder> ParseWireByteOrder(std::string_view value);
[[nodiscard]] std::optional<StorageCodecKind> ParseStorageCodecKind(std::string_view value);
[[nodiscard]] std::optional<SqliteStorageClass> ParseStorageClass(std::string_view value);
[[nodiscard]] std::optional<SemanticTypeKind> ParseSemanticTypeKind(std::string_view value);
[[nodiscard]] std::optional<FieldKind> ParseFieldKind(std::string_view value);
[[nodiscard]] std::optional<NormalizationKind> ParseNormalization(std::string_view value);
}
