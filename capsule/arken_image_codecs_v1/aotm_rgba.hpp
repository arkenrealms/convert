#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace arken::image::v1 {
enum class AotmRgbaError { none, invalidArgument, unsupportedFormat, truncatedInput };
struct AotmRgbaPackageView {
  std::uint32_t width{},height{},pixelByteLength{},formatCode{},sourceWidth{},sourceHeight{},qualityClass{},flags{},pixelOffset{};
  std::span<const std::uint8_t> pixels;
};
struct AotmRgbaReadResult {
  AotmRgbaError error{AotmRgbaError::invalidArgument};
  std::string_view message;
  AotmRgbaPackageView value;
  explicit operator bool() const noexcept{return error==AotmRgbaError::none;}
};
// The AOTM v1 raw RGBA package, distinct from an AO chunk container. Borrowed
// pixels retain their source storage. This validates without decoding/copying.
[[nodiscard]] AotmRgbaReadResult ReadAotmRgbaPackage(std::span<const std::uint8_t> bytes) noexcept;
}
