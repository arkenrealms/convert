#pragma once
#include "arken/digest/v1/digest.hpp"

namespace arken::digest::v1 {
inline constexpr std::size_t source_digest_chunk_bytes = 65536;
enum class SourceDigestStatus : std::uint8_t {
    complete, unavailable, cancelled, changed, invalid_read, limit_exceeded
};
struct SourceDigestRead {
    SourceDigestStatus status = SourceDigestStatus::unavailable;
    std::size_t bytes = 0;
};
// The caller owns one immutable source snapshot. No path, allocation or I/O
// authority is retained here; cancellation/change must reject in its reader.
struct DigestSource {
    std::uint64_t byte_length = 0;
    void* context = nullptr;
    SourceDigestRead (*read)(void*, std::uint64_t, std::span<std::byte>) noexcept = nullptr;
};
struct SourceDigestResult {
    SourceDigestStatus status = SourceDigestStatus::unavailable;
    std::optional<Sha256ContentAddress> address;
    std::uint64_t bytes_read = 0;
};
[[nodiscard]] SourceDigestResult sha256_source(
    DigestSource const& source, std::uint64_t maximum_bytes) noexcept;
}
