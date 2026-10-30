#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/allocation.hpp"

namespace arken::schema::idl::v1::detail
{
[[nodiscard]] bool ValidateAllocationLedgerData(
    AllocationLedgerData& data,
    std::string_view source_name,
    const ParseLimits& limits,
    std::vector<Diagnostic>& diagnostics
);
[[nodiscard]] AllocationLedger CreateAllocationLedger(AllocationLedgerData data);
}
