#include <libheif/heif.h>
#include <libde265/de265.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#if defined(__wasm32__)
#include <wasi/api.h>
#endif
#if !defined(__wasm32__)
#include <fstream>
#include <iterator>
#include <vector>
#endif

// One callable admission check is shared by the native CLI and browser Wasm.
// This does not advertise HEIF as an admitted Arken image format.
extern "C" int arken_heif_dependency_check() {
    if(std::strcmp(heif_get_version(),"1.23.7")||std::strcmp(de265_get_version(),"1.1.3"))return 1;
    const auto initialized=heif_init(nullptr);
    if(initialized.code!=heif_error_Ok)return 2;
    int result=0;
    if(!heif_have_decoder_for_format(heif_compression_HEVC)
        ||heif_have_encoder_for_format(heif_compression_HEVC)
        ||heif_have_decoder_for_format(heif_compression_AV1))result=3;
    auto* context=heif_context_alloc();
    if(!context){heif_deinit();return 4;}
    auto limits=*heif_context_get_security_limits(context);
    limits.max_image_size_pixels=1024*1024;
    limits.max_memory_block_size=16*1024*1024;
    limits.max_total_memory=32*1024*1024;
    limits.max_number_of_tiles=64;
    limits.max_items=128;
    limits.max_sequence_frames=1;
    if(heif_context_set_security_limits(context,&limits).code!=heif_error_Ok)result=5;
    heif_context_set_max_decoding_threads(context,0);
    constexpr unsigned char invalid[]={0,0,0,8,'f','t','y','p'};
    if(heif_context_read_from_memory_without_copy(context,invalid,sizeof(invalid),nullptr).code==heif_error_Ok)result=6;
#if defined(__wasm32__)
    if(heif_context_read_from_file(context,"not-an-admitted-file",nullptr).code!=heif_error_Unsupported_feature)result=7;
    __wasi_size_t read=123;
    if(__wasi_fd_read(0,nullptr,0,&read)!=__WASI_ERRNO_BADF||read!=0)result=8;
#endif
    heif_context_free(context);heif_deinit();return result;
}

namespace {
unsigned width=0,height=0;
std::uint64_t pixelFingerprint=0;
}
extern "C" unsigned arken_heif_dependency_width(){return width;}
extern "C" unsigned arken_heif_dependency_height(){return height;}
extern "C" std::uint64_t arken_heif_dependency_pixels(){return pixelFingerprint;}
// Dependency parity only: accepted product color/selection policy is separate.
extern "C" int arken_heif_dependency_decode(const unsigned char* bytes,unsigned length){
    width=height=0;pixelFingerprint=0;
    if(!bytes||!length||length>32u*1024*1024)return 10;
    if(heif_init(nullptr).code!=heif_error_Ok)return 11;
    struct End{~End(){heif_deinit();}} end;
    std::unique_ptr<heif_context,decltype(&heif_context_free)> context(heif_context_alloc(),heif_context_free);
    if(!context)return 12;
    auto limits=*heif_context_get_security_limits(context.get());
    limits.max_image_size_pixels=8ull*1024*1024;
    limits.max_memory_block_size=64ull*1024*1024;
    limits.max_total_memory=128ull*1024*1024;
    limits.max_number_of_tiles=256;limits.max_items=1024;limits.max_sequence_frames=1;
    if(heif_context_set_security_limits(context.get(),&limits).code!=heif_error_Ok)return 13;
    heif_context_set_max_decoding_threads(context.get(),0);
    if(heif_context_read_from_memory_without_copy(context.get(),bytes,length,nullptr).code!=heif_error_Ok)return 14;
    heif_image_handle* handle=nullptr;
    if(heif_context_get_primary_image_handle(context.get(),&handle).code!=heif_error_Ok)return 15;
    std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)> owner(handle,heif_image_handle_release);
    std::unique_ptr<heif_decoding_options,decltype(&heif_decoding_options_free)> options(heif_decoding_options_alloc(),heif_decoding_options_free);
    if(!options)return 16;
    options->strict_decoding=1;options->num_codec_threads=1;
    heif_image* image=nullptr;
    const auto decoded=heif_decode_image(handle,&image,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,options.get());
    if(decoded.code!=heif_error_Ok)return 17;
    std::unique_ptr<heif_image,decltype(&heif_image_release)> pixels(image,heif_image_release);
    const auto w=heif_image_get_width(image,heif_channel_interleaved),h=heif_image_get_height(image,heif_channel_interleaved);
    int stride=0;const auto* data=heif_image_get_plane_readonly(image,heif_channel_interleaved,&stride);
    if(w<=0||h<=0||w>8192||h>8192||std::uint64_t(w)*h>limits.max_image_size_pixels||!data||stride<w*4)return 18;
    width=unsigned(w);height=unsigned(h);pixelFingerprint=14695981039346656037ull;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width*4;++x){pixelFingerprint^=data[std::size_t(y)*std::size_t(stride)+x];pixelFingerprint*=1099511628211ull;}
    return 0;
}

#if !defined(__wasm32__)
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--version")){
        std::puts("arken-heif-dependency-check r6364");return 0;
    }
    if(argc==3&&!std::strcmp(argv[1],"--decode")){
        std::ifstream input(argv[2],std::ios::binary|std::ios::ate);
        if(!input||input.tellg()<=0||input.tellg()>32*1024*1024)return 65;
        std::vector<unsigned char> bytes(std::size_t(input.tellg()));input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));if(!input)return 66;
        const auto result=arken_heif_dependency_decode(bytes.data(),unsigned(bytes.size()));
        std::printf("{\"status\":\"%s\",\"code\":%d,\"width\":%u,\"height\":%u,\"rgbaFnv1a64\":\"%016llx\"}\n",result?"failed":"passed",result,width,height,static_cast<unsigned long long>(pixelFingerprint));return result;
    }
    if(argc!=1){std::fputs("Unexpected argument\n",stderr);return 64;}
    const auto result=arken_heif_dependency_check();
    std::printf("HEIF dependency admission: %s (%d)\n",result?"failed":"passed",result);return result;
}
#endif
