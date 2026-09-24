#include "arken_image_codecs_v1/aotm_runtime_profile.hpp"
#include "arken/ao/core/v1/core.hpp"
#include "arken/digest/v1/digest.hpp"
#include <algorithm>
#include <limits>

namespace arken::image::v1 {
namespace {
namespace core=arken::ao::core::v1;
constexpr auto profile_type=core::fourcc('R','T','P','F');
constexpr auto package_type=core::fourcc('R','G','B','A');
constexpr auto source_type=core::fourcc('S','R','C','T');
std::uint32_t u32(std::span<const std::uint8_t> bytes,std::size_t at){
    return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
}
AotmRuntimeIdentity identity(std::string_view domain,std::span<const std::uint8_t> bytes) noexcept {
    arken::digest::v1::Sha256Hasher hash;const std::uint8_t separator=0;
    if(!hash.update(std::as_bytes(std::span(domain)))||!hash.update(std::as_bytes(std::span(&separator,1)))||!hash.update(std::as_bytes(bytes)))return {};
    const auto digest=hash.finalize();return digest?digest.value().bytes:AotmRuntimeIdentity{};
}
bool zero(const AotmRuntimeIdentity& v){return std::all_of(v.begin(),v.end(),[](auto n){return n==0;});}
}
AotmRuntimeIdentities DeriveAotmRuntimeIdentities(std::span<const std::uint8_t> bytes) noexcept {
    return {identity("arken.aotm.logical-texture.v1",bytes),identity("arken.aotm.rgba8-representation.v1",bytes)};
}
AotmRuntimeReadResult ReadAotmRuntimeProfile(std::span<const std::uint8_t> bytes) noexcept {
    using Error=AotmRuntimeError;
    const auto fail=[](Error e,std::string text){AotmRuntimeReadResult out;out.error=e;out.message=std::move(text);return out;};
    try {
        if(bytes.empty())return fail(Error::invalid_argument,"AOTM runtime profile requires bytes");
        const auto read=core::read_container(bytes.data(),bytes.size());
        if(read.status!=core::Status::ok)return fail(Error::invalid_container,read.message);
        if(read.container.kind!=core::FileKind::aotm)return fail(Error::invalid_container,"AOTM runtime profile received another container kind");
        AotmRuntimeReadResult out;auto& value=out.value;
        value.chunk_count=std::uint32_t(read.container.chunks.size());value.content_hash64=read.container.content_hash64;
        value.runtime_profile_count=core::count_chunks(read.container,profile_type);
        value.rgba_package_count=core::count_chunks(read.container,package_type);
        value.source_texture_count=core::count_chunks(read.container,source_type);
        if(value.runtime_profile_count!=1||value.rgba_package_count!=1)return fail(Error::count_mismatch,"AOTM requires exactly one RTPF and one RGBA chunk");
        if(value.source_texture_count)return fail(Error::invalid_container,"AOTM source-free runtime profile must not retain SRCT");
        const auto* profile=core::find_chunk(read.container,profile_type);const auto* package=core::find_chunk(read.container,package_type);
        const auto layout=[](auto* c){return c&&c->version==1&&c->compression==core::kCompressionNone&&c->encoding==core::kEncodingBytes&&c->element_count==1;};
        if(!layout(profile)||!layout(package)||profile->payload.size()!=128||package->payload.size()>std::numeric_limits<std::uint32_t>::max())
            return fail(Error::invalid_container,"AOTM source-free runtime profile chunk layout is invalid");
        if(u32(profile->payload,0)!=core::fourcc('A','T','R','P')||u32(profile->payload,4)!=1||u32(profile->payload,8)!=3)
            return fail(Error::invalid_container,"AOTM RTPF identity, version or flags are invalid");
        value.profile_flags=3;
        std::copy_n(profile->payload.begin()+16,32,value.logical_texture_id.begin());
        std::copy_n(profile->payload.begin()+48,32,value.representation_id.begin());
        if(zero(value.logical_texture_id)||zero(value.representation_id))return fail(Error::invalid_container,"AOTM RTPF identities must be nonzero SHA-256 values");
        const auto rgba=ReadAotmRgbaPackage(package->payload);
        if(!rgba){auto failed=fail(Error::invalid_rgba,std::string(rgba.message));failed.rgba_error=rgba.error;return failed;}
        value.package_byte_length=std::uint32_t(package->payload.size());value.package_hash64=package->hash64;
        value.width=rgba.value.width;value.height=rgba.value.height;value.pixel_byte_length=rgba.value.pixelByteLength;
        value.format_code=rgba.value.formatCode;value.quality_class=rgba.value.qualityClass;value.flags=rgba.value.flags;
        if(u32(profile->payload,80)!=value.width||u32(profile->payload,84)!=value.height||u32(profile->payload,88)!=value.pixel_byte_length||
            u32(profile->payload,92)!=value.format_code||u32(profile->payload,96)!=value.quality_class||u32(profile->payload,100)!=value.flags||
            u32(profile->payload,104)!=value.package_byte_length)return fail(Error::count_mismatch,"AOTM RTPF descriptor differs from RGBA representation");
        const auto expected=DeriveAotmRuntimeIdentities(package->payload);
        if(value.logical_texture_id!=expected.logical||value.representation_id!=expected.representation)
            return fail(Error::invalid_container,"AOTM RTPF SHA-256 identities differ from RGBA package");
        return out;
    }catch(std::exception const& e){return fail(Error::invalid_container,e.what());}
    catch(...){return fail(Error::invalid_container,"AOTM runtime profile allocation failed");}
}
}
