#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::idl::v1
{
enum class CompatibilitySubjectKind : std::uint8_t
{
    Source,
    WireType,
    StorageCodec,
    SemanticType,
    Enum,
    EnumValue,
    Model,
    Field,
    Key,
    Index,
    Relation,
    Join,
    OneOf,
    TemporalContract,
    Reservation,
};

enum class CompatibilityChangeKind : std::uint8_t
{
    Addition,
    Removal,
    Rename,
    ReservationAdded,
    ReservationRemoved,
    TypeChanged,
    PresenceChanged,
    RelationChanged,
    KeyChanged,
    CodecChanged,
    StorageChanged,
    OwnershipChanged,
    TargetChanged,
    PolicyChanged,
    ContractChanged,
};

enum class CompatibilityDomain : std::uint8_t
{
    Source,
    Semantic,
    Wire,
    Storage,
    CppApi,
    CSharpApi,
    JavaScriptApi,
};

enum class CompatibilityDisposition : std::uint8_t
{
    Unaffected,
    Compatible,
    MigrationRequired,
    Breaking,
};

struct CompatibilityEffects
{
    CompatibilityDisposition source = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition semantic = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition wire = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition storage = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition cppApi = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition csharpApi = CompatibilityDisposition::Unaffected;
    CompatibilityDisposition javaScriptApi = CompatibilityDisposition::Unaffected;
    bool dataRebuildRequired = false;

    [[nodiscard]] CompatibilityDisposition disposition(CompatibilityDomain domain) const;
};

struct CompatibilitySubject
{
    CompatibilitySubjectKind kind = CompatibilitySubjectKind::Source;
    std::optional<RegistryScope> registryScope;
    std::optional<std::uint32_t> parentNumericId;
    std::optional<std::uint32_t> numericId;
    std::string previousName;
    std::string currentName;
};

struct SemanticCompatibilityChange
{
    CompatibilityChangeKind kind = CompatibilityChangeKind::ContractChanged;
    CompatibilitySubject subject;
    CompatibilityEffects effects;
    Diagnostic diagnostic;
};

struct SemanticCompatibilityReport
{
    std::string previousSemanticFingerprint;
    std::string currentSemanticFingerprint;
    std::vector<SemanticCompatibilityChange> changes;
    std::string canonicalJson;

    [[nodiscard]] bool hasBreakingChanges() const;
    [[nodiscard]] bool requiresStorageMigration() const;
    [[nodiscard]] bool requiresDataRebuild() const;
};

[[nodiscard]] std::string_view CompatibilitySubjectKindName(CompatibilitySubjectKind kind);
[[nodiscard]] std::string_view CompatibilityChangeKindName(CompatibilityChangeKind kind);
[[nodiscard]] std::string_view CompatibilityDomainName(CompatibilityDomain domain);
[[nodiscard]] std::string_view CompatibilityDispositionName(CompatibilityDisposition disposition);
[[nodiscard]] SemanticCompatibilityReport CompareSemanticManifests(const SemanticManifest& previous,
                                                                   const SemanticManifest& current);
}
