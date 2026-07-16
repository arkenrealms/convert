#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <type_traits>

namespace arken::identity::v1
{
inline constexpr std::size_t arken_id_byte_size = 16u;
inline constexpr std::size_t canonical_text_size = 36u;

enum class IdentityError : std::uint32_t
{
    none = 0u,
    invalid_argument = 1u,
    invalid_length = 2u,
    invalid_character = 3u,
    invalid_hyphen = 4u,
    invalid_variant = 5u,
    unsupported_version = 6u,
    nil_not_allowed = 7u,
    maximum_not_allowed = 8u,
    buffer_too_small = 9u,
    entropy_unavailable = 10u,
    clock_unavailable = 11u,
    clock_unreliable = 12u,
    generation_capacity_exhausted = 13u,
    allocation_failed = 14u,
};

enum class ParseMode : std::uint8_t
{
    strict_canonical = 0u,
    operator_input = 1u,
};

template <typename Value, typename Error>
class Result final
{
public:
    [[nodiscard]] static constexpr Result success(Value value) noexcept
    {
        return Result(true, value, Error{});
    }

    [[nodiscard]] static constexpr Result failure(Error error, Value value = Value{}) noexcept
    {
        return Result(false, value, error);
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }
    // Failure values carry bounded diagnostics such as required size or partial batch count.
    [[nodiscard]] constexpr Value const& value() const noexcept { return value_; }
    [[nodiscard]] constexpr Value& value() noexcept { return value_; }
    [[nodiscard]] constexpr Error const& error() const noexcept { return error_; }

private:
    constexpr Result(bool has_value, Value value, Error error) noexcept
        : has_value_(has_value), value_(value), error_(error)
    {
    }

    bool has_value_ = false;
    Value value_{};
    Error error_{};
};

struct ArkenId final
{
    std::array<std::uint8_t, arken_id_byte_size> bytes{};

    [[nodiscard]] constexpr bool is_nil() const noexcept
    {
        for(std::uint8_t const byte : bytes)
        {
            if(byte != 0u)
                return false;
        }
        return true;
    }

    [[nodiscard]] constexpr bool is_maximum() const noexcept
    {
        for(std::uint8_t const byte : bytes)
        {
            if(byte != 0xffu)
                return false;
        }
        return true;
    }

    [[nodiscard]] constexpr std::uint8_t version() const noexcept
    {
        return static_cast<std::uint8_t>(bytes[6u] >> 4u);
    }

    [[nodiscard]] constexpr bool has_rfc_variant() const noexcept
    {
        return (bytes[8u] & 0xc0u) == 0x80u;
    }

    [[nodiscard]] constexpr std::strong_ordering operator<=>(ArkenId const& other) const noexcept
    {
        for(std::size_t index = 0u; index < bytes.size(); ++index)
        {
            if(bytes[index] < other.bytes[index])
                return std::strong_ordering::less;
            if(bytes[index] > other.bytes[index])
                return std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

    [[nodiscard]] constexpr bool operator==(ArkenId const&) const noexcept = default;
};

static_assert(sizeof(ArkenId) == arken_id_byte_size);
static_assert(alignof(ArkenId) == alignof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<ArkenId>);

struct ParseError final
{
    std::uint32_t offset = 0u;
    IdentityError code = IdentityError::none;
};

struct FormatResult final
{
    std::size_t required_size = canonical_text_size;
    std::size_t written_size = 0u;
};

[[nodiscard]] Result<ArkenId, ParseError> parse(
    std::string_view text,
    ParseMode mode = ParseMode::strict_canonical) noexcept;

[[nodiscard]] Result<FormatResult, IdentityError> format(
    ArkenId id,
    std::span<char> output,
    bool null_terminate = false) noexcept;

[[nodiscard]] IdentityError validate_issued(ArkenId id) noexcept;
[[nodiscard]] std::uint64_t uuid_v7_unix_millis(ArkenId id) noexcept;

template <typename Kind>
class TypedId final
{
public:
    constexpr TypedId() noexcept = default;
    explicit constexpr TypedId(ArkenId value) noexcept : value_(value) {}

    [[nodiscard]] constexpr ArkenId value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_nil() const noexcept { return value_.is_nil(); }
    [[nodiscard]] constexpr auto operator<=>(TypedId const&) const noexcept = default;

private:
    ArkenId value_{};
};

template <typename Kind>
inline constexpr bool typed_id_has_canonical_layout = sizeof(TypedId<Kind>) == sizeof(ArkenId)
    && std::is_trivially_copyable_v<TypedId<Kind>>;

using ClockMillisFunction = bool (*)(void* context, std::uint64_t* unix_millis);
using RandomFillFunction = bool (*)(void* context, std::uint8_t* output, std::size_t output_size);

struct V7Capabilities final
{
    void* context = nullptr;
    ClockMillisFunction read_clock_millis = nullptr;
    RandomFillFunction fill_random = nullptr;
};

struct V7Options final
{
    std::uint64_t maximum_clock_rollback_millis = 10'000u;
};

class V7Generator final
{
public:
    explicit V7Generator(V7Capabilities capabilities, V7Options options = {}) noexcept;

    V7Generator(V7Generator const&) = delete;
    V7Generator& operator=(V7Generator const&) = delete;

    [[nodiscard]] Result<ArkenId, IdentityError> next() noexcept;
    [[nodiscard]] Result<std::size_t, IdentityError> fill(std::span<ArkenId> output) noexcept;

private:
    [[nodiscard]] Result<ArkenId, IdentityError> next_locked() noexcept;

    V7Capabilities capabilities_{};
    V7Options options_{};
    std::mutex mutex_{};
    ArkenId last_id_{};
    std::uint64_t last_millis_ = 0u;
    bool initialized_ = false;
};
}
