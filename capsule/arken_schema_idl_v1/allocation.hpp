#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::idl::v1
{
struct ActiveAllocation
{
    std::uint32_t id = 0;
    std::string canonicalName;
    std::string ownerCapsule;
    SemanticProvenance provenance;
};

struct RetiredIdAllocation
{
    std::uint32_t id = 0;
    std::string ownerCapsule;
    SemanticProvenance provenance;
};

struct RetiredNameAllocation
{
    std::string canonicalName;
    std::string ownerCapsule;
    SemanticProvenance provenance;
};

struct AllocationRegistry
{
    RegistryScope scope;
    std::vector<ActiveAllocation> active;
    std::vector<RetiredIdAllocation> retiredIds;
    std::vector<RetiredNameAllocation> retiredNames;
};

struct AllocationLedgerData
{
    std::uint32_t formatVersion = 2;
    std::vector<AllocationRegistry> registries;
};

struct AllocationLedger
{
    AllocationLedgerData data;
    std::string canonicalJson;
    std::string allocationFingerprint;
};

struct AllocationLedgerResult
{
    std::optional<AllocationLedger> ledger;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

struct AllocationEvolutionResult
{
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

struct AllocationRange
{
    std::uint32_t first = 1;
    std::uint32_t last = std::numeric_limits<std::uint32_t>::max();
};

struct AllocationSuggestion
{
    RegistryScope scope;
    std::uint32_t value = 0;
};

struct AllocationSuggestionResult
{
    std::optional<AllocationSuggestion> suggestion;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] AllocationLedgerResult BuildAllocationLedger(
    const SemanticIr& ir,
    const ParseLimits& limits = {}
);
[[nodiscard]] AllocationLedgerResult ParseAllocationLedger(
    std::string_view source_name,
    std::string_view ledger_json,
    const ParseLimits& limits = {}
);
[[nodiscard]] AllocationEvolutionResult ValidateAllocationEvolution(
    const AllocationLedgerData& previous,
    const AllocationLedgerData& current,
    const ParseLimits& limits = {}
);
[[nodiscard]] AllocationSuggestionResult SuggestAllocationId(
    const AllocationLedgerData& ledger,
    RegistryScope scope,
    AllocationRange range = {}
);
}
