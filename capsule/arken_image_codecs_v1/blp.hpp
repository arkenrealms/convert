#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arken::image::v1 {
enum class DecodeError {
    none, input_too_large, truncated, trailing_bytes, wrong_magic,
    unsupported_content, unsupported_compression, unsupported_alpha_depth,
    invalid_dimensions, invalid_layout, resource_limit, integer_overflow,
    allocation_failed, palette_required, invalid_palette, width_required,
    selection_out_of_range, invalid_run, embedded_jpeg
};
struct DecodeIssue {
    DecodeError code = DecodeError::none;
    std::uint64_t byte_offset = 0;
};
enum class Blp2Compression : std::uint8_t { palette=1, dxt=2, bgra8=3 };
struct Blp2Metadata {
    Blp2Compression compression = Blp2Compression::palette;
    std::uint8_t source_alpha_depth=0, alpha_depth=0, alpha_type=0;
    bool has_mipmaps=false;
    std::uint32_t width=0, height=0, authored_mip_count=0, legacy_packed_dxt_mip_count=0;
};
struct Blp2MipLevel {
    std::uint32_t level=0, width=0, height=0, source_offset=0, source_byte_length=0;
    bool decoded=true, legacy_packed_dxt=false;
    std::vector<std::uint8_t> rgba8;
};
struct DecodedBlp2 {
    Blp2Metadata metadata;
    std::vector<Blp2MipLevel> mips;
};
struct Blp2DecodeResult {
    DecodeIssue issue;
    DecodedBlp2 image;
    explicit operator bool() const noexcept {return issue.code==DecodeError::none;}
};
// Portable codec only: no MPQ, Warcraft state, filesystem or browser dependency.
[[nodiscard]] Blp2DecodeResult DecodeBlp2(std::span<const std::byte> bytes,
    std::uint64_t maximumBytes=512ull*1024*1024);
}
