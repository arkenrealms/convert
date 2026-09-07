#pragma once
#include "arken/ao/core/v1/core.hpp"
#include <span>

namespace arken::ao::core::v1 {
// Directory-only admission for immutable range sources. Payload bytes are
// deliberately absent; verify_chunk must succeed before a slice is decoded.
struct ChunkRange {
    uint32_t type = 0, element_count = 0;
    uint64_t offset = 0, byte_length = 0, hash64 = 0;
};
struct Directory {
    Status status = Status::ok;
    std::string message;
    FileKind kind = FileKind::aodb;
    uint64_t required_bytes = kHeaderSize;
    std::vector<ChunkRange> chunks;
    explicit operator bool() const noexcept { return status == Status::ok; }
};
Directory read_directory(std::span<uint8_t const> prefix, uint64_t file_size,
                         FileKind expected_kind) noexcept;
bool verify_chunk(ChunkRange const &, std::span<uint8_t const>) noexcept;
}
