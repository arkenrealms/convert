#include "arken/digest/v1/source_digest.hpp"
#include <algorithm>
#include <limits>

namespace arken::digest::v1 {
SourceDigestResult sha256_source(DigestSource const& source, std::uint64_t maximum_bytes) noexcept {
    SourceDigestResult result;
    if (source.byte_length > maximum_bytes ||
        source.byte_length > std::numeric_limits<std::uint64_t>::max() / 8) {
        result.status = SourceDigestStatus::limit_exceeded;
        return result;
    }
    if (!source.read && source.byte_length) return result;
    Sha256Hasher hasher;
    std::array<std::byte, source_digest_chunk_bytes> buffer;
    while (result.bytes_read < source.byte_length) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            buffer.size(), source.byte_length - result.bytes_read));
        const auto destination = std::span(buffer).first(count);
        const auto read = source.read(source.context, result.bytes_read, destination);
        if (read.status != SourceDigestStatus::complete) {
            result.status = read.status;
            return result;
        }
        if (read.bytes != count) {
            result.status = SourceDigestStatus::invalid_read;
            return result;
        }
        if (!hasher.update(destination)) {
            result.status = SourceDigestStatus::limit_exceeded;
            return result;
        }
        result.bytes_read += count;
    }
    const auto digest = hasher.finalize();
    if (!digest) { result.status = SourceDigestStatus::limit_exceeded; return result; }
    result.status = SourceDigestStatus::complete;
    result.address = Sha256ContentAddress{*digest, source.byte_length};
    return result;
}
}
