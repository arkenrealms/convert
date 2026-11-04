#include "reader.hpp"
#include <array>

namespace arken::image::v1::detail {
// ZSoft PCX framing/plane rules; independently bounded implementation.
// Reference inventory: DentonW/DevIL src-IL/src/il_pcx.cpp (no code vendored).
DecodeResult DecodePcx(Bytes b,const DecodeOptions& o){
    if(b.size()<128)return Failure(DecodeError::truncated);
    if(U8(b,0)!=10)return Failure(DecodeError::wrong_magic);
    const auto version=U8(b,1);
    if(version!=0&&version!=2&&version!=3&&version!=4&&version!=5)return Failure(DecodeError::unsupported_content,1);
    const auto bits=U8(b,3),planes=U8(b,65),encoding=U8(b,2);
    if(encoding>1||(bits!=1&&bits!=2&&bits!=4&&bits!=8)||planes<1||planes>4)
        return Failure(DecodeError::unsupported_content,2);
    if(U16(b,8)<U16(b,4)||U16(b,10)<U16(b,6))return Failure(DecodeError::invalid_dimensions,4);
    DecodeResult r;auto& image=r.image;
    image.width=std::uint32_t(U16(b,8))-U16(b,4)+1;image.height=std::uint32_t(U16(b,10))-U16(b,6)+1;
    if(!Dimensions(image.width,image.height,o))return Failure(DecodeError::invalid_dimensions,4);
    const bool trueColor=bits==8&&(planes==3||planes==4);
    const unsigned colorBits=bits*planes;
    if(!trueColor&&colorBits>8)return Failure(DecodeError::unsupported_content,65);
    const std::size_t stride=U16(b,66), rowSize=stride*planes;
    if(stride<(std::uint64_t(image.width)*bits+7)/8||rowSize>262140)return Failure(DecodeError::invalid_layout,66);
    if(std::uint64_t(rowSize)*image.height>o.maximumPixels*4)return Failure(DecodeError::resource_limit,66);
    std::array<std::byte,768> colors{};
    std::size_t end=b.size();
    if(!trueColor){
        if(colorBits>4){
            if(b.size()>=897&&U8(b,b.size()-769)==12){end-=769;std::copy_n(b.begin()+end+1,768,colors.begin());}
            else if(o.palette.size()==768)std::copy(o.palette.begin(),o.palette.end(),colors.begin());
            else if(U16(b,68)==2){for(unsigned i=0;i<256;++i)std::fill_n(colors.begin()+i*3,3,std::byte(i));}
            else {r.issue.code=o.palette.empty()?DecodeError::palette_required:DecodeError::invalid_palette;return r;}
        }else{
            std::copy_n(b.begin()+16,48,colors.begin());
            // Version zero defines the fixed EGA palette, not a user/game default.
            if(U8(b,1)==0){
                constexpr std::array<std::uint32_t,16> ega={0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
                for(unsigned i=0;i<16;++i)for(unsigned c=0;c<3;++c)colors[i*3+c]=std::byte(ega[i]>>(16-c*8));
                if(colorBits==1)std::fill_n(colors.begin()+3,3,std::byte{255});
            }
        }
    }
    image.rgba8.resize(std::size_t(image.width)*image.height*4);
    std::vector<std::uint8_t> row(rowSize);std::size_t cursor=128;
    for(std::uint32_t y=0;y<image.height;++y){
        std::size_t x=0;
        while(x<rowSize){
            if(cursor>=end)return Failure(DecodeError::truncated,cursor);
            auto value=U8(b,cursor++);std::size_t count=1;
            if(encoding==1&&(value&0xc0)==0xc0){count=value&63;if(!count)return Failure(DecodeError::invalid_run,cursor-1);
                if(cursor>=end)return Failure(DecodeError::truncated,cursor);
                value=U8(b,cursor++);}
            if(count>rowSize-x)return Failure(DecodeError::invalid_run,cursor);
            std::fill_n(row.begin()+x,count,value);x+=count;
        }
        for(x=0;x<image.width;++x){
            const auto pixel=std::size_t(y)*image.width+x;
            if(trueColor){for(unsigned c=0;c<3;++c)image.rgba8[pixel*4+c]=row[c*stride+x];image.rgba8[pixel*4+3]=planes==4?row[3*stride+x]:255;}
            else{unsigned index=0;for(unsigned plane=0;plane<planes;++plane){const auto bit=x*bits;
                index|=((row[plane*stride+bit/8]>>(8-bits-bit%8))&((1u<<bits)-1))<<(plane*bits);}
                PalettePixel(image,pixel,colors,static_cast<std::uint8_t>(index));}
        }
    }
    if(cursor!=end)return Failure(DecodeError::trailing_bytes,cursor);
    return r;
}
}
