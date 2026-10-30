#pragma once

#include <string>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::cpp::v1::generator_detail
{
[[nodiscard]] std::string GeneratedWireByteOrderName(
    ::arken::schema::idl::v1::WireByteOrder order
);
[[nodiscard]] std::string GeneratedWireCompatibilityClassName(
    ::arken::schema::idl::v1::WireCompatibilityClass compatibility
);
[[nodiscard]] std::string GeneratedFieldCardinalityName(
    ::arken::schema::idl::v1::FieldCardinality cardinality
);
[[nodiscard]] std::string GeneratedFabricLaneName(
    ::arken::schema::idl::v1::FabricLane lane
);
[[nodiscard]] std::string GeneratedCommandIdempotencyName(
    ::arken::schema::idl::v1::CommandIdempotency idempotency
);
[[nodiscard]] std::string GeneratedCommandStreamingName(
    ::arken::schema::idl::v1::CommandStreaming streaming
);
[[nodiscard]] std::string GeneratedEventDeliveryName(
    ::arken::schema::idl::v1::EventDelivery delivery
);
[[nodiscard]] std::string GeneratedEventOrderingName(
    ::arken::schema::idl::v1::EventOrdering ordering
);
[[nodiscard]] std::string GeneratedEventReplayName(
    ::arken::schema::idl::v1::EventReplay replay
);
}
