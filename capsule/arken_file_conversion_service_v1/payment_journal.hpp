#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace arken::file_conversion::service::v1::payment {
inline constexpr std::size_t journalMaximumRows = 16;
inline constexpr std::size_t journalMaximumPacketBytes = 1048576;

// Private read-only actor protocol, not an authenticated public endpoint.
// scope is the opaque stored scope string or null; after is an exclusive
// operation/<digest> cursor. The caller must bind actorKey to the actual actor.
// Planning derives one lookahead read. Projection accepts the resulting ordered
// {key,value} entries and injected Unix seconds in now (canonical decimal text).
// No payment transition, storage mutation or external effect is performed.
// Historical tool and update-time metadata were not retained and remain null.
std::string JournalReadPlanPacket(std::string_view) noexcept;
std::string JournalProjectPacket(std::string_view) noexcept;
} // namespace arken::file_conversion::service::v1::payment
