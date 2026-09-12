#pragma once
#include "payment.hpp"
#include <string>
#include <string_view>
namespace arken::file_conversion::service::payment {
// Private storage/Worker packet, not a public MCP command. All uint64 values
// are decimal text and all identifiers are fixed-width lowercase hex.
std::string InputPacket(const Operation&,const Quote&,const std::optional<NonceClaim>&,const Event&);
std::string DecisionPacket(const Decision&);
std::string AdvancePacket(std::string_view) noexcept;
}
