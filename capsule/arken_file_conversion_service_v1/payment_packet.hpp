#pragma once
#include "payment.hpp"
#include <string>
#include <string_view>
namespace arken::file_conversion::service::v1::payment {
// Private storage/Worker packet, not a public MCP command. All uint64 values
// are decimal text and all identifiers are fixed-width lowercase hex.
std::string InputPacket(const Operation&,const Quote&,const std::optional<NonceClaim>&,const Event&);
std::string DecisionPacket(const Decision&);
std::string AdvancePacket(std::string_view) noexcept;
// Storage records and commands are opaque JSON strings. C++ derives every
// key/write and validates the durable actor scope before admitting a change.
std::string StorageLookupPacket(std::string_view) noexcept;
std::string StorageAdvancePacket(std::string_view) noexcept;
}
