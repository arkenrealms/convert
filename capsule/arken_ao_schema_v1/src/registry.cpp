#include "arken/ao/schema/v1/registry.hpp"

#include <array>

namespace arken::ao::schema::v1
{
namespace
{
constexpr std::array<AoFormatDescriptor, 2u> descriptors{{
    {aogm_format_id, {1u, 0u, 0u}, "aogm"},
    {aotm_format_id, {1u, 0u, 0u}, "aotm"},
}};
}

AoFormatLookup AoSchemaRegistry::find_ao_format(AoFormatId id) const noexcept
{
    for(auto const& descriptor : descriptors)
    {
        if(descriptor.format_id == id)
            return {&descriptor};
    }
    return {};
}

AoSchemaRegistry bootstrap_registry() noexcept
{
    return {};
}
}
