#pragma once

#include <optional>
#include <string_view>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::idl::v1::detail
{
[[nodiscard]] std::optional<JsonProfile> ParseJsonProfile(std::string_view value);
[[nodiscard]] std::optional<BoundaryMappingPolicy> ParseBoundaryMappingPolicy(std::string_view value);
[[nodiscard]] bool JsonContractValid(
    SemanticTypeKind kind,
    JsonProfile profile,
    BoundaryMappingPolicy mapping
);
[[nodiscard]] bool JsonLiteralValid(std::string_view value, JsonProfile profile);
[[nodiscard]] bool TargetSupportsJsonProfile(TargetProfile target, JsonProfile profile);
[[nodiscard]] SemanticManifest CreateSemanticManifest(SemanticIr ir);
}
