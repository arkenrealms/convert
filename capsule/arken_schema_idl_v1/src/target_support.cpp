#include "arken_schema_idl_v1/semantic.hpp"

#include <algorithm>
#include <string>

#include "semantic_internal.hpp"

namespace arken::schema::idl::v1
{
TargetSupportResult ValidateTargetSupport(
    const SemanticIr& ir,
    TargetProfile profile,
    const ParseLimits& limits
)
{
    TargetSupportResult result;
    const bool known_profile = profile == TargetProfile::Cpp || profile == TargetProfile::Sqlite ||
        profile == TargetProfile::CSharp || profile == TargetProfile::JavaScript ||
        profile == TargetProfile::Fabric || profile == TargetProfile::HttpJson;
    if (limits.maximumDiagnostics == 0 || !known_profile)
    {
        result.diagnostics.push_back({
            .code = limits.maximumDiagnostics == 0 ? "idl.invalid_parse_limits" : "idl.target_profile_invalid",
            .severity = DiagnosticSeverity::Error,
            .message = limits.maximumDiagnostics == 0 ? "The diagnostic limit must be greater than zero."
                                                      : "Target support validation requires a known target profile.",
            .sourceName = "<target-support>",
        });
        return result;
    }
    for (const auto& type : ir.semanticTypes)
    {
        const bool mapped = std::find(type.targets.begin(), type.targets.end(), profile) != type.targets.end();
        if (!mapped)
        {
            if (result.diagnostics.size() >= limits.maximumDiagnostics) break;
            result.diagnostics.push_back({
                .code = "idl.target_mapping_unsupported",
                .severity = DiagnosticSeverity::Error,
                .message = "Semantic type " + type.qualifiedName + " has no " +
                    std::string(TargetProfileName(profile)) + " target mapping.",
                .sourceName = type.provenance.sourcePath,
                .span = type.provenance.span,
            });
            continue;
        }
        if (!detail::JsonContractValid(type.kind, type.jsonProfile, type.boundaryMapping))
        {
            if (result.diagnostics.size() >= limits.maximumDiagnostics) break;
            result.diagnostics.push_back({
                .code = "idl.target_json_contract_invalid",
                .severity = DiagnosticSeverity::Error,
                .message = "Semantic type " + type.qualifiedName +
                    " has a JSON profile or boundary mapping that is unsafe for target generation.",
                .sourceName = type.provenance.sourcePath,
                .span = type.provenance.span,
            });
            continue;
        }
        if (type.kind == SemanticTypeKind::JsonText &&
            !detail::TargetSupportsJsonProfile(profile, type.jsonProfile))
        {
            if (result.diagnostics.size() >= limits.maximumDiagnostics) break;
            result.diagnostics.push_back({
                .code = "idl.target_json_profile_unsupported",
                .severity = DiagnosticSeverity::Error,
                .message = "Semantic type " + type.qualifiedName + " requires " +
                    std::string(JsonProfileName(type.jsonProfile)) + " support that is not implemented for " +
                    std::string(TargetProfileName(profile)) + ".",
                .sourceName = type.provenance.sourcePath,
                .span = type.provenance.span,
            });
        }
    }
    return result;
}
}
