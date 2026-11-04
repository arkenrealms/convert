#include "arken_image_codecs_v1/encode.hpp"
#include "../test_support/tiff_fixtures.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace image=arken::image::v1;
namespace tf=tiff_fixture;
namespace fs=std::filesystem;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class T>void Write(const fs::path& path,const std::vector<T>& bytes){
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()*sizeof(T));Require(bool(f),"Fixture write failed");
}
tf::Bytes Read(const fs::path& path){
    const auto size=fs::file_size(path);Require(size<=64ull*1024*1024,"Fixture too large");tf::Bytes b(size);
    std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),b.size());Require(bool(f),"Fixture read failed");return b;
}
void Reject(const tf::Bytes& b){Require(!image::DecodeImage(image::Format::tiff,b),"Invalid TIFF accepted");}
void Tests(){
    unsigned checked=0;
    for(bool little:{true,false})for(unsigned compression:{1u,32773u,8u,32946u})
        for(auto shape:{std::pair{3u,2u},{4u,2u},{1u,0u},{1u,1u},{2u,1u}}){
            const auto f=tf::Make(shape.first,shape.second,little,compression,compression==8?2:1);
            const auto d=image::DecodeImage(image::Format::tiff,f.bytes);
            Require(bool(d)&&d.image.width==7&&d.image.height==3&&d.image.rgba8==f.rgba,"TIFF sample/alpha/strip/predictor mismatch");++checked;
        }
    const auto f=tf::Make();
    auto w=image::EncodeImage(image::Format::tiff,7,3,f.rgba);
    Require(bool(w)&&!w.alphaFlattened&&w.bytes.size()==218+f.rgba.size(),"TIFF write layout wrong");
    Require(image::EncodeImage(image::Format::tiff,7,3,f.rgba).bytes==w.bytes,"TIFF encoding nondeterministic");
    auto d=image::DecodeImage(image::Format::tiff,w.bytes);Require(bool(d)&&d.image.rgba8==f.rgba,"TIFF writer lost RGBA");
    image::EncodeOptions out;out.maximumOutputBytes=w.bytes.size()-1;
    Require(image::EncodeImage(image::Format::tiff,7,3,f.rgba,out).error==image::EncodeError::output_limit,"TIFF header escaped output budget");
    out.maximumOutputBytes=w.bytes.size();Require(bool(image::EncodeImage(image::Format::tiff,7,3,f.rgba,out)),"Exact TIFF budget rejected");
    out.resourceProfile=image::RasterProfile::largeRaster;Require(!image::EncodeImage(image::Format::tiff,7,3,f.rgba,out),"TIFF broadened large profile");
    image::DecodeOptions in;in.maximumPixels=20;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF pixel limit ignored");
    in={};in.maximumSourceBytes=f.bytes.size()-1;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF byte limit ignored");
    in={};in.frameIndex=1;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF frame selection ignored");
    in={};in.resourceProfile=image::RasterProfile::largeRaster;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF decode broadened large profile");
    for(auto [tag,value]:{std::pair{256u,9000u},{274u,2u},{284u,2u},{338u,1u},{338u,0u},{259u,5u},{259u,7u},{278u,0u}}){
        auto b=f.bytes;tf::Scalar(b,tag,value);Reject(b);
    }
    for(std::size_t size=0;size<f.bytes.size();++size)Reject(tf::Bytes(f.bytes.begin(),f.bytes.begin()+size));
    auto b=f.bytes;tf::Put(b,22,256,2);Reject(b); // Duplicate directory tag.
    b=f.bytes;tf::Put(b,8+2+tf::Get(b,8,2)*12,8,4);Reject(b); // Another image.
    b=f.bytes;tf::Put(b,2,43,2);Reject(b); // BigTIFF.
    b=f.bytes;tf::Put(b,tf::Get(b,tf::Field(b,258)+8),16,2);Reject(b);
    b=f.bytes;tf::Put(b,tf::Get(b,tf::Field(b,273)+8),0,4);Reject(b); // Header overlap.
    b=f.bytes;const auto offsets=tf::Get(b,tf::Field(b,273)+8);tf::Put(b,offsets+4,tf::Get(b,offsets),4);Reject(b);
    b=f.bytes;tf::Put(b,tf::Field(b,273)+4,8193,4);Reject(b);
    b=f.bytes;tf::Put(b,tf::Field(b,284),34675,2);Reject(b); // Unsupported color profile.
    auto pack=tf::Make(4,2,true,32773);b=pack.bytes;
    const auto first=tf::Get(b,tf::Get(b,tf::Field(b,273)+8));b[first+1]=std::byte{127};Reject(b); // Row overflow.
    auto compressed=tf::Make(4,2,true,8,2);b=compressed.bytes;b.back()^=std::byte{1};Reject(b);
    // A replicate packet for every grayscale row, with explicit no-ops.
    auto repeat=tf::Make(1,1,true,32773,1,1);b=repeat.bytes;
    const auto positions=tf::Get(b,tf::Field(b,273)+8),sizes=tf::Get(b,tf::Field(b,279)+8);
    for(unsigned row=0;row<3;++row){const auto p=tf::Get(b,positions+row*4);b[p]=std::byte{250};b[p+1]=std::byte(30+row);tf::Put(b,sizes+row*4,2,4);}
    d=image::DecodeImage(image::Format::tiff,b);Require(bool(d),"PackBits replicate failed");
    for(unsigned row=0;row<3;++row)for(unsigned x=0;x<7;++x)Require(d.image.rgba8[(row*7+x)*4]==30+row,"PackBits repeat pixels differ");
    auto truncated_repeat=b;tf::Put(truncated_repeat,sizes,1,4);Reject(truncated_repeat);
    auto leftover=pack.bytes;tf::Put(leftover,tf::Field(leftover,278)+8,3,4);Reject(leftover);
    image::DecodeOptions bounded;bounded.maximumPixels=1024;bounded.maximumDimension=64;
    const tf::Bytes* seeds[]{&f.bytes,&pack.bytes,&compressed.bytes};
    for(const auto* seed:seeds)for(std::size_t p=0;p<seed->size();++p)
        for(unsigned bit:{1u,128u}){auto mutated=*seed;mutated[p]^=std::byte(bit);
            const auto result=image::DecodeImage(image::Format::tiff,mutated,bounded);
            if(result)Require(result.image.rgba8.size()==std::size_t(result.image.width)*result.image.height*4,"Mutated TIFF result extent differs");}
    std::cout<<"TIFF native profile passed: "<<checked<<" pixel variants, writer, malformed input and resource checks\n";
}
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-tiff-codec-tests-r6354\n";return 0;}
    try{
        if(argc==3&&std::string_view(argv[1])=="--browser-fixtures"){
            const fs::path root=argv[2];Require(root.is_absolute()&&!fs::exists(root),"Use a new absolute fixture directory");
            fs::create_directories(root.parent_path());Require(fs::create_directory(root),"Fixture destination already exists");
            constexpr unsigned width=320,height=192;std::vector<std::uint8_t> rgba(width*height*4);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto p=(y*width+x)*4;
                rgba[p]=std::uint8_t(x*3);rgba[p+1]=std::uint8_t(y*5);rgba[p+2]=std::uint8_t(x+y);
                rgba[p+3]=std::uint8_t((x/32+y/32)%3==0?0:(x/32+y/32)%3==1?127:255);}
            const auto png=image::EncodeImage(image::Format::png,width,height,rgba);
            const auto tiff=image::EncodeImage(image::Format::tiff,width,height,rgba);
            Require(bool(png)&&bool(tiff),"Browser reference encoding failed");
            Write(root/"color-grid.png",png.bytes);Write(root/"color-grid.tiff",tiff.bytes);Write(root/"reference.rgba",rgba);
            std::cout<<"TIFF browser references emitted\n";return 0;
        }
        if(argc==3&&std::string_view(argv[1])=="--emit"){
            const fs::path root=argv[2];Require(root.is_absolute()&&!fs::exists(root),"Use a new absolute fixture directory");
            fs::create_directories(root.parent_path());Require(fs::create_directory(root),"Fixture destination already exists");
            const auto f=tf::Make();const auto rgba=image::EncodeImage(image::Format::tiff,7,3,f.rgba);
            Require(bool(rgba),"Reference encoding failed");Write(root/"native-rgba.tiff",rgba.bytes);Write(root/"reference.rgba",f.rgba);
            for(auto [name,little,compression]:{std::tuple{"rgb-be.tiff",false,1u},std::tuple{"rgba-packbits.tiff",true,32773u},std::tuple{"rgba-deflate.tiff",true,8u}}){
                const auto fixture=tf::Make(name==std::string_view("rgb-be.tiff")?3:4,2,little,compression,compression==8?2:1);
                Write(root/name,fixture.bytes);Write(root/(std::string(name)+".rgba"),fixture.rgba);
            }
            std::cout<<"TIFF references emitted\n";return 0;
        }
        if(argc==4&&std::string_view(argv[1])=="--check"){
            const auto d=image::DecodeImage(image::Format::tiff,Read(argv[2]));Require(bool(d),image::ErrorMessage(d.issue.code).data());
            const auto raw=Read(argv[3]);Require(raw.size()==d.image.rgba8.size()&&std::equal(raw.begin(),raw.end(),d.image.rgba8.begin(),[](auto a,auto b){return std::to_integer<unsigned>(a)==b;}),"Independent TIFF sample mismatch");
            std::cout<<"Independent TIFF exact samples passed: "<<d.image.width<<"x"<<d.image.height<<"\n";return 0;
        }
        Require(argc==1,"Unknown TIFF test argument");Tests();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
