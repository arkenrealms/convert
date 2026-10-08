#include "arken/digest/v1/digest.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace arken::digest::v1
{
namespace
{
constexpr std::array<std::uint32_t, 64u> round_constants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

[[nodiscard]] constexpr std::uint32_t load_be32(std::byte const* bytes) noexcept
{
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24u)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16u)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8u)
        | static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

template<std::size_t Round>
void compress_round(std::array<std::uint32_t, 16u>& words,
                    std::array<std::uint32_t, 8u>& working) noexcept
{
    constexpr auto slot = Round & 15u;
    if constexpr(Round >= 16u)
    {
        auto const first_word = words[(Round + 1u) & 15u];
        auto const second_word = words[(Round + 14u) & 15u];
        auto const first = std::rotr(first_word, 7) ^ std::rotr(first_word, 18)
            ^ (first_word >> 3u);
        auto const second = std::rotr(second_word, 17) ^ std::rotr(second_word, 19)
            ^ (second_word >> 10u);
        words[slot] += first + words[(Round + 9u) & 15u] + second;
    }
    // Rotate logical a..h through constant physical slots. The two writes become
    // next-round e and a; after 64 rounds the physical order is a..h again.
    auto const a = working[(64u - Round) & 7u];
    auto const b = working[(65u - Round) & 7u];
    auto const c = working[(66u - Round) & 7u];
    auto& d = working[(67u - Round) & 7u];
    auto const e = working[(68u - Round) & 7u];
    auto const f = working[(69u - Round) & 7u];
    auto const g = working[(70u - Round) & 7u];
    auto& h = working[(71u - Round) & 7u];
    auto const sum_one = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    auto const choose = (e & f) ^ (~e & g);
    auto const temporary_one = h + sum_one + choose + round_constants[Round] + words[slot];
    auto const sum_zero = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    auto const majority = (a & b) ^ (a & c) ^ (b & c);
    d += temporary_one;
    h = temporary_one + sum_zero + majority;
}

template<std::size_t... Rounds>
void compress_rounds(std::array<std::uint32_t, 16u>& words,
                     std::array<std::uint32_t, 8u>& working,
                     std::index_sequence<Rounds...>) noexcept
{
    (compress_round<Rounds>(words, working), ...);
}
}

Sha256Hasher::Sha256Hasher() noexcept
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}
{
}

void Sha256Hasher::compress(std::byte const* block) noexcept
{
    std::array<std::uint32_t, 16u> words{};
    for(std::size_t index = 0u; index < words.size(); ++index)
        words[index] = load_be32(block + index * 4u);

    auto working = state_;
    compress_rounds(words, working, std::make_index_sequence<64u>{});
    for(std::size_t index = 0u; index < state_.size(); ++index)
        state_[index] += working[index];
}

bool Sha256Hasher::update(std::span<std::byte const> bytes) noexcept
{
    constexpr auto maximum_message_bytes = std::numeric_limits<std::uint64_t>::max() / 8u;
    if(!valid_ || bytes.size() > maximum_message_bytes - total_bytes_)
    {
        valid_ = false;
        return false;
    }
    total_bytes_ += static_cast<std::uint64_t>(bytes.size());
    std::size_t offset = 0u;
    if(buffered_bytes_ != 0u)
    {
        auto const count = std::min(buffer_.size() - buffered_bytes_, bytes.size());
        std::memcpy(buffer_.data() + buffered_bytes_, bytes.data(), count);
        buffered_bytes_ += count;
        offset += count;
        if(buffered_bytes_ == buffer_.size())
        {
            compress(buffer_.data());
            buffered_bytes_ = 0u;
        }
    }
    while(bytes.size() - offset >= buffer_.size())
    {
        compress(bytes.data() + offset);
        offset += buffer_.size();
    }
    if(offset < bytes.size())
    {
        buffered_bytes_ = bytes.size() - offset;
        std::memcpy(buffer_.data(), bytes.data() + offset, buffered_bytes_);
    }
    return true;
}

std::optional<Sha256Digest> Sha256Hasher::finalize() const noexcept
{
    if(!valid_)
        return std::nullopt;
    auto copy = *this;
    auto size = copy.buffered_bytes_;
    copy.buffer_[size++] = std::byte{0x80u};
    if(size > 56u)
    {
        std::fill(copy.buffer_.begin() + static_cast<std::ptrdiff_t>(size), copy.buffer_.end(), std::byte{0u});
        copy.compress(copy.buffer_.data());
        size = 0u;
    }
    std::fill(copy.buffer_.begin() + static_cast<std::ptrdiff_t>(size),
              copy.buffer_.begin() + 56, std::byte{0u});
    auto const bit_length = copy.total_bytes_ * 8u;
    for(std::size_t index = 0u; index < 8u; ++index)
        copy.buffer_[56u + index] = static_cast<std::byte>((bit_length >> ((7u - index) * 8u)) & 0xffu);
    copy.compress(copy.buffer_.data());

    Sha256Digest digest{};
    for(std::size_t index = 0u; index < copy.state_.size(); ++index)
    {
        auto const value = copy.state_[index];
        digest.bytes[index * 4u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
        digest.bytes[index * 4u + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
        digest.bytes[index * 4u + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        digest.bytes[index * 4u + 3u] = static_cast<std::uint8_t>(value & 0xffu);
    }
    return digest;
}

std::optional<Sha256Digest> sha256(std::span<std::byte const> bytes) noexcept
{
    Sha256Hasher hasher;
    if(!hasher.update(bytes))
        return std::nullopt;
    return hasher.finalize();
}

std::optional<Sha256ContentAddress> sha256_address(std::span<std::byte const> bytes) noexcept
{
    auto digest = sha256(bytes);
    if(!digest)
        return std::nullopt;
    return Sha256ContentAddress{*digest, static_cast<std::uint64_t>(bytes.size())};
}

bool constant_time_equal(Sha256Digest left, Sha256Digest right) noexcept
{
    volatile std::uint32_t difference = 0u;
    for(std::size_t index = 0u; index < left.bytes.size(); ++index)
        difference = difference |
            static_cast<std::uint32_t>(left.bytes[index] ^ right.bytes[index]);
    return difference == 0u;
}

std::optional<Sha256Digest> parse_sha256(std::string_view text) noexcept
{
    if(text.size()!=sha256_canonical_text_size || !text.starts_with("sha256:"))return std::nullopt;
    const auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    Sha256Digest value;
    for(std::size_t i=0;i<value.bytes.size();++i){
        const auto high=digit(text[7+i*2]),low=digit(text[8+i*2]);
        if(high<0||low<0)return std::nullopt;
        value.bytes[i]=static_cast<std::uint8_t>((high<<4)|low);
    }
    return value;
}

bool format_sha256(Sha256Digest const& digest, std::span<char> output) noexcept
{
    if(output.size() < sha256_canonical_text_size)
        return false;
    static constexpr char prefix[] = "sha256:";
    static constexpr char hex[] = "0123456789abcdef";
    std::copy_n(prefix, sizeof(prefix) - 1u, output.begin());
    std::size_t offset = sizeof(prefix) - 1u;
    for(auto const byte : digest.bytes)
    {
        output[offset++] = hex[byte >> 4u];
        output[offset++] = hex[byte & 0x0fu];
    }
    return true;
}
}
