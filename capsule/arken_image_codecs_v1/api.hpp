#pragma once
#include "blp.hpp"
#include <string_view>

namespace arken::image::v1 {
enum class Format : std::uint32_t {pcx=1, cel=2, dc6=3, blp=4, dds=5, tga=6};
struct DecodeOptions {
    std::uint32_t frameIndex=0, directionIndex=0, mipIndex=0;
    std::uint32_t celWidth=0, celFrameHeaderBytes=0;
    std::span<const std::byte> palette;
    std::uint64_t maximumSourceBytes=32ull*1024*1024;
    std::uint64_t maximumPixels=8ull*1024*1024;
    std::uint32_t maximumDimension=8192;
};
struct Image {
    std::uint32_t width=0, height=0, frameCount=1, directionCount=1, mipCount=1;
    std::int32_t offsetX=0, offsetY=0;
    std::vector<std::uint8_t> rgba8;
    // BLP1's independently framed JPEG is a platform codec payload, never a
    // replacement document model. Consumers must handle this disposition explicitly.
    std::vector<std::byte> embeddedJpeg;
};
struct DecodeResult {
    DecodeIssue issue;
    Image image;
    explicit operator bool() const noexcept {return issue.code==DecodeError::none;}
};
[[nodiscard]] DecodeResult DecodeImage(Format format,std::span<const std::byte> bytes,
    const DecodeOptions& options={}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(DecodeError error) noexcept;
}
