#pragma once
#include "arken_image_codecs_v1/api.hpp"

namespace scalar_fixture {
// Two rows deliberately cross byte boundaries and decrease; predictor state
// must reset per row, and big-endian source must retain the low eight bits.
inline std::vector<std::byte> Tiff16(bool little,bool predicted){
  std::vector<std::byte> b;
  const auto append=[&](std::uint32_t n,unsigned size){for(unsigned i=0;i<size;++i)
    b.push_back(std::byte((n>>(8*(little?i:size-1-i)))&255));};
  append(little?0x4949:0x4d4d,2);append(42,2);append(8,4);append(11,2);
  const auto tag=[&](unsigned id,unsigned value){append(id,2);append(4,2);append(1,4);append(value,4);};
  tag(256,3);tag(257,2);tag(258,16);tag(259,1);tag(262,1);tag(273,8+2+11*12+4);
  tag(274,1);tag(277,1);tag(278,2);tag(279,12);tag(317,predicted?2:1);append(0,4);
  const std::uint16_t values[]{1,257,65535,32768,10,65534};
  for(unsigned i=0;i<6;++i)append(std::uint16_t(values[i]-(predicted&&i%3?values[i-1]:0)),2);
  return b;
}
}
