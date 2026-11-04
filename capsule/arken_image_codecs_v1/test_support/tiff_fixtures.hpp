#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace tiff_fixture {
using Bytes=std::vector<std::byte>;
inline void Put(Bytes& b,std::size_t p,std::uint64_t v,unsigned size,bool little=true){
    for(unsigned i=0;i<size;++i)b[p+i]=std::byte((v>>(8*(little?i:size-i-1)))&255u);
}
inline std::uint32_t Get(const Bytes& b,std::size_t p,unsigned size=4){
    std::uint32_t v=0;for(unsigned i=0;i<size;++i)v|=std::uint32_t(std::to_integer<unsigned>(b.at(p+i)))<<(i*8);return v;
}
inline std::size_t Field(const Bytes& b,unsigned tag){
    for(unsigned i=0;i<Get(b,8,2);++i){const auto p=10+i*12;if(Get(b,p,2)==tag)return p;}
    return 0;
}
inline void Scalar(Bytes& b,unsigned tag,std::uint32_t v){const auto p=Field(b,tag);Put(b,p+8,v,Get(b,p+2,2)==3?2:4);}
inline std::uint32_t Adler(const Bytes& b){
    std::uint32_t a=1,s=0;for(auto c:b){a=(a+std::to_integer<unsigned>(c))%65521;s=(s+a)%65521;}return (s<<16)|a;
}
// zlib stream of stored DEFLATE blocks (independent of the library's compressor).
inline Bytes Zlib(const Bytes& b){
    Bytes z{std::byte{0x78},std::byte{1}};std::size_t at=0;
    do{const auto n=std::min<std::size_t>(65535,b.size()-at);const bool last=at+n==b.size();
        const auto p=z.size();z.resize(p+5);z[p]=std::byte(last?1:0);Put(z,p+1,n,2);Put(z,p+3,~std::uint32_t(n)&0xffffu,2);
        z.insert(z.end(),b.begin()+std::ptrdiff_t(at),b.begin()+std::ptrdiff_t(at+n));at+=n;}while(at<b.size());
    const auto p=z.size();z.resize(p+4);Put(z,p,Adler(b),4,false);return z;
}
// TIFF 6.0 LZW encoder: MSB-first, 9-12 bit codes, early change, Clear before
// the table fills, explicit EOI.
inline Bytes Lzw(const Bytes& in){
    Bytes out;std::uint32_t buffer=0;unsigned held=0;
    const auto put=[&](unsigned code,unsigned width){buffer=(buffer<<width)|code;held+=width;
        while(held>=8){out.push_back(std::byte((buffer>>(held-8))&255u));held-=8;}buffer&=(1u<<held)-1u;};
    std::map<std::pair<unsigned,unsigned>,unsigned> table;unsigned next=258,width=9;
    put(256,9);int w=-1;
    for(auto byte:in){const auto c=std::to_integer<unsigned>(byte);
        if(w<0){w=int(c);continue;}
        const auto it=table.find({unsigned(w),c});
        if(it!=table.end()){w=int(it->second);continue;}
        put(unsigned(w),width);table[{unsigned(w),c}]=next++;
        if(next==4093){put(256,width);table.clear();next=258;width=9;}
        else if(next==(1u<<width)&&width<12)++width;
        w=int(c);
    }
    if(w>=0){put(unsigned(w),width);++next;if(next==(1u<<width)&&width<12)++width;}
    put(257,width);if(held)out.push_back(std::byte((buffer<<(8-held))&255u));
    return out;
}
inline Bytes PackBits(const Bytes& raw,std::size_t row_bytes){
    Bytes packed;
    for(std::size_t row=0;row<raw.size();row+=row_bytes)
        for(std::size_t at=0;at<row_bytes;){const auto n=std::min<std::size_t>(128,row_bytes-at);
            packed.push_back(std::byte(n-1));packed.insert(packed.end(),raw.begin()+std::ptrdiff_t(row+at),raw.begin()+std::ptrdiff_t(row+at+n));at+=n;}
    return packed;
}
inline std::byte Reverse(std::byte b){unsigned v=std::to_integer<unsigned>(b),r=0;for(unsigned i=0;i<8;++i)if(v&(1u<<i))r|=0x80u>>i;return std::byte(r);}
inline Bytes Compress(const Bytes& raw,unsigned compression,std::size_t row_bytes,unsigned fill=1){
    Bytes out=compression==8||compression==32946?Zlib(raw):compression==5?Lzw(raw):compression==32773?PackBits(raw,row_bytes):raw;
    if(fill==2)for(auto& b:out)b=Reverse(b);
    return out;
}

