#include "arken/ao/core/v1/directory.hpp"
#include <algorithm>
#include <set>

namespace arken::ao::core::v1 {
namespace {
uint64_t integer(std::span<uint8_t const> b, size_t p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint64_t(b[p + i]) << (i * 8);
    return v;
}
Directory fail(Status s, char const *message, uint64_t required = kHeaderSize) {
    Directory r; r.status = s; r.message = message; r.required_bytes = required; return r;
}
}
Directory read_directory(std::span<uint8_t const> bytes, uint64_t size,
                         FileKind kind) noexcept {
    try {
        if (bytes.size() < kHeaderSize || size < kHeaderSize)
            return fail(Status::truncated, "AO header is truncated");
        if (integer(bytes, 0, 4) != uint32_t(kind))
            return fail(Status::invalid_magic, "AO format does not match the declared payload");
        if (integer(bytes, 4, 2) != kContainerVersion || integer(bytes, 6, 2) != kHeaderSize)
            return fail(Status::unsupported_version, "Unsupported AO directory version");
        if (integer(bytes, 8, 4) != kEndianMarker)
            return fail(Status::unsupported_endian, "Unsupported AO byte order");
        const auto offset = integer(bytes, 16, 8), count = integer(bytes, 24, 4);
        constexpr uint64_t maximum_directory_bytes = 4 * 1024 * 1024;
        if (offset < kHeaderSize || offset > size || count > maximum_directory_bytes / kChunkRecordSize
            || count * kChunkRecordSize > size - offset || offset > maximum_directory_bytes
            || count * kChunkRecordSize > maximum_directory_bytes - offset)
            return fail(Status::invalid_chunk_directory, "AO directory exceeds its file or metadata bound");
        const auto end = offset + count * kChunkRecordSize;
        if (bytes.size() < end) return fail(Status::truncated, "AO directory needs additional range bytes", end);
        Directory out; out.kind = kind; out.required_bytes = end;
        std::set<uint32_t> types;
        for (uint64_t i = 0; i < count; ++i) {
            const auto p = size_t(offset + i * kChunkRecordSize);
            ChunkRange row{uint32_t(integer(bytes,p,4)),uint32_t(integer(bytes,p+16,4)),
                integer(bytes,p+24,8),integer(bytes,p+40,8),integer(bytes,p+48,8)};
            if (!types.insert(row.type).second)
                return fail(Status::invalid_chunk_directory, "AO directory contains duplicate chunk types");
            if (integer(bytes,p+8,4) != kCompressionNone || integer(bytes,p+32,8) != row.byte_length)
                return fail(Status::unsupported_compression, "AO ranged decoding requires uncompressed chunks");
            if (row.offset < end || row.offset > size || row.byte_length > size - row.offset)
                return fail(Status::invalid_chunk_directory, "AO chunk is outside its admitted file");
            out.chunks.push_back(row);
        }
        auto ordered = out.chunks;
        std::sort(ordered.begin(),ordered.end(),[](auto const &a,auto const &b){return a.offset < b.offset || (a.offset==b.offset && a.byte_length<b.byte_length);});
        for (size_t i=1;i<ordered.size();++i)
            if (ordered[i].offset < ordered[i-1].offset + ordered[i-1].byte_length)
                return fail(Status::invalid_chunk_directory,"AO chunk ranges overlap");
        return out;
    } catch (...) { return fail(Status::invalid_argument,"AO directory allocation failed"); }
}
bool verify_chunk(ChunkRange const &row, std::span<uint8_t const> bytes) noexcept {
    return bytes.size() == row.byte_length && fnv1a64(bytes.data(),bytes.size()) == row.hash64;
}
}
