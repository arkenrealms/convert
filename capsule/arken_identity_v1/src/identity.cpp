#include "arken/identity/v1/identity.hpp"

#include <algorithm>

namespace arken::identity::v1
{
namespace
{
constexpr std::array<std::size_t, 4u> hyphen_offsets = {8u, 13u, 18u, 23u};

[[nodiscard]] bool is_hyphen_offset(std::size_t offset) noexcept
{
    return std::find(hyphen_offsets.begin(), hyphen_offsets.end(), offset) != hyphen_offsets.end();
}

[[nodiscard]] int decode_hex(char character, ParseMode mode) noexcept
{
    if(character >= '0' && character <= '9')
        return character - '0';
    if(character >= 'a' && character <= 'f')
        return 10 + character - 'a';
    if(mode == ParseMode::operator_input && character >= 'A' && character <= 'F')
        return 10 + character - 'A';
    return -1;
}

void write_timestamp(ArkenId& id, std::uint64_t millis) noexcept
{
    for(std::size_t index = 0u; index < 6u; ++index)
    {
        std::size_t const shift = (5u - index) * 8u;
        id.bytes[index] = static_cast<std::uint8_t>((millis >> shift) & 0xffu);
    }
}

[[nodiscard]] bool increment_suffix(ArkenId& id) noexcept
{
    for(std::size_t index = 16u; index-- > 9u;)
    {
        id.bytes[index] = static_cast<std::uint8_t>(id.bytes[index] + 1u);
        if(id.bytes[index] != 0u)
            return true;
    }

    std::uint8_t const variant_value = static_cast<std::uint8_t>(id.bytes[8u] & 0x3fu);
    if(variant_value != 0x3fu)
    {
        id.bytes[8u] = static_cast<std::uint8_t>(0x80u | (variant_value + 1u));
        return true;
    }
    id.bytes[8u] = 0x80u;

    id.bytes[7u] = static_cast<std::uint8_t>(id.bytes[7u] + 1u);
    if(id.bytes[7u] != 0u)
        return true;

    std::uint8_t const version_value = static_cast<std::uint8_t>(id.bytes[6u] & 0x0fu);
    if(version_value != 0x0fu)
    {
        id.bytes[6u] = static_cast<std::uint8_t>(0x70u | (version_value + 1u));
        return true;
    }
    id.bytes[6u] = 0x70u;
    return false;
}
}

Result<ArkenId, ParseError> parse(std::string_view text, ParseMode mode) noexcept
{
    if(text.size() != canonical_text_size)
        return Result<ArkenId, ParseError>::failure({0u, IdentityError::invalid_length});

    ArkenId parsed{};
    std::size_t byte_index = 0u;
    int high_nibble = -1;
    for(std::size_t offset = 0u; offset < text.size(); ++offset)
    {
        if(is_hyphen_offset(offset))
        {
            if(text[offset] != '-')
                return Result<ArkenId, ParseError>::failure({static_cast<std::uint32_t>(offset), IdentityError::invalid_hyphen});
            continue;
        }
        if(text[offset] == '-')
            return Result<ArkenId, ParseError>::failure({static_cast<std::uint32_t>(offset), IdentityError::invalid_hyphen});

        int const nibble = decode_hex(text[offset], mode);
        if(nibble < 0)
            return Result<ArkenId, ParseError>::failure({static_cast<std::uint32_t>(offset), IdentityError::invalid_character});
        if(high_nibble < 0)
        {
            high_nibble = nibble;
            continue;
        }
        parsed.bytes[byte_index++] = static_cast<std::uint8_t>((high_nibble << 4) | nibble);
        high_nibble = -1;
    }

    if(parsed.is_nil())
        return Result<ArkenId, ParseError>::success(parsed);
    if(parsed.is_maximum())
        return Result<ArkenId, ParseError>::failure({0u, IdentityError::maximum_not_allowed});
    if(!parsed.has_rfc_variant())
        return Result<ArkenId, ParseError>::failure({19u, IdentityError::invalid_variant});
    if(parsed.version() != 7u && parsed.version() != 8u)
        return Result<ArkenId, ParseError>::failure({14u, IdentityError::unsupported_version});
    return Result<ArkenId, ParseError>::success(parsed);
}

Result<FormatResult, IdentityError> format(ArkenId id, std::span<char> output, bool null_terminate) noexcept
{
    std::size_t const required_size = canonical_text_size + (null_terminate ? 1u : 0u);
    if(output.size() < required_size)
        return Result<FormatResult, IdentityError>::failure(
            IdentityError::buffer_too_small,
            {required_size, 0u});

    static constexpr char hex[] = "0123456789abcdef";
    std::size_t output_offset = 0u;
    for(std::size_t byte_index = 0u; byte_index < id.bytes.size(); ++byte_index)
    {
        if(byte_index == 4u || byte_index == 6u || byte_index == 8u || byte_index == 10u)
            output[output_offset++] = '-';
        output[output_offset++] = hex[id.bytes[byte_index] >> 4u];
        output[output_offset++] = hex[id.bytes[byte_index] & 0x0fu];
    }
    if(null_terminate)
        output[output_offset++] = '\0';
    return Result<FormatResult, IdentityError>::success({required_size, output_offset});
}

IdentityError validate_issued(ArkenId id) noexcept
{
    if(id.is_nil())
        return IdentityError::nil_not_allowed;
    if(id.is_maximum())
        return IdentityError::maximum_not_allowed;
    if(!id.has_rfc_variant())
        return IdentityError::invalid_variant;
    if(id.version() != 7u && id.version() != 8u)
        return IdentityError::unsupported_version;
    return IdentityError::none;
}

std::uint64_t uuid_v7_unix_millis(ArkenId id) noexcept
{
    if(id.version() != 7u || !id.has_rfc_variant())
        return 0u;
    std::uint64_t millis = 0u;
    for(std::size_t index = 0u; index < 6u; ++index)
        millis = (millis << 8u) | id.bytes[index];
    return millis;
}

V7Generator::V7Generator(V7Capabilities capabilities, V7Options options) noexcept
    : capabilities_(capabilities), options_(options)
{
}

Result<ArkenId, IdentityError> V7Generator::next() noexcept
{
    std::lock_guard lock(mutex_);
    return next_locked();
}

Result<std::size_t, IdentityError> V7Generator::fill(std::span<ArkenId> output) noexcept
{
    std::lock_guard lock(mutex_);
    for(std::size_t index = 0u; index < output.size(); ++index)
    {
        Result<ArkenId, IdentityError> result = next_locked();
        if(!result)
            return Result<std::size_t, IdentityError>::failure(result.error(), index);
        output[index] = result.value();
    }
    return Result<std::size_t, IdentityError>::success(output.size());
}

Result<ArkenId, IdentityError> V7Generator::next_locked() noexcept
{
    if(capabilities_.read_clock_millis == nullptr || capabilities_.fill_random == nullptr)
        return Result<ArkenId, IdentityError>::failure(IdentityError::invalid_argument);

    std::uint64_t observed_millis = 0u;
    if(!capabilities_.read_clock_millis(capabilities_.context, &observed_millis))
        return Result<ArkenId, IdentityError>::failure(IdentityError::clock_unavailable);
    if(observed_millis > 0xffffffffffffull)
        return Result<ArkenId, IdentityError>::failure(IdentityError::clock_unreliable);

    if(initialized_ && observed_millis < last_millis_)
    {
        std::uint64_t const rollback = last_millis_ - observed_millis;
        if(rollback > options_.maximum_clock_rollback_millis)
            return Result<ArkenId, IdentityError>::failure(IdentityError::clock_unreliable);
        observed_millis = last_millis_;
    }

    ArkenId next_id{};
    if(!initialized_ || observed_millis > last_millis_)
    {
        if(!capabilities_.fill_random(capabilities_.context, next_id.bytes.data(), next_id.bytes.size()))
            return Result<ArkenId, IdentityError>::failure(IdentityError::entropy_unavailable);
        write_timestamp(next_id, observed_millis);
        next_id.bytes[6u] = static_cast<std::uint8_t>(0x70u | (next_id.bytes[6u] & 0x0fu));
        next_id.bytes[8u] = static_cast<std::uint8_t>(0x80u | (next_id.bytes[8u] & 0x3fu));
    }
    else
    {
        next_id = last_id_;
        if(!increment_suffix(next_id))
            return Result<ArkenId, IdentityError>::failure(IdentityError::generation_capacity_exhausted);
    }

    last_id_ = next_id;
    last_millis_ = observed_millis;
    initialized_ = true;
    return Result<ArkenId, IdentityError>::success(next_id);
}
}
