#include "arken_image_codecs_v1/aotm_rgba.hpp"
#include <limits>

namespace arken::image::v1 {
AotmRgbaReadResult ReadAotmRgbaPackage(std::span<const std::uint8_t> bytes) noexcept {
  using Error=AotmRgbaError;
  const auto fail=[](Error error,std::string_view message){return AotmRgbaReadResult{error,message,{}};};
  if(bytes.size()<64)return fail(Error::truncatedInput,"AOTM package header is truncated");
  const auto read=[&](std::size_t at){return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);};
  if(read(0)!=0x4d544f41u||read(4)!=1)return fail(Error::unsupportedFormat,"AOTM package magic or version is unsupported");
  const auto header=read(8),payload=read(48);
  AotmRgbaPackageView value{read(12),read(16),read(20),read(24),read(28),read(32),read(36),read(40),read(44),{}};
  if(header<64||value.pixelOffset<header||payload!=value.pixelByteLength)
    return fail(Error::invalidArgument,"AOTM package payload bounds are invalid");
  if(value.pixelOffset>bytes.size()||payload>bytes.size()-value.pixelOffset)
    return fail(Error::truncatedInput,"AOTM package pixels are truncated");
  if(!value.width||!value.height||value.height>std::numeric_limits<std::uint32_t>::max()/4/value.width
    ||std::uint64_t(value.width)*value.height*4!=value.pixelByteLength)
    return fail(Error::invalidArgument,"AOTM dimensions do not match RGBA bytes");
  if(value.formatCode<1||value.formatCode>8||value.qualityClass<1||value.qualityClass>3||!value.sourceWidth||!value.sourceHeight)
    return fail(Error::invalidArgument,"AOTM source format, quality or dimensions are invalid");
  value.pixels=bytes.subspan(value.pixelOffset,payload);
  return {Error::none,{},value};
}
}
