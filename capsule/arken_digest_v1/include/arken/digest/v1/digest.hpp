#pragma once

#include "arken/identity/v1/identity.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace arken::digest::v1
{
inline constexpr std::size_t sha256_digest_size = 32u;
inline constexpr std::size_t sha256_canonical_text_size = 71u;

struct DigestAlgorithmIdentityKind;
using DigestAlgorithmId = arken::identity::v1::TypedId<DigestAlgorithmIdentityKind>;
inline constexpr DigestAlgorithmId sha256_algorithm_id{arken::identity::v1::ArkenId{{
    0x73u, 0x68u, 0x61u, 0x32u, 0x35u, 0x36u, 0x80u, 0x01u,
    0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u,
}}};

struct Sha256Digest final
{
    std::array<std::uint8_t, sha256_digest_size> bytes{};
    [[nodiscard]] constexpr bool operator==(Sha256Digest const&) const noexcept = default;
};

struct Sha256ContentAddress final
{
    Sha256Digest digest{};
    std::uint64_t byte_length = 0u;
    [[nodiscard]] constexpr bool operator==(Sha256ContentAddress const&) const noexcept = default;
};

class Sha256Hasher final
{
  public:
    Sha256Hasher() noexcept;

    [[nodiscard]] bool update(std::span<std::byte const> bytes) noexcept;
    [[nodiscard]] std::optional<Sha256Digest> finalize() const noexcept;

  private:
    void compress(std::byte const* block) noexcept;

    std::array<std::uint32_t, 8u> state_{};
    std::array<std::byte, 64u> buffer_{};
    std::uint64_t total_bytes_ = 0u;
    std::size_t buffered_bytes_ = 0u;
    bool valid_ = true;
};

[[nodiscard]] std::optional<Sha256Digest> sha256(
    std::span<std::byte const> bytes) noexcept;
[[nodiscard]] std::optional<Sha256ContentAddress> sha256_address(
    std::span<std::byte const> bytes) noexcept;
[[nodiscard]] bool format_sha256(
    Sha256Digest const& digest,
    std::span<char> output) noexcept;
}