// Generic writer: classic or BigTIFF, either byte order, any number of pages.
// Chunk offsets/byte counts (273/279 or 324/325) are filled in from `chunks`.
struct Tag {unsigned type=3;std::vector<std::uint64_t> values;Bytes raw;};
struct Page {std::map<unsigned,Tag> tags;std::vector<Bytes> chunks;bool tiled=false;};
inline unsigned TypeSize(unsigned type){return type==3||type==8?2:type==4||type==9||type==13?4:type==5||type==10||type==16||type==17||type==12?8:1;}
inline Bytes Build(std::vector<Page> pages,bool little=true,bool big=false){
    Bytes b(big?16:8);b[0]=b[1]=std::byte(little?'I':'M');
    Put(b,2,big?43:42,2,little);if(big){Put(b,4,8,2,little);Put(b,6,0,2,little);}
    std::size_t link=big?8:4;const unsigned word=big?8:4,entry=big?20:12;
    for(auto& page:pages){
        std::vector<std::uint64_t> offsets,counts;
        for(const auto& c:page.chunks){if(b.size()%2)b.push_back(std::byte{0});offsets.push_back(b.size());counts.push_back(c.size());b.insert(b.end(),c.begin(),c.end());}
        const unsigned type=big?16:4;
        page.tags[page.tiled?324:273]={type,offsets,{}};page.tags[page.tiled?325:279]={type,counts,{}};
        if(b.size()%2)b.push_back(std::byte{0});
        const auto ifd=b.size();Put(b,link,ifd,word,little);
        b.resize(ifd+(big?8:2)+page.tags.size()*entry+word);Put(b,ifd,page.tags.size(),big?8:2,little);
        std::size_t at=ifd+(big?8:2);
        for(const auto& [id,t]:page.tags){
            const auto size=TypeSize(t.type);const bool raw=t.type==1||t.type==2||t.type==7;
            const std::size_t count=raw?t.raw.size():t.type==5?t.values.size()/2:t.values.size();
            Put(b,at,id,2,little);Put(b,at+2,t.type,2,little);Put(b,at+4,count,big?8:4,little);
            const std::size_t extent=std::size_t(size)*count;std::size_t data=at+(big?12:8);
            if(extent>word){if(b.size()%2)b.push_back(std::byte{0});data=b.size();Put(b,at+(big?12:8),data,word,little);b.resize(data+extent);}
            if(raw)std::copy(t.raw.begin(),t.raw.end(),b.begin()+std::ptrdiff_t(data));
            else if(t.type==5)for(std::size_t i=0;i<t.values.size();++i)Put(b,data+i*4,t.values[i],4,little);
            else for(std::size_t i=0;i<count;++i)Put(b,data+i*size,t.values[i],size,little);
            at+=entry;
        }
        link=ifd+(big?8:2)+page.tags.size()*entry;
    }
    return b;
}
// Sample layout description; `samples` is chunky, width*height*spp values.
struct Spec {
    unsigned width=7,height=5,bits=8,spp=3,photometric=2,compression=1,predictor=1,planar=1,rows=2;
    unsigned tileW=0,tileH=0,fill=1;
    bool little=true;
    std::vector<std::uint32_t> samples;
    std::map<unsigned,Tag> extra;
};
inline Page MakePage(const Spec& s){
    Page page;page.tiled=s.tileW!=0;
    const unsigned planes=s.planar==2?s.spp:1,cs=s.planar==2?1:s.spp;
    const unsigned cw=page.tiled?s.tileW:s.width,ch=page.tiled?s.tileH:s.rows;
    const unsigned across=(s.width+cw-1)/cw,down=(s.height+ch-1)/ch;
    for(unsigned plane=0;plane<planes;++plane)for(unsigned ty=0;ty<down;++ty)for(unsigned tx=0;tx<across;++tx){
        const unsigned x0=tx*cw,y0=ty*ch,dh=page.tiled?ch:std::min(ch,s.height-y0);
        const std::size_t row_bytes=(std::size_t(cw)*cs*s.bits+7)/8;Bytes raw(row_bytes*dh);
        for(unsigned y=0;y<dh;++y){
            std::vector<std::uint32_t> row(std::size_t(cw)*cs);
            for(unsigned x=0;x<cw;++x)for(unsigned c=0;c<cs;++c){const auto X=x0+x,Y=y0+y;
                if(X<s.width&&Y<s.height)row[x*cs+c]=s.samples[(std::size_t(Y)*s.width+X)*s.spp+(s.planar==2?plane:c)];}
            if(s.predictor==2)for(std::size_t i=row.size();i-->cs;)row[i]=(row[i]-row[i-cs])&((1u<<s.bits)-1u);
            for(std::size_t i=0;i<row.size();++i){const auto at=y*row_bytes;
                if(s.bits==16)Put(raw,at+i*2,row[i],2,s.little);
                else if(s.bits==8)raw[at+i]=std::byte(row[i]);
                else{const auto bit=i*s.bits;raw[at+bit/8]|=std::byte(row[i]<<(8-s.bits-bit%8));}
            }
        }
        page.chunks.push_back(Compress(raw,s.compression,row_bytes,s.fill));
    }
    const std::uint64_t w=s.width,h=s.height;
    page.tags={{256,{4,{w},{}}},{257,{4,{h},{}}},{258,{3,std::vector<std::uint64_t>(s.spp,s.bits),{}}},
        {259,{3,{s.compression},{}}},{262,{3,{s.photometric},{}}},{277,{3,{s.spp},{}}}};
    if(s.planar!=1)page.tags[284]={3,{s.planar},{}};
    if(s.predictor!=1)page.tags[317]={3,{s.predictor},{}};
    if(s.fill!=1)page.tags[266]={3,{s.fill},{}};
    if(page.tiled){page.tags[322]={3,{s.tileW},{}};page.tags[323]={3,{s.tileH},{}};}
    else page.tags[278]={4,{s.rows},{}};
    for(const auto& [id,t]:s.extra)page.tags[id]=t;
    return page;
}
inline Bytes Encode(const Spec& s,bool big=false){return Build({MakePage(s)},s.little,big);}
// Deterministic asymmetric sample pattern.
inline std::vector<std::uint32_t> Pattern(unsigned w,unsigned h,unsigned spp,unsigned bits,unsigned seed=0){
    std::vector<std::uint32_t> v(std::size_t(w)*h*spp);const auto mask=bits>=32?~0u:(1u<<bits)-1u;
    for(std::size_t i=0;i<v.size();++i)v[i]=std::uint32_t((i*2654435761u+seed*40503u+(i/spp)*97u)>>7)&mask;
    return v;
}

