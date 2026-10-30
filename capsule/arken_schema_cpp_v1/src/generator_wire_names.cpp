#include "generator_wire_names.hpp"

namespace arken::schema::cpp::v1::generator_detail
{
namespace idl = ::arken::schema::idl::v1;

std::string GeneratedWireByteOrderName(idl::WireByteOrder order)
{
    switch (order)
    {
        case idl::WireByteOrder::None: return "none";
        case idl::WireByteOrder::BigEndian: return "big_endian";
    }
    return "none";
}

std::string GeneratedWireCompatibilityClassName(idl::WireCompatibilityClass compatibility)
{
    switch (compatibility)
    {
        case idl::WireCompatibilityClass::Boolean: return "boolean";
        case idl::WireCompatibilityClass::FixedUnsigned: return "fixed_unsigned";
        case idl::WireCompatibilityClass::FixedSigned: return "fixed_signed";
        case idl::WireCompatibilityClass::Ieee754: return "ieee754";
        case idl::WireCompatibilityClass::Utf8: return "utf8";
        case idl::WireCompatibilityClass::Bytes: return "bytes";
        case idl::WireCompatibilityClass::FixedBytes: return "fixed_bytes";
        case idl::WireCompatibilityClass::Decimal128: return "decimal128";
    }
    return "bytes";
}

std::string GeneratedFieldCardinalityName(idl::FieldCardinality cardinality)
{
    switch (cardinality)
    {
        case idl::FieldCardinality::RequiredOne: return "required_one";
        case idl::FieldCardinality::OptionalOne: return "optional_one";
        case idl::FieldCardinality::Repeated: return "repeated";
    }
    return "required_one";
}

std::string GeneratedFabricLaneName(idl::FabricLane lane)
{
    switch (lane)
    {
        case idl::FabricLane::ControlReliable: return "control_reliable";
        case idl::FabricLane::StateReliable: return "state_reliable";
        case idl::FabricLane::BulkReliable: return "bulk_reliable";
        case idl::FabricLane::RealtimeUnreliable: return "realtime_unreliable";
        case idl::FabricLane::Compatibility: return "compatibility";
    }
    return "control_reliable";
}

std::string GeneratedCommandIdempotencyName(idl::CommandIdempotency idempotency)
{
    switch (idempotency)
    {
        case idl::CommandIdempotency::None: return "none";
        case idl::CommandIdempotency::Idempotent: return "idempotent";
        case idl::CommandIdempotency::Safe: return "safe";
    }
    return "none";
}

std::string GeneratedCommandStreamingName(idl::CommandStreaming streaming)
{
    switch (streaming)
    {
        case idl::CommandStreaming::Unary: return "unary";
        case idl::CommandStreaming::ServerStream: return "server_stream";
        case idl::CommandStreaming::ClientStream: return "client_stream";
        case idl::CommandStreaming::Bidirectional: return "bidirectional";
    }
    return "unary";
}

std::string GeneratedEventDeliveryName(idl::EventDelivery delivery)
{
    switch (delivery)
    {
        case idl::EventDelivery::BestEffort: return "best_effort";
        case idl::EventDelivery::AtLeastOnce: return "at_least_once";
    }
    return "best_effort";
}

std::string GeneratedEventOrderingName(idl::EventOrdering ordering)
{
    switch (ordering)
    {
        case idl::EventOrdering::None: return "none";
        case idl::EventOrdering::Source: return "source";
        case idl::EventOrdering::Aggregate: return "aggregate";
        case idl::EventOrdering::Global: return "global";
    }
    return "none";
}

std::string GeneratedEventReplayName(idl::EventReplay replay)
{
    switch (replay)
    {
        case idl::EventReplay::None: return "none";
        case idl::EventReplay::Retained: return "retained";
        case idl::EventReplay::Resumable: return "resumable";
    }
    return "none";
}
}
