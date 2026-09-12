#include "arken_file_conversion_v1/api.hpp"
#include <new>
#define EXPORT __attribute__((used, visibility("default")))
namespace conversion = arken::file_conversion::v1;
namespace {
std::vector<std::byte> input;
conversion::ImageResult result;
bool prepared = false;
bool active = false;
void Clear() noexcept {
    active = false;
    prepared = false;
    std::vector<std::byte>().swap(input);
    result = {};
}
}
// Private host ABI: a request is prepared, filled and converted synchronously.
// The host copies output before awaiting I/O and releases the instance's data.
// None of these memory addresses are exposed as customer file identifiers.
extern "C" EXPORT std::uint32_t arken_conversion_prepare(std::uint32_t bytes) noexcept {
    if (active) return 0;
    Clear();
    if (!bytes || bytes > conversion::maximumSourceBytes) {
        result.error = conversion::Error::input_limit; return 0;
    }
    try { input.resize(bytes); active = true; prepared = true; return 1; }
    catch (const std::bad_alloc&) { result.error = conversion::Error::allocation_failed; return 0; }
    catch (...) { result.error = conversion::Error::internal_failure; return 0; }
}
extern "C" EXPORT std::byte* arken_conversion_input() noexcept { return prepared ? input.data() : nullptr; }
extern "C" EXPORT std::uint32_t arken_conversion_run(std::uint32_t from, std::uint32_t to,
    std::uint32_t quality, std::uint32_t alpha) noexcept {
    if (!prepared) return static_cast<std::uint32_t>(conversion::Error::input_not_prepared);
    else {
        prepared = false;
        result = conversion::ConvertImage(input, {static_cast<conversion::image::Format>(from),
            static_cast<conversion::image::Format>(to), quality, static_cast<conversion::image::AlphaPolicy>(alpha)});
    }
    std::vector<std::byte>().swap(input);
    return static_cast<std::uint32_t>(result.error);
}
extern "C" EXPORT const std::byte* arken_conversion_output() noexcept { return result.bytes.data(); }
extern "C" EXPORT std::uint32_t arken_conversion_output_size() noexcept { return static_cast<std::uint32_t>(result.bytes.size()); }
extern "C" EXPORT std::uint32_t arken_conversion_width() noexcept { return result.width; }
extern "C" EXPORT std::uint32_t arken_conversion_height() noexcept { return result.height; }
extern "C" EXPORT std::uint32_t arken_conversion_alpha_flattened() noexcept { return result.alphaFlattened; }
extern "C" EXPORT std::uint32_t arken_conversion_decode_error() noexcept { return static_cast<std::uint32_t>(result.decodeError); }
extern "C" EXPORT std::uint32_t arken_conversion_encode_error() noexcept { return static_cast<std::uint32_t>(result.encodeError); }
extern "C" EXPORT void arken_conversion_release() noexcept { Clear(); }
extern "C" EXPORT std::uint32_t arken_conversion_abi_version() noexcept { return 1; }