struct Image {Bytes bytes;std::vector<std::uint8_t> rgba;};
inline Image Make(unsigned channels=4,unsigned photo=2,bool little=true,unsigned compression=1,
    unsigned predictor=1,unsigned rows=2){
    constexpr unsigned w=7,h=3;
    Image result;std::vector<Bytes> strips;
    for(unsigned y=0;y<h;y+=rows){
        Bytes raw;const auto n=std::min(rows,h-y);
        for(unsigned row=0;row<n;++row)for(unsigned x=0;x<w;++x){
            const auto red=std::uint8_t(11+x*17),green=std::uint8_t(27+(y+row)*31),blue=std::uint8_t(201-x*13);
            const auto alpha=std::uint8_t(x%3==0?0:x%3==1?127:255);
            raw.push_back(std::byte(red));
            if(photo==2){raw.push_back(std::byte(green));raw.push_back(std::byte(blue));}
            const bool has_alpha=channels==(photo==2?4u:2u);
            if(has_alpha)raw.push_back(std::byte(alpha));
            if(photo==2)result.rgba.insert(result.rgba.end(),{red,green,blue});
            else result.rgba.insert(result.rgba.end(),3,photo==0?std::uint8_t(255-red):red);
            result.rgba.push_back(has_alpha?alpha:255);
        }
        if(predictor==2)for(unsigned row=0;row<n;++row)
            for(std::size_t i=w*channels;i-->channels;){const auto p=row*w*channels+i;
                raw[p]=std::byte((std::to_integer<unsigned>(raw[p])-std::to_integer<unsigned>(raw[p-channels]))&255u);}
        if(compression==8||compression==32946)strips.push_back(Zlib(raw));
        else if(compression==32773){
            Bytes packed;
            for(unsigned row=0;row<n;++row){packed.push_back(std::byte{128});packed.push_back(std::byte(w*channels-1));
                packed.insert(packed.end(),raw.begin()+row*w*channels,raw.begin()+(row+1)*w*channels);}
            strips.push_back(std::move(packed));
        }else strips.push_back(std::move(raw));
    }
    using Entry=std::pair<unsigned,std::vector<std::uint32_t>>;
    std::map<unsigned,Entry> tags{{256,{4,{w}}},{257,{4,{h}}},{258,{3,std::vector<std::uint32_t>(channels,8)}},
        {259,{3,{compression}}},{262,{3,{photo}}},{273,{4,std::vector<std::uint32_t>(strips.size())}},
        {274,{3,{1}}},{277,{3,{channels}}},{278,{4,{rows}}},{279,{4,{}}},{284,{3,{1}}},{317,{3,{predictor}}}};
    if(channels==(photo==2?4u:2u))tags[338]={3,{2}};
    for(const auto& s:strips)tags[279].second.push_back(s.size());
    auto& b=result.bytes;b.resize(8+2+tags.size()*12+4);
    b[0]=b[1]=std::byte(little?'I':'M');Put(b,2,42,2,little);Put(b,4,8,4,little);Put(b,8,tags.size(),2,little);
    std::size_t at=10,offsets=0;
    for(const auto& [id,e]:tags){
        Put(b,at,id,2,little);Put(b,at+2,e.first,2,little);Put(b,at+4,e.second.size(),4,little);
        const auto size=e.first==3?2u:4u;
        const auto extent=std::size_t(size)*e.second.size();
        std::size_t data=at+8;
        if(extent>4){data=b.size();Put(b,at+8,data,4,little);b.resize(data+extent);}
        if(id==273)offsets=data;
        for(std::size_t i=0;i<e.second.size();++i)Put(b,data+i*size,e.second[i],size,little);
        at+=12;
    }
    for(std::size_t i=0;i<strips.size();++i){Put(b,offsets+i*4,b.size(),4,little);b.insert(b.end(),strips[i].begin(),strips[i].end());}
    return result;
}

