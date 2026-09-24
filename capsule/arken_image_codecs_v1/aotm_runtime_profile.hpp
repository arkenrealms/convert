#pragma once
#include "aotm_rgba.hpp"
#include <array>
#include <string>

namespace arken::image::v1 {
using AotmRuntimeIdentity=std::array<std::uint8_t,32>;
struct AotmRuntimeIdentities {AotmRuntimeIdentity logical{},representation{};};
[[nodiscard]] AotmRuntimeIdentities DeriveAotmRuntimeIdentities(std::span<const std::uint8_t>) noexcept;
enum class AotmRuntimeError {none,invalid_argument,invalid_container,count_mismatch,invalid_rgba};
struct AotmRuntimeProfile {
    std::uint32_t profile_flags{},chunk_count{},runtime_profile_count{},source_texture_count{},rgba_package_count{};
    std::uint32_t package_byte_length{},width{},height{},pixel_byte_length{},format_code{},quality_class{},flags{};
    std::uint64_t content_hash64{},package_hash64{};
    AotmRuntimeIdentity logical_texture_id{},representation_id{};
};
struct AotmRuntimeReadResult {
    AotmRuntimeError error=AotmRuntimeError::none;
    std::string message;
    AotmRgbaError rgba_error=AotmRgbaError::none;
    AotmRuntimeProfile value;
    explicit operator bool()const noexcept{return error==AotmRuntimeError::none;}
};
// A validated AO v1 source-free RGBA8 texture, not a raw RGBA package.
// Source-retained imports must still use their source/fidelity validator.
[[nodiscard]] AotmRuntimeReadResult ReadAotmRuntimeProfile(std::span<const std::uint8_t>) noexcept;
}
