#include "arken_image_codecs_v1/encode.hpp"
#include <iostream>
#include <stdexcept>

namespace image=arken::image::v1;
void check(bool value,char const* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){
  if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-atlas-png-tests-r4214\n";return 0;}
  try {
    check(argc==1,"unexpected arguments");constexpr unsigned side=4096;
    std::vector<std::uint8_t> rgba(std::size_t(side)*side*4);
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto at=(std::size_t(y)*side+x)*4;
      rgba[at]=static_cast<std::uint8_t>(x);rgba[at+1]=static_cast<std::uint8_t>(y);
      rgba[at+2]=static_cast<std::uint8_t>((x>>8)^(y>>8));rgba[at+3]=static_cast<std::uint8_t>(x+y);}
    check(image::EncodeImage(image::Format::png,side,side,rgba).error==image::EncodeError::invalid_dimensions,"default encode budget widened");
    image::EncodeOptions options;options.maximumPixels=16*1024*1024;options.maximumOutputBytes=8*1024*1024;
    const auto encoded=image::EncodeImage(image::Format::png,side,side,rgba,options);
    if(!encoded)throw std::runtime_error(std::string(image::EncodeErrorMessage(encoded.error)));
    check(!encoded.alphaFlattened&&encoded.bytes.size()<8*1024*1024,"PNG alpha or output budget changed");
    check(!image::DecodeImage(image::Format::png,encoded.bytes),"default decode budget widened");
    image::DecodeOptions decode;decode.maximumPixels=16*1024*1024;
    const auto decoded=image::DecodeImage(image::Format::png,encoded.bytes,decode);
    if(!decoded)throw std::runtime_error(std::string(image::ErrorMessage(decoded.issue.code)));
    check(decoded.image.width==side&&decoded.image.height==side&&decoded.image.rgba8==rgba,"full-size RGBA roundtrip changed pixels or orientation");
    check(!image::EncodeImage(image::Format::png,side,side+1,rgba,options),"pixel budget exceeded");
    options.maximumPixels++;check(image::EncodeImage(image::Format::png,side,side,rgba,options).error==image::EncodeError::invalid_options,"unsupported budget accepted");
    options.maximumPixels--;options.maximumOutputBytes=1;
    const auto limited=image::EncodeImage(image::Format::png,1,1,std::span(rgba).first(4),options);
    check(limited.error==image::EncodeError::output_limit&&limited.bytes.empty(),"partial output escaped");
    options.maximumOutputBytes=8*1024*1024;
    for(auto format:{image::Format::bmp,image::Format::jpeg,image::Format::tga})
      check(image::EncodeImage(format,1,1,std::span(rgba).first(4),options).error==image::EncodeError::invalid_options,"other format budget widened");
    std::cout<<"passed: exact 4096x4096 RGBA/alpha PNG roundtrip, explicit opt-in, preserved defaults, rejected overflow and partial output; pngBytes="<<encoded.bytes.size()<<'\n';
  }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
