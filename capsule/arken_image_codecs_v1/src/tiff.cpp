#include "tiff.hpp"
#include "tiff_directory.hpp"
#include <cmath>
#include <cstring>
#include <skcms_public.h>

// TIFF decode policy (see api.hpp): classic and BigTIFF, both byte orders,
// any page of the IFD chain; strips or tiles, chunky or planar; uncompressed,
// LZW, new-style JPEG, Deflate and PackBits; horizontal predictor; FillOrder 2
// by bit reversal of the encoded chunk (not for JPEG); orientation 1-8; gray,
// palette, RGB, CMYK, YCbCr; associated/unassociated alpha; ICC to sRGB.
// Every other variant rejects with a typed error, never a partial image.
namespace arken::image::v1::detail {
namespace {
constexpr std::size_t maximum_pages=4096;
constexpr std::uint64_t maximum_chunk_bytes=64ull*1024*1024;
constexpr std::uint64_t maximum_sample_bytes=64ull*1024*1024;
constexpr std::uint64_t maximum_icc_bytes=4ull*1024*1024;
constexpr std::uint64_t maximum_jpeg_tables=64ull*1024;
// 8M pixels in 16x16 tiles across 8 planes; bounds offset/count vectors.
constexpr std::uint64_t maximum_chunks=1ull<<19;

void unpack_packbits(Bytes source,std::span<std::byte> output,std::size_t row_bytes){
    std::size_t input=0;
    for(std::size_t row=0;row<output.size();row+=row_bytes){
        std::size_t written=0;
        while(written<row_bytes){
            TiffRequire(input<source.size(),DecodeError::truncated);
            const auto control=U8(source,input++);
            if(control==128)continue;
            const std::size_t count=control<128?control+1:257-control;
            TiffRequire(count<=row_bytes-written,DecodeError::invalid_run);
            const std::size_t consumed=control<128?count:1;
            TiffRequire(Range(source,input,consumed),DecodeError::truncated);
            if(control<128)std::copy_n(source.begin()+input,count,output.begin()+row+written);
            else std::fill_n(output.begin()+row+written,count,source[input]);
            input+=consumed;written+=count;
        }
    }
    while(input<source.size())TiffRequire(U8(source,input++)==128,DecodeError::invalid_run);
}
// TIFF 6.0 LZW: MSB-first codes of 9-12 bits, Clear=256, EOI=257, with the
// TIFF "early change" (widen once the next free code reaches 2^n-1). The old
// LSB-first (pre-6.0) variant is rejected explicitly. Output must fill the
// chunk exactly; overrun, invalid codes and short data are errors.
void unpack_lzw(Bytes source,std::span<std::byte> output){
    TiffRequire(!(source.size()>=2&&U8(source,0)==0&&(U8(source,1)&1)),DecodeError::unsupported_compression);
    struct Code {std::uint16_t prefix;std::uint16_t length;std::uint8_t suffix,first;};
    std::vector<Code> table(4096);
    for(unsigned i=0;i<256;++i)table[i]={0,1,std::uint8_t(i),std::uint8_t(i)};
    const std::uint64_t bit_count=std::uint64_t(source.size())*8;
    std::uint64_t position=0;unsigned width=9,next=258;int previous=-1;std::size_t written=0;
    const auto emit=[&](unsigned code){
        const auto length=table[code].length;
        TiffRequire(length<=output.size()-written,DecodeError::invalid_layout);
        auto c=code;
        for(std::size_t i=length;i-->0;){output[written+i]=std::byte(table[c].suffix);c=table[c].prefix;}
        written+=length;
    };
    while(position+width<=bit_count){
        unsigned code=0;
        for(unsigned i=0;i<width;++i,++position)
            code=(code<<1)|((U8(source,std::size_t(position/8))>>(7-position%8))&1u);
        if(code==257)break;
        if(code==256){width=9;next=258;previous=-1;continue;}
        if(previous<0){TiffRequire(code<256,DecodeError::invalid_layout);emit(code);previous=int(code);continue;}
        if(code<next){
            if(next<4096){table[next]={std::uint16_t(previous),std::uint16_t(table[previous].length+1),table[code].first,table[previous].first};++next;}
            emit(code);
        }else{
            TiffRequire(code==next&&next<4096,DecodeError::invalid_layout);
            table[next]={std::uint16_t(previous),std::uint16_t(table[previous].length+1),table[previous].first,table[previous].first};++next;
            emit(code);
        }
        previous=int(code);
        if(next>=(1u<<width)-1u&&width<12)++width;
    }
    TiffRequire(written==output.size(),DecodeError::truncated);
}
std::uint8_t reverse_bits(std::uint8_t v){
    v=std::uint8_t(((v&0xf0u)>>4)|((v&0x0fu)<<4));
    v=std::uint8_t(((v&0xccu)>>2)|((v&0x33u)<<2));
    return std::uint8_t(((v&0xaau)>>1)|((v&0x55u)<<1));
}
void orient(Image& image,unsigned orientation){
    if(orientation==1)return;
    const auto w=image.width,h=image.height;const bool transpose=orientation>=5;
    const auto out_w=transpose?h:w,out_h=transpose?w:h;
    std::vector<std::uint8_t> output(image.rgba8.size());
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){
        std::uint32_t dx=x,dy=y;
        switch(orientation){case 2:dx=w-1-x;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dy=h-1-y;break;
            case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;case 8:dx=y;dy=w-1-x;break;default:break;}
        std::memcpy(output.data()+(std::size_t(dy)*out_w+dx)*4,image.rgba8.data()+(std::size_t(y)*w+x)*4,4);
    }
    image.rgba8=std::move(output);image.width=out_w;image.height=out_h;
}
enum class Model {gray,palette,rgb,cmyk,ycbcr};
struct Samples {
    bool wide=false;
    std::vector<std::uint8_t> narrow;
    std::vector<std::uint16_t> broad;
    void resize(std::size_t n){if(wide)broad.assign(n,0);else narrow.assign(n,0);}
    std::uint32_t get(std::size_t i)const {return wide?broad[i]:narrow[i];}
    void set(std::size_t i,std::uint32_t v){if(wide)broad[i]=std::uint16_t(v);else narrow[i]=std::uint8_t(v);}
};
struct Colour {
    std::vector<std::byte> bytes;
    skcms_ICCProfile profile{};
    bool present()const {return !bytes.empty();}
};
void read_icc(Bytes payload,Model model,Colour& colour){
    TiffRequire(payload.size()>=132,DecodeError::invalid_layout);
    const auto declared=(std::uint32_t(U8(payload,0))<<24)|(std::uint32_t(U8(payload,1))<<16)|(std::uint32_t(U8(payload,2))<<8)|U8(payload,3);
    TiffRequire(declared==payload.size(),DecodeError::invalid_layout);
    colour.bytes.assign(payload.begin(),payload.end());
    colour.bytes.push_back(std::byte{0});colour.bytes.push_back(std::byte{0}); // skcms padding
    TiffRequire(skcms_Parse(colour.bytes.data(),payload.size(),&colour.profile),DecodeError::invalid_layout);
    const auto space=colour.profile.data_color_space;
    const bool matches=model==Model::gray?space==skcms_Signature_Gray
        :model==Model::cmyk?(space==skcms_Signature_CMYK&&colour.profile.has_A2B)
        :space==skcms_Signature_RGB;
    TiffRequire(matches,DecodeError::unsupported_content);
}
struct Chunk {std::uint64_t offset,size;};

DecodeResult decode(Bytes bytes,const DecodeOptions& options){
    if(options.directionIndex||options.mipIndex)return Failure(DecodeError::selection_out_of_range);
    TiffWork work(bytes.size());
    const auto header=TiffHeader::Parse(bytes);
    const auto pages=TiffDirectory::Chain(bytes,header,maximum_pages,work);
    TiffRequire(options.frameIndex<pages.size(),DecodeError::selection_out_of_range);
    // A truncated or overrun file rejects whole, even when the selected page
    // is intact: every page's directory and out-of-line values must lie in the
    // file. Chunk arrays (strip/tile offsets and byte counts) are read element
    // by element for the selected page only: other pages may all share one
    // 2^19-entry array, and rescanning it per page let a ~4.5 MiB file demand
    // ~2.1e9 range checks. All of this draws on the aggregate `work` budget.
    for(std::size_t index=0;index<pages.size();++index){
        const TiffDirectory other(bytes,header,pages[index],work);(void)other.metadata();
        if(index!=options.frameIndex)continue;
        for(auto [offsets_tag,sizes_tag]:{std::pair<std::uint16_t,std::uint16_t>{273,279},{324,325}}){
            if(!other.has(offsets_tag)||!other.has(sizes_tag))continue;
            for(auto tag:{offsets_tag,sizes_tag})TiffRequire(other.entry(tag).count<=maximum_chunks,DecodeError::resource_limit);
            const auto o=other.values(offsets_tag,maximum_chunks),s=other.values(sizes_tag,maximum_chunks);
            TiffRequire(o.size()==s.size(),DecodeError::invalid_layout);
            for(std::size_t i=0;i<o.size();++i)TiffRequire(Range(bytes,o[i],s[i]),DecodeError::truncated);
        }
    }
    const TiffDirectory f(bytes,header,pages[options.frameIndex],work);
    const TiffBytes& file=f.file;

    const auto width=f.scalar(256,0),height=f.scalar(257,0);
    TiffRequire(Dimensions(width,height,options),DecodeError::invalid_dimensions);
    const auto compression=f.scalar(259,1);
    const bool deflate=compression==8||compression==32946,jpeg=compression==7;
    // CCITT (2/3/4), old-style JPEG (6) and every other scheme reject explicitly.
    TiffRequire(compression==1||compression==5||jpeg||deflate||compression==32773,DecodeError::unsupported_compression);
    const auto photo=f.scalar(262,UINT32_MAX);
    const auto spp=f.scalar(277,1);
    TiffRequire(spp>=1&&spp<=8,DecodeError::unsupported_content);
    std::uint32_t bits=1;
    if(f.has(258)){
        const auto v=f.values(258,8);
        TiffRequire(v.size()==1||v.size()==spp,DecodeError::invalid_layout);
        TiffRequire(std::all_of(v.begin(),v.end(),[&](auto n){return n==v.front();}),DecodeError::unsupported_content);
        bits=std::uint32_t(std::min<std::uint64_t>(v.front(),64));
    }
    if(f.has(339)){
        const auto v=f.values(339,8);
        TiffRequire(v.size()==1||v.size()==spp,DecodeError::invalid_layout);
        // Unsigned integer only: IEEE float (3), signed (2) and void (4) reject.
        TiffRequire(std::all_of(v.begin(),v.end(),[](auto n){return n==1;}),DecodeError::unsupported_content);
    }
    const auto planar=f.scalar(284,1),fill=f.scalar(266,1),predictor=f.scalar(317,1),orientation=f.scalar(274,1);
    TiffRequire(planar==1||planar==2,DecodeError::unsupported_content);
    TiffRequire(fill==1||fill==2,DecodeError::invalid_layout);
    TiffRequire(predictor==1||predictor==2,DecodeError::unsupported_content); // 3: floating point
    TiffRequire(orientation>=1&&orientation<=8,DecodeError::invalid_layout);

    Model model{};unsigned colours=0;
    switch(photo){
        case 0:case 1:model=Model::gray;colours=1;TiffRequire(bits==1||bits==2||bits==4||bits==8||bits==16,DecodeError::unsupported_content);break;
        case 2:model=Model::rgb;colours=3;TiffRequire(bits==8||bits==16,DecodeError::unsupported_content);break;
        case 3:model=Model::palette;colours=1;TiffRequire(bits==1||bits==2||bits==4||bits==8,DecodeError::unsupported_content);break;
        case 5:model=Model::cmyk;colours=4;
            TiffRequire(f.scalar(332,1)==1&&f.scalar(334,4)==4,DecodeError::unsupported_content);
            TiffRequire(bits==8||bits==16,DecodeError::unsupported_content);break;
        case 6:model=Model::ycbcr;colours=3;TiffRequire(bits==8,DecodeError::unsupported_content);break;
        default:throw DecodeError::unsupported_content; // CIELab (8), ICCLab, ITULab, masks, LogLuv...
    }
    TiffRequire(spp>=colours,DecodeError::unsupported_content);
    const bool wide=bits==16;
    // 16-bit samples are only rounded into an explicit rendering derivative.
    TiffRequire(!wide||options.allowPng16BitRenderingDerivative,DecodeError::unsupported_content);
    const unsigned extras=spp-colours;
    int alpha_channel=-1;bool associated=false;
    if(f.has(338)){
        TiffRequire(extras>0,DecodeError::invalid_layout);
        const auto v=f.values(338,8);
        TiffRequire(v.size()==extras,DecodeError::invalid_layout);
        for(unsigned i=0;i<extras;++i){
            TiffRequire(v[i]<=2,DecodeError::unsupported_content);
            if(alpha_channel<0&&v[i]){alpha_channel=int(colours+i);associated=v[i]==1;}
        }
    }
    if(model==Model::cmyk)TiffRequire(!associated,DecodeError::unsupported_content);
    if(predictor==2)TiffRequire(bits==8||bits==16,DecodeError::unsupported_content);
    unsigned sub_h=1,sub_v=1;
    if(jpeg){
        // New-style JPEG: gray (BlackIsZero), YCbCr, or RGB when the stream is
        // RGB-coded; the decoder yields gray or RGB. CMYK/palette-in-JPEG,
        // predictors and extra samples reject.
        TiffRequire((model==Model::ycbcr||model==Model::rgb||(model==Model::gray&&photo==1))&&bits==8&&extras==0
            &&planar==1&&predictor==1,DecodeError::unsupported_content);
        TiffRequire(fill==1,DecodeError::unsupported_content);
    }else if(model==Model::ycbcr){
        if(f.has(530)){
            const auto v=f.values(530,2);TiffRequire(v.size()==2,DecodeError::invalid_layout);
            sub_h=unsigned(std::min<std::uint64_t>(v[0],8));sub_v=unsigned(std::min<std::uint64_t>(v[1],8));
        }else{sub_h=2;sub_v=2;}
        TiffRequire((sub_h==1&&sub_v==1)||(sub_h==2&&sub_v==1)||(sub_h==2&&sub_v==2),DecodeError::unsupported_content);
        TiffRequire(extras==0&&planar==1&&predictor==1,DecodeError::unsupported_content);
    }
    double luma[3]={0.299,0.587,0.114},reference[6]={0,255,128,255,128,255};
    if(model==Model::ycbcr&&!jpeg){
        if(f.has(529)){const auto v=f.rationals(529,3);std::copy(v.begin(),v.end(),luma);}
        if(f.has(532)){const auto v=f.rationals(532,6);std::copy(v.begin(),v.end(),reference);}
        TiffRequire(luma[1]!=0&&reference[1]!=reference[0]&&reference[3]!=reference[2]&&reference[5]!=reference[4],DecodeError::invalid_layout);
    }
    std::vector<std::uint16_t> palette;
    if(model==Model::palette){
        const auto entries=std::uint64_t(3)<<bits;
        TiffRequire(f.has(320)&&f.entry(320).count==entries&&f.entry(320).type==3,DecodeError::invalid_palette);
        for(auto v:f.values(320,entries))palette.push_back(std::uint16_t(v));
    }else TiffRequire(!f.has(320),DecodeError::invalid_layout);
    Colour colour;
    if(f.has(34675))read_icc(f.raw(34675,maximum_icc_bytes),model,colour);

    // Chunk geometry: strips or tiles, one set per plane when planar.
    const bool tiled=f.has(322)||f.has(323)||f.has(324)||f.has(325);
    const unsigned planes=planar==2?spp:1,chunk_samples=planar==2?1:spp;
    std::uint32_t tile_w=width,tile_h=0,across=1,down=0;
    std::uint16_t offsets_tag=273,sizes_tag=279;
    if(tiled){
        TiffRequire(!f.has(273)&&!f.has(279),DecodeError::invalid_layout);
        tile_w=f.scalar(322,0);tile_h=f.scalar(323,0);
        TiffRequire(tile_w&&tile_h&&tile_w%16==0&&tile_h%16==0,DecodeError::invalid_layout);
        TiffRequire(tile_w<=8192&&tile_h<=8192&&std::uint64_t(tile_w)*tile_h<=options.maximumPixels,DecodeError::resource_limit);
        across=(width+tile_w-1)/tile_w;down=(height+tile_h-1)/tile_h;
        offsets_tag=324;sizes_tag=325;
    }else{
        const auto rows=f.scalar(278,UINT32_MAX);
        TiffRequire(rows>0,DecodeError::invalid_layout);
        tile_h=std::min(rows,height);down=(height+tile_h-1)/tile_h;
        TiffRequire(tile_h==height||tile_h%sub_v==0,DecodeError::invalid_layout);
    }
    const auto per_plane=std::uint64_t(across)*down,total=per_plane*planes;
    TiffRequire(f.has(offsets_tag)&&f.has(sizes_tag),DecodeError::invalid_layout);
    TiffRequire(f.entry(offsets_tag).count==total&&f.entry(sizes_tag).count==total,DecodeError::invalid_layout);
    const auto offsets=f.values(offsets_tag,total),sizes=f.values(sizes_tag,total);
    // Chunks may not overlap the header, the directory, its out-of-line values
    // or each other. Metadata is merged into disjoint sorted spans first so the
    // check stays O(n log n) for large tile counts.
    auto metadata=f.metadata();
    std::sort(metadata.begin(),metadata.end(),[](auto a,auto b){return a.begin<b.begin;});
    std::vector<TiffRange> spans;
    for(auto m:metadata){
        if(!m.size)continue;
        if(!spans.empty()&&m.begin<=spans.back().begin+spans.back().size)
            spans.back().size=std::max(spans.back().begin+spans.back().size,m.begin+m.size)-spans.back().begin;
        else spans.push_back(m);
    }
    std::vector<TiffRange> ranges;ranges.reserve(offsets.size());
    for(std::size_t i=0;i<offsets.size();++i){
        TiffRequire(sizes[i]&&Range(bytes,offsets[i],sizes[i]),DecodeError::truncated);
        const TiffRange range{offsets[i],sizes[i]};
        const auto after=std::upper_bound(spans.begin(),spans.end(),range.begin,[](std::uint64_t p,const TiffRange& s){return p<s.begin;});
        if(after!=spans.end())TiffRequire(!TiffOverlap(range,*after),DecodeError::invalid_layout);
        if(after!=spans.begin())TiffRequire(!TiffOverlap(range,*std::prev(after)),DecodeError::invalid_layout);
        ranges.push_back(range);
    }
    std::sort(ranges.begin(),ranges.end(),[](auto a,auto b){return a.begin<b.begin;});
    for(std::size_t i=1;i<ranges.size();++i)TiffRequire(!TiffOverlap(ranges[i-1],ranges[i]),DecodeError::invalid_layout);
    Bytes tables;
    if(jpeg&&f.has(347)){
        tables=f.raw(347,maximum_jpeg_tables);
        TiffRequire(tables.size()>=4&&U8(tables,0)==255&&U8(tables,1)==216
            &&U8(tables,tables.size()-2)==255&&U8(tables,tables.size()-1)==217,DecodeError::invalid_layout);
    }

    // Decoded sample store: JPEG/YCbCr arrive as RGB; everything else keeps spp.
    const unsigned stored=model==Model::ycbcr?3:spp;
    const auto pixel_count=std::uint64_t(width)*height;
    TiffRequire(pixel_count*stored*(wide?2:1)<=maximum_sample_bytes,DecodeError::resource_limit);
    Samples samples;samples.wide=wide;samples.resize(std::size_t(pixel_count*stored));
    const auto clamp8=[](double v){return std::uint32_t(v<=0?0:v>=255?255:std::floor(v+0.5));};
    std::vector<std::byte> encoded_copy,decoded;std::vector<std::uint8_t> jpeg_samples;std::vector<std::byte> stream;
    for(std::uint64_t index=0;index<total;++index){
        const auto plane=unsigned(index/per_plane);const auto k=index%per_plane;
        const auto x0=std::uint32_t(k%across)*tile_w,y0=std::uint32_t(k/across)*tile_h;
        const auto visible_w=std::min(tile_w,width-x0),visible_h=std::min(tile_h,height-y0);
        const auto data_w=tile_w,data_h=tiled?tile_h:visible_h;
        Bytes encoded=bytes.subspan(std::size_t(offsets[index]),std::size_t(sizes[index]));
        if(jpeg){
            if(!tables.empty()){
                TiffRequire(encoded.size()>=2&&U8(encoded,0)==255&&U8(encoded,1)==216,DecodeError::invalid_layout);
                stream.assign(tables.begin(),tables.end()-2);stream.insert(stream.end(),encoded.begin()+2,encoded.end());
                encoded=stream;
            }
            const auto error=DecodeTiffJpeg(encoded,data_w,data_h,colours,model==Model::rgb,jpeg_samples);
            TiffRequire(error==DecodeError::none,error);
            for(std::uint32_t y=0;y<visible_h;++y)for(std::uint32_t x=0;x<visible_w;++x)for(unsigned c=0;c<colours;++c)
                samples.set((std::size_t(y0+y)*width+x0+x)*stored+c,jpeg_samples[(std::size_t(y)*data_w+x)*colours+c]);
            continue;
        }
        std::uint64_t row_bytes=0,row_count=0;
        if(model==Model::ycbcr){
            row_bytes=std::uint64_t((data_w+sub_h-1)/sub_h)*(sub_h*sub_v+2);row_count=(data_h+sub_v-1)/sub_v;
        }else{row_bytes=(std::uint64_t(data_w)*chunk_samples*bits+7)/8;row_count=data_h;}
        const auto extent=row_bytes*row_count;
        TiffRequire(extent<=maximum_chunk_bytes,DecodeError::resource_limit);
        if(fill==2){
            encoded_copy.resize(encoded.size());
            for(std::size_t i=0;i<encoded.size();++i)encoded_copy[i]=std::byte(reverse_bits(U8(encoded,i)));
            encoded=encoded_copy;
        }
        Bytes data=encoded;
        if(compression==1)TiffRequire(encoded.size()==extent,DecodeError::invalid_layout);
        else{
            decoded.assign(std::size_t(extent),std::byte{0});
            if(deflate)TiffRequire(InflateTiffStrip(encoded,decoded),DecodeError::invalid_layout);
            else if(compression==5)unpack_lzw(encoded,decoded);
            else unpack_packbits(encoded,decoded,std::size_t(row_bytes));
            data=decoded;
        }
        if(predictor==2){
            if(data.data()!=decoded.data()){decoded.assign(data.begin(),data.end());data=decoded;}
            for(std::uint64_t y=0;y<row_count;++y){
                const auto row=std::size_t(y*row_bytes);
                if(bits==8){for(std::size_t i=chunk_samples;i<std::size_t(data_w)*chunk_samples;++i)
                    decoded[row+i]=std::byte((U8(decoded,row+i)+U8(decoded,row+i-chunk_samples))&255u);}
                else{const TiffBytes words{decoded,file.little};
                    for(std::size_t i=chunk_samples;i<std::size_t(data_w)*chunk_samples;++i){
                        const auto v=std::uint16_t(words.u16(row+i*2)+words.u16(row+(i-chunk_samples)*2));
                        decoded[row+i*2+(file.little?0:1)]=std::byte(v&255u);decoded[row+i*2+(file.little?1:0)]=std::byte(v>>8);}}
            }
        }
        if(model==Model::ycbcr){
            const unsigned unit=sub_h*sub_v+2;
            for(std::uint64_t by=0;by<row_count;++by)for(std::uint32_t bx=0;bx<(data_w+sub_h-1)/sub_h;++bx){
                const auto base=std::size_t(by*row_bytes)+std::size_t(bx)*unit;
                const double cb=(U8(data,base+unit-2)-reference[2])*127.0/(reference[3]-reference[2]);
                const double cr=(U8(data,base+unit-1)-reference[4])*127.0/(reference[5]-reference[4]);
                for(unsigned j=0;j<sub_v;++j)for(unsigned i=0;i<sub_h;++i){
                    const auto x=bx*sub_h+i,y=std::uint32_t(by*sub_v+j);
                    if(x>=visible_w||y>=visible_h)continue;
                    const double luminance=(U8(data,base+j*sub_h+i)-reference[0])*255.0/(reference[1]-reference[0]);
                    const double r=luminance+cr*(2-2*luma[0]),b=luminance+cb*(2-2*luma[2]);
                    const double g=(luminance-luma[2]*b-luma[0]*r)/luma[1];
                    const auto out=(std::size_t(y0+y)*width+x0+x)*3;
                    samples.set(out,clamp8(r));samples.set(out+1,clamp8(g));samples.set(out+2,clamp8(b));
                }
            }
            continue;
        }
        const TiffBytes words{data,file.little};const auto mask=(1u<<bits)-1u;
        for(std::uint32_t y=0;y<visible_h;++y){
            const auto row=std::uint64_t(y)*row_bytes;
            for(std::uint32_t x=0;x<visible_w;++x)for(unsigned s=0;s<chunk_samples;++s){
                const auto j=std::uint64_t(x)*chunk_samples+s;std::uint32_t v=0;
                if(bits==16)v=words.u16(row+j*2);
                else if(bits==8)v=U8(data,std::size_t(row+j));
                else{const auto bit=j*bits;v=(U8(data,std::size_t(row+bit/8))>>(8-bits-bit%8))&mask;}
                samples.set((std::size_t(y0+y)*width+x0+x)*stored+(planar==2?plane:s),v);
            }
        }
    }

    // Colour rendering into straight RGBA8 (or RGBA16 for 16-bit sources).
    DecodeResult result;auto& image=result.image;
    image.width=width;image.height=height;image.frameCount=std::uint32_t(pages.size());
    image.sourceBitsPerChannel=wide?16:8;
    const auto n=std::size_t(pixel_count);
    image.rgba8.resize(n*4);
    std::vector<std::uint16_t> wide_rgba;if(wide)wide_rgba.resize(n*4);
    const std::uint32_t top=wide?65535u:255u,source_top=(1u<<std::min(bits,16u))-1u;
    const auto scale=[&](std::uint32_t v){return bits>=8?v:v*255u/source_top;};
    const bool cmyk_profile=model==Model::cmyk&&colour.present();
    std::vector<std::uint8_t> cmyk_alpha;if(cmyk_profile&&!wide)cmyk_alpha.resize(n);
    std::vector<std::uint16_t> cmyk_alpha16;if(cmyk_profile&&wide)cmyk_alpha16.resize(n);
    for(std::size_t i=0;i<n;++i){
        const auto at=i*stored;
        std::uint32_t c[4]={0,0,0,0};std::uint32_t a=top;
        if(alpha_channel>=0)a=scale(samples.get(at+unsigned(alpha_channel)));
        unsigned count=0;
        switch(model){
            case Model::gray:{auto v=scale(samples.get(at));if(photo==0)v=top-v;c[0]=c[1]=c[2]=v;count=3;break;}
            case Model::palette:{const auto index=samples.get(at);
                for(unsigned k=0;k<3;++k){c[k]=(std::uint32_t(palette[k*(std::size_t(1)<<bits)+index])+128u)/257u;}count=3;break;}
            case Model::rgb:for(unsigned k=0;k<3;++k){c[k]=samples.get(at+k);}count=3;break;
            case Model::ycbcr:for(unsigned k=0;k<3;++k){c[k]=samples.get(at+k);}count=3;break;
            case Model::cmyk:for(unsigned k=0;k<4;++k){c[k]=samples.get(at+k);}count=4;break;
        }
        // Associated alpha: colour = round(premultiplied * max / alpha), clamped
        // to max; fully transparent pixels become zero colour.
        if(associated)for(unsigned k=0;k<count;++k)c[k]=a?std::min(top,(c[k]*top+a/2)/a):0;
        std::uint32_t out[4]={0,0,0,a};
        if(model==Model::cmyk){
            if(cmyk_profile){
                // skcms consumes Adobe-style inverted CMYK and treats CMYK as opaque.
                for(unsigned k=0;k<4;++k)out[k]=top-c[k];
                if(wide)cmyk_alpha16[i]=std::uint16_t(a);else cmyk_alpha[i]=std::uint8_t(a);
            }else for(unsigned k=0;k<3;++k)out[k]=((top-c[k])*(top-c[3])+top/2)/top;
        }else for(unsigned k=0;k<3;++k)out[k]=c[k];
        if(wide)for(unsigned k=0;k<4;++k)wide_rgba[i*4+k]=std::uint16_t(out[k]);
        else for(unsigned k=0;k<4;++k)image.rgba8[i*4+k]=std::uint8_t(out[k]);
    }
    samples=Samples{};
    if(colour.present()){
        const auto format=wide?(std::endian::native==std::endian::little?skcms_PixelFormat_RGBA_16161616LE:skcms_PixelFormat_RGBA_16161616BE)
            :skcms_PixelFormat_RGBA_8888;
        const void* source=wide?static_cast<const void*>(wide_rgba.data()):static_cast<const void*>(image.rgba8.data());
        TiffRequire(skcms_Transform(source,format,skcms_AlphaFormat_Unpremul,&colour.profile,
            image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),n),DecodeError::unsupported_content);
        if(cmyk_profile)for(std::size_t i=0;i<n;++i)
            image.rgba8[i*4+3]=wide?std::uint8_t((std::uint32_t(cmyk_alpha16[i])+128u)/257u):cmyk_alpha[i];
        if(wide&&!cmyk_profile)for(std::size_t i=0;i<n;++i)image.rgba8[i*4+3]=std::uint8_t((std::uint32_t(wide_rgba[i*4+3])+128u)/257u);
    }else if(wide){
        for(std::size_t i=0;i<n*4;++i)image.rgba8[i]=std::uint8_t((std::uint32_t(wide_rgba[i])+128u)/257u);
    }
    orient(image,orientation);
    return result;
}
}
DecodeResult DecodeTiff(Bytes bytes,const DecodeOptions& options){
    try{return decode(bytes,options);}catch(DecodeError error){return Failure(error);}
}
// Default: one classic little-endian uncompressed chunky RGBA8 strip.
// Deflate: compression 8 with horizontal predictor, strips of about 64 KiB.
EncodeResult EncodeTiff(std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba,std::uint64_t maximum_bytes,bool deflate){
    const auto row_bytes=std::size_t(width)*4;
    const std::uint32_t rows=deflate?std::max<std::uint32_t>(1,std::min<std::uint32_t>(height,std::uint32_t(65536/row_bytes))):height;
    const std::uint32_t strips=(height+rows-1)/rows;
    std::vector<std::vector<std::byte>> payloads;
    if(deflate){
        std::vector<std::uint8_t> predicted;
        for(std::uint32_t s=0;s<strips;++s){
            const auto y0=std::size_t(s)*rows,n=std::min<std::size_t>(rows,height-y0);
            predicted.assign(rgba.begin()+std::ptrdiff_t(y0*row_bytes),rgba.begin()+std::ptrdiff_t((y0+n)*row_bytes));
            for(std::size_t y=0;y<n;++y)for(std::size_t i=row_bytes;i-->4;)
                predicted[y*row_bytes+i]=std::uint8_t(predicted[y*row_bytes+i]-predicted[y*row_bytes+i-4]);
            payloads.emplace_back();
            if(!CompressTiffZlib(predicted,payloads.back()))return {EncodeError::encoding_failed,false,{}};
        }
    }
    const std::uint32_t count=deflate?16:15;
    const std::uint32_t ifd_end=8+2+count*12+4,bits=ifd_end,xres=bits+8,yres=xres+8;
    const std::uint32_t offsets_at=yres+8,sizes_at=offsets_at+(strips>1?strips*4:0),pixels=sizes_at+(strips>1?strips*4:0);
    std::uint64_t data=0;
    if(deflate)for(const auto& p:payloads)data+=p.size();else data=rgba.size();
    const auto total=std::uint64_t(pixels)+data;
    if(total>maximum_bytes||total>UINT32_MAX)return {EncodeError::output_limit,false,{}};
    std::vector<std::byte> bytes(static_cast<std::size_t>(total));
    const auto put=[&](std::size_t offset,std::uint32_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes[offset+i]=std::byte((value>>(8*i))&255u);};
    bytes[0]=bytes[1]=std::byte{73};put(2,42,2);put(4,8,4);put(8,count,2);
    std::size_t entry=10;
    const auto tag=[&](unsigned id,unsigned type,std::uint32_t length,std::uint32_t value){
        put(entry,id,2);put(entry+2,type,2);put(entry+4,length,4);put(entry+8,value,4);entry+=12;
    };
    std::vector<std::uint32_t> positions,lengths;
    if(deflate){std::uint32_t at=pixels;for(const auto& p:payloads){positions.push_back(at);lengths.push_back(std::uint32_t(p.size()));at+=std::uint32_t(p.size());}}
    else{positions.push_back(pixels);lengths.push_back(static_cast<std::uint32_t>(rgba.size()));}
    tag(256,4,1,width);tag(257,4,1,height);tag(258,3,4,bits);tag(259,3,1,deflate?8:1);
    tag(262,3,1,2);tag(273,4,strips,strips>1?offsets_at:positions[0]);tag(274,3,1,1);tag(277,3,1,4);
    tag(278,4,1,rows);tag(279,4,strips,strips>1?sizes_at:lengths[0]);
    tag(282,5,1,xres);tag(283,5,1,yres);tag(284,3,1,1);tag(296,3,1,1);
    if(deflate)tag(317,3,1,2);
    tag(338,3,1,2);
    for(unsigned i=0;i<4;++i)put(bits+i*2,8,2);
    put(xres,1,4);put(xres+4,1,4);put(yres,1,4);put(yres+4,1,4);
    if(strips>1)for(std::uint32_t s=0;s<strips;++s){put(offsets_at+s*4,positions[s],4);put(sizes_at+s*4,lengths[s],4);}
    if(deflate)for(std::uint32_t s=0;s<strips;++s)std::memcpy(bytes.data()+positions[s],payloads[s].data(),payloads[s].size());
    else std::memcpy(bytes.data()+pixels,rgba.data(),rgba.size());
    return {EncodeError::none,false,std::move(bytes)};
}
}
