#include "arken_schema_idl_v1/semantic.hpp"

#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

#include "semantic_internal.hpp"

namespace arken::schema::idl::v1
{
std::string_view JsonProfileName(JsonProfile profile)
{
    switch (profile)
    {
    case JsonProfile::None: return "none";
    case JsonProfile::Rfc8259Preserve: return "rfc8259_preserve";
    case JsonProfile::Rfc8785Canonical: return "rfc8785_canonical";
    }
    return "unknown";
}

std::string_view BoundaryMappingPolicyName(BoundaryMappingPolicy policy)
{
    switch (policy)
    {
    case BoundaryMappingPolicy::Scalar: return "scalar";
    case BoundaryMappingPolicy::Opaque: return "opaque";
    }
    return "unknown";
}

namespace detail
{
std::optional<JsonProfile> ParseJsonProfile(std::string_view value)
{
    if (value == "none") return JsonProfile::None;
    if (value == "rfc8259_preserve") return JsonProfile::Rfc8259Preserve;
    if (value == "rfc8785_canonical") return JsonProfile::Rfc8785Canonical;
    return std::nullopt;
}

std::optional<BoundaryMappingPolicy> ParseBoundaryMappingPolicy(std::string_view value)
{
    if (value == "scalar") return BoundaryMappingPolicy::Scalar;
    if (value == "opaque") return BoundaryMappingPolicy::Opaque;
    return std::nullopt;
}

bool JsonContractValid(
    SemanticTypeKind kind,
    JsonProfile profile,
    BoundaryMappingPolicy mapping
)
{
    if (kind == SemanticTypeKind::JsonText)
        return profile != JsonProfile::None && mapping == BoundaryMappingPolicy::Opaque;
    return profile == JsonProfile::None && mapping == BoundaryMappingPolicy::Scalar;
}

bool JsonLiteralValid(std::string_view value, JsonProfile profile)
{
    if (profile != JsonProfile::Rfc8259Preserve) return false;
    const auto parsed = nlohmann::json::parse(value.begin(), value.end(), nullptr, false, false);
    return !parsed.is_discarded();
}

bool TargetSupportsJsonProfile(TargetProfile target, JsonProfile profile)
{
    const bool known_target = target == TargetProfile::Cpp || target == TargetProfile::Sqlite ||
        target == TargetProfile::CSharp || target == TargetProfile::JavaScript ||
        target == TargetProfile::Fabric || target == TargetProfile::HttpJson;
    return known_target && profile != JsonProfile::Rfc8785Canonical;
}
}
}
