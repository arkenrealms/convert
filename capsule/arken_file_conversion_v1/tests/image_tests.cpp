#include "arken_file_conversion_v1/api.hpp"
#include "arken_image_codecs_v1/test_support/texture_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/raster_fixtures.hpp"
#include "expansion_fixture.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace c = arken::file_conversion::v1;
namespace image = arken::image::v1;
namespace {
using Json = nlohmann::json;
void Check(bool passed, std::string_view message) { if (!passed) throw std::runtime_error(std::string(message)); }
std::string Hex(std::span<const std::byte> bytes) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string text(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto value = std::to_integer<unsigned>(bytes[i]);
        text[i*2] = alphabet[value >> 4]; text[i*2+1] = alphabet[value & 15];
    }
    return text;
}
Json Cases() {
    Json cases = Json::array();
    const auto add = [&](std::string id, std::vector<std::byte> source, c::ImageOptions options,
                         c::Error expected = c::Error::none) {
        const auto start = std::chrono::steady_clock::now();
        auto result = c::ConvertImage(source, options);
        Check(result.error == expected, id + ": wrong disposition");
        if (result) {
            const auto decoded = image::DecodeImage(options.output, result.bytes);
            Check(bool(decoded) && decoded.image.width == result.width && decoded.image.height == result.height,
                id + ": encoded output cannot be opened");
            Check(c::ConvertImage(source, options).bytes == result.bytes, id + ": repeated output changed");
            if (options.output != image::Format::jpeg)
                Check(decoded.image.rgba8 == image::DecodeImage(options.input, source).image.rgba8,
                    id + ": lossless conversion changed pixels");
        } else Check(result.bytes.empty() && !result.width && !result.height, id + ": failed output leaked");
        const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        cases.push_back({{"id",id},{"input",Hex(source)},{"from",options.input},{"to",options.output},
            {"quality",options.jpegQuality},{"alpha",options.alphaPolicy},{"error",result.error},
            {"decodeError",result.decodeError},{"encodeError",result.encodeError},
            {"width",result.width},{"height",result.height},{"alphaFlattened",result.alphaFlattened},
            {"output",Hex(result.bytes)},{"nativeValidationMs",ms}});
    };
    for (auto input : {image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga}) {
        auto source = input == image::Format::tga ? texture_fixture::Tga(13,7,true)
            : raster_fixture::Encode(input,13,7,input != image::Format::jpeg);
        for (auto output : {image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga}) {
            c::ImageOptions options{input,output};
            if (output == image::Format::jpeg) options.alphaPolicy = image::AlphaPolicy::flatten_white;
            add("raster-"+std::to_string(unsigned(input))+"-"+std::to_string(unsigned(output)),source,options);
        }
        source.pop_back();
        add("truncated-"+std::to_string(unsigned(input)),source,{input,image::Format::png},c::Error::decode_failed);
    }
    for (unsigned compression : {1u,3u,5u}) {
        for (auto output : {image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga})
            add("dds-bc-"+std::to_string(compression)+"-"+std::to_string(unsigned(output)),
                texture_fixture::Dds(8,4,1,compression),{image::Format::dds,output});
    }
    auto jpeg = raster_fixture::Encode(image::Format::jpeg,13,7);
    for (unsigned orientation=1; orientation<=8; ++orientation)
        add("exif-"+std::to_string(orientation),raster_fixture::WithOrientation(jpeg,orientation),
            {image::Format::jpeg,image::Format::png});
    add("exif-invalid",raster_fixture::WithOrientation(jpeg,9),{image::Format::jpeg,image::Format::png},c::Error::decode_failed);
    auto png = raster_fixture::Encode(image::Format::png,13,7,true);
    add("alpha-consent-required",png,{image::Format::png,image::Format::jpeg},c::Error::encode_failed);
    add("invalid-matte",png,{image::Format::png,image::Format::png,95,image::AlphaPolicy::flatten_white},c::Error::invalid_options);
    add("invalid-quality",png,{image::Format::png,image::Format::jpeg,101},c::Error::invalid_options);
    add("irrelevant-quality",png,{image::Format::png,image::Format::png,50},c::Error::invalid_options);
    add("unsupported-input",png,{image::Format::blp,image::Format::png},c::Error::input_format_unavailable);
    add("unsupported-output",png,{image::Format::png,image::Format::dds},c::Error::output_format_unavailable);
    add("format-mismatch",png,{image::Format::jpeg,image::Format::png},c::Error::decode_failed);
    add("custom-color-profile",raster_fixture::WithChromaticities(png,false),{},c::Error::decode_failed);
    png.back() ^= std::byte{1}; add("crc-corrupt",png,{},c::Error::decode_failed);
    add("empty",{}, {}, c::Error::input_limit);
    add("pixel-limit",raster_fixture::Encode(image::Format::png,1025,1024),{},c::Error::decode_failed);
    add("dimension-limit",raster_fixture::Encode(image::Format::png,4097,1),{},c::Error::decode_failed);
    add("compressed-expansion-limit",conversion_fixture::ExpansionBomb(),{},c::Error::decode_failed);
    // Deterministic high-entropy maximum-sized input, with no donor/user assets.
    std::vector<std::uint8_t> pixels(c::maximumPixels * 4);
    std::uint32_t seed=123456789;
    for (std::size_t i=0; i<pixels.size(); ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        pixels[i] = i%4==3 ? 255 : std::uint8_t(seed);
    }
    auto large = image::EncodeImage(image::Format::png,1024,1024,pixels);
    Check(bool(large),"large fixture encode failed");
    add("one-megapixel-entropy",std::move(large.bytes),{});
    std::vector<std::byte> over(c::maximumSourceBytes+1);
    Check(c::ConvertImage(over).error==c::Error::input_limit,"oversized input accepted");
    return cases;
}
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--version") {
        std::cout<<"arken-file-conversion-tests-r4327\n"; return 0;
    }
    try {
        Check(argc==1 || (argc==3 && std::string_view(argv[1])=="--write-cases"),
            "Usage: arken-file-conversion-tests [--write-cases OUTPUT_JSON]");
        const auto cases=Cases();
        if(argc==3) {
            std::ofstream output(argv[2],std::ios::binary);
            Check(bool(output),"cannot open case output");
            output<<cases.dump(); Check(bool(output),"cannot write cases");
        }
        std::cout<<cases.size()<<" native conversion cases passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