// ICC fixtures (big-endian ICC encoding).
inline void Be(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.at(at+i)=std::byte((value>>(24-8*i))&255);}
inline Bytes IccHeader(std::size_t size,std::uint32_t space,std::uint32_t pcs,std::uint32_t device_class,unsigned tags){
    Bytes p(size);Be(p,0,std::uint32_t(size));Be(p,8,0x02100000);Be(p,12,device_class);Be(p,16,space);Be(p,20,pcs);
    Be(p,36,0x61637370);Be(p,68,63190);Be(p,72,65536);Be(p,76,54061);Be(p,128,tags);return p;
}
// Matrix/TRC RGB profile with sRGB primaries and a linear (gamma 1) curve.
inline Bytes LinearRgbProfile(){
    auto profile=IccHeader(348,0x52474220,0x58595a20,0x6d6e7472,6);
    const std::uint32_t tags[]={0x7258595a,0x6758595a,0x6258595a,0x72545243,0x67545243,0x62545243};
    const double xyz[][3]={{0.4360747,0.2225045,0.0139322},{0.3850649,0.7168786,0.0971045},{0.1430804,0.0606169,0.7141733}};
    std::size_t at=204;
    for(unsigned i=0;i<6;++i){
        Be(profile,132+12*i,tags[i]);Be(profile,136+12*i,std::uint32_t(at));Be(profile,140+12*i,i<3?20:14);
        if(i<3){Be(profile,at,0x58595a20);for(unsigned c=0;c<3;++c)Be(profile,at+8+4*c,std::uint32_t(std::lround(xyz[i][c]*65536)));at+=20;}
        else{Be(profile,at,0x63757276);Be(profile,at+8,1);profile[at+12]=std::byte{1};at+=28;}
    }
    return profile;
}
// Gray profile with a linear kTRC.
inline Bytes LinearGrayProfile(){
    auto profile=IccHeader(160,0x47524159,0x58595a20,0x6d6e7472,1);
    Be(profile,132,0x6b545243);Be(profile,136,144);Be(profile,140,14);
    Be(profile,144,0x63757276);Be(profile,152,1);profile[156]=std::byte{1};return profile;
}
// CMYK printer profile, lut8 A2B0 to Lab with a 2-point grid whose output
// depends on K only: no black ink is Lab white, full black ink is Lab black.
inline Bytes KOnlyCmykProfile(){
    constexpr std::size_t lut=48+4*256+16*3+3*256;
    auto profile=IccHeader(144+lut,0x434d594b,0x4c616220,0x70727472,1);
    Be(profile,132,0x41324230);Be(profile,136,144);Be(profile,140,std::uint32_t(lut));
    const std::size_t at=144;Be(profile,at,0x6d667431);
    profile[at+8]=std::byte{4};profile[at+9]=std::byte{3};profile[at+10]=std::byte{2};
    for(unsigned i=0;i<3;++i)Be(profile,at+12+i*16,65536);
    std::size_t p=at+48;
    for(unsigned c=0;c<4;++c)for(unsigned i=0;i<256;++i)profile[p++]=std::byte(i);
    for(unsigned g=0;g<16;++g){profile[p++]=std::byte(g&1?0:255);profile[p++]=std::byte{128};profile[p++]=std::byte{128};}
    for(unsigned c=0;c<3;++c)for(unsigned i=0;i<256;++i)profile[p++]=std::byte(i);
    return profile;
}
inline double SrgbEncode(double linear){
    return linear<=0.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-0.055;
}
}
