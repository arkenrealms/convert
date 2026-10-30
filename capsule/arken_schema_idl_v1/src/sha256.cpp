#include "sha256.hpp"

#include "arken_schema_idl_v1/hash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arken::schema::idl::v1::detail
{
namespace
{
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t RotateRight(std::uint32_t value, std::uint32_t amount)
{
    return (value >> amount) | (value << (32U - amount));
}

class Sha256 final
{
public:
    void Update(std::string_view bytes)
    {
        byteCount_ += static_cast<std::uint64_t>(bytes.size());
        const auto* input = reinterpret_cast<const std::uint8_t*>(bytes.data());
        std::size_t remaining = bytes.size();
        while (remaining != 0)
        {
            const std::size_t copied = std::min(remaining, buffer_.size() - bufferedBytes_);
            std::memcpy(buffer_.data() + bufferedBytes_, input, copied);
            bufferedBytes_ += copied;
            input += copied;
            remaining -= copied;
            if (bufferedBytes_ == buffer_.size())
            {
                Transform(buffer_.data());
                bufferedBytes_ = 0;
            }
        }
    }

    std::string Finish()
    {
        const std::uint64_t bit_count = byteCount_ * 8U;
        buffer_[bufferedBytes_++] = 0x80U;
        if (bufferedBytes_ > 56U)
        {
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferedBytes_), buffer_.end(), 0U);
            Transform(buffer_.data());
            bufferedBytes_ = 0;
        }
        std::fill(
            buffer_.begin() + static_cast<std::ptrdiff_t>(bufferedBytes_),
            buffer_.begin() + 56,
            0U
        );
        for (std::size_t index = 0; index < 8; ++index)
        {
            const std::size_t shift = (7U - index) * 8U;
            buffer_[56U + index] = static_cast<std::uint8_t>((bit_count >> shift) & 0xffU);
        }
        Transform(buffer_.data());

        constexpr char hexadecimal[] = "0123456789abcdef";
        std::string output(64, '0');
        for (std::size_t word = 0; word < state_.size(); ++word)
        {
            for (std::size_t byte = 0; byte < 4; ++byte)
            {
                const auto value = static_cast<std::uint8_t>(
                    (state_[word] >> ((3U - byte) * 8U)) & 0xffU
                );
                const std::size_t offset = (word * 8U) + (byte * 2U);
                output[offset] = hexadecimal[value >> 4U];
                output[offset + 1U] = hexadecimal[value & 0x0fU];
            }
        }
        return output;
    }

private:
    void Transform(const std::uint8_t* block)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            const std::size_t offset = index * 4U;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t index = 16; index < words.size(); ++index)
        {
            const std::uint32_t sigma_zero = RotateRight(words[index - 15U], 7U) ^
                RotateRight(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const std::uint32_t sigma_one = RotateRight(words[index - 2U], 17U) ^
                RotateRight(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + sigma_zero + words[index - 7U] + sigma_one;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const std::uint32_t sum_one = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t first = h + sum_one + choose + kRoundConstants[index] + words[index];
            const std::uint32_t sum_zero = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = sum_zero + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t byteCount_ = 0;
    std::size_t bufferedBytes_ = 0;
};
}

std::string Sha256Hex(std::string_view bytes)
{
    Sha256 hasher;
    hasher.Update(bytes);
    return hasher.Finish();
}
}

namespace arken::schema::idl::v1
{
std::string StableSha256Hex(std::string_view bytes)
{
    return detail::Sha256Hex(bytes);
}
}
