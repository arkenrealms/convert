#pragma once
#include "api.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace arken::image::v1 {
struct SourceFormat {
    std::string_view extension;
    Format format;
    bool firstFrameOnly = false;
    bool browserPreview = false;
};
// Filename eligibility only; DecodeImage remains the byte-validation authority.
inline constexpr std::array<SourceFormat,15> sourceFormats = {{
    {"pcx",Format::pcx}, {"cel",Format::cel}, {"dc6",Format::dc6},
    {"blp",Format::blp}, {"dds",Format::dds}, {"tga",Format::tga},
    {"png",Format::png,false,true}, {"bmp",Format::bmp,false,true},
    {"jpg",Format::jpeg,false,true}, {"jpeg",Format::jpeg,false,true},
    {"gif",Format::gif,true,false}, {"ico",Format::ico,false,true},
    {"tif",Format::tiff,false,false}, {"tiff",Format::tiff,false,false},
    {"webp",Format::webp,false,false}
}};
// Presentation-only formats are not admitted to DecodeImage or conversion.
inline constexpr std::array<std::string_view,2> browserPreviewOnlyExtensions = {"avif","svg"};
[[nodiscard]] constexpr const SourceFormat* FindSourceFormat(std::string_view extension) noexcept {
    for (const auto& entry : sourceFormats) {
        if (entry.extension.size() != extension.size()) continue;
        bool equal = true;
        for (std::size_t i=0;i<extension.size();++i) {
            const char c=extension[i];
            if (entry.extension[i] != (c>='A' && c<='Z' ? c+('a'-'A') : c)) {equal=false;break;}
        }
        if (equal) return &entry;
    }
    return nullptr;
}
}
