#include "arken/digest/v1/digest.hpp"
#include <algorithm>

namespace arken::digest::v1
{
namespace
{
template<class T> struct ClearOnExit final
{
    T& value;
    ~ClearOnExit()
    {
        auto* bytes = reinterpret_cast<volatile unsigned char*>(&value);
        for(std::size_t index = 0; index < sizeof(T); ++index) bytes[index] = 0;
    }
};
}
std::optional<Sha256Digest> hmac_sha256(
    std::span<std::byte const> key, std::span<std::byte const> message) noexcept
{
    std::array<std::byte, 64> innerPad{}, outerPad{};
    ClearOnExit clearInner{innerPad}, clearOuter{outerPad};
    if(key.size() > innerPad.size())
    {
        auto reduced = sha256(key);
        if(!reduced) return std::nullopt;
        ClearOnExit clearReduced{*reduced};
        const auto bytes = std::as_bytes(std::span(reduced->bytes));
        std::copy(bytes.begin(), bytes.end(), innerPad.begin());
    }
    else std::copy(key.begin(), key.end(), innerPad.begin());
    outerPad = innerPad;
    for(auto& byte : innerPad) byte ^= std::byte{0x36};
    for(auto& byte : outerPad) byte ^= std::byte{0x5c};
    Sha256Hasher inner, outer;
    ClearOnExit clearInnerHash{inner}, clearOuterHash{outer};
    if(!inner.update(innerPad) || !inner.update(message)) return std::nullopt;
    auto innerDigest = inner.finalize();
    if(!innerDigest) return std::nullopt;
    ClearOnExit clearDigest{*innerDigest};
    if(!outer.update(outerPad) ||
       !outer.update(std::as_bytes(std::span(innerDigest->bytes)))) return std::nullopt;
    return outer.finalize();
}
}
