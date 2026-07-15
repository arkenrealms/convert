#include "arken_schema_cpp_v1/binding.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace arken::schema::cpp::v1
{
namespace
{
template <typename Value>
std::optional<Value> ParseInteger(std::string_view text)
{
    Value parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return parsed;
}

std::optional<double> ParseReal(std::string_view text)
{
    const auto parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false, false);
    if (parsed.is_discarded() || !parsed.is_number()) return std::nullopt;
    const double value = parsed.get<double>();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

bool IsLowercase(std::string_view text)
{
    return std::none_of(text.begin(), text.end(), [](char character) {
        return character >= 'A' && character <= 'Z';
    });
}

bool IsTrimmed(std::string_view text)
{
    const auto whitespace = [](char character) {
        return character == ' ' || character == '\t' || character == '\n' ||
            character == '\r' || character == '\f' || character == '\v';
    };
    return text.empty() || (!whitespace(text.front()) && !whitespace(text.back()));
}

bool IsHex(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f') ||
            (character >= 'A' && character <= 'F');
    });
}

bool IsCanonicalText(std::string_view text, normalization_kind normalization)
{
    switch (normalization)
    {
        case normalization_kind::none: return true;
        case normalization_kind::trim: return IsTrimmed(text);
        case normalization_kind::lowercase: return IsLowercase(text);
        case normalization_kind::lowercase_trim:
            return IsLowercase(text) && IsTrimmed(text);
        case normalization_kind::ethereum_address:
            return text.size() == 42 && text.starts_with("0x") &&
                IsLowercase(text) && IsHex(text.substr(2));
        case normalization_kind::url:
            return IsTrimmed(text) &&
                (text.starts_with("http://") || text.starts_with("https://"));
    }
    return false;
}

std::optional<std::size_t> Utf8ScalarLength(std::string_view text)
{
    const auto continuation = [](unsigned char byte) {
        return (byte & UINT8_C(0xc0)) == UINT8_C(0x80);
    };
    std::size_t scalar_count = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto first = static_cast<unsigned char>(text[offset]);
        if (first == 0) break;
        std::size_t width = 0;
        if (first <= UINT8_C(0x7f))
        {
            width = 1;
        }
        else if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf))
        {
            width = 2;
        }
        else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef))
        {
            width = 3;
        }
        else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4))
        {
            width = 4;
        }
        else
        {
            return std::nullopt;
        }
        if (offset + width > text.size()) return std::nullopt;
        for (std::size_t index = 1; index < width; ++index)
            if (!continuation(static_cast<unsigned char>(text[offset + index]))) return std::nullopt;
        if (width == 3)
        {
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            if ((first == UINT8_C(0xe0) && second < UINT8_C(0xa0)) ||
                (first == UINT8_C(0xed) && second >= UINT8_C(0xa0)))
            {
                return std::nullopt;
            }
        }
        if (width == 4)
        {
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            if ((first == UINT8_C(0xf0) && second < UINT8_C(0x90)) ||
                (first == UINT8_C(0xf4) && second > UINT8_C(0x8f)))
            {
                return std::nullopt;
            }
        }
        offset += width;
        ++scalar_count;
    }
    return scalar_count;
}

std::optional<std::size_t> LogicalTextLength(schema_field_kind kind, std::size_t byte_count)
{
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    switch (kind)
    {
        case schema_field_kind::object_id:
        case schema_field_kind::hash_hex:
        case schema_field_kind::token_hash:
            return byte_count <= maximum / 2u ? std::optional<std::size_t>(byte_count * 2u) : std::nullopt;
        case schema_field_kind::ethereum_address:
            return byte_count <= (maximum - 2u) / 2u
                ? std::optional<std::size_t>(byte_count * 2u + 2u)
                : std::nullopt;
        default: return std::nullopt;
    }
}

std::size_t DecimalPrecision(const std::array<std::byte, 16>& coefficient)
{
    std::array<std::uint8_t, 16> magnitude{};
    for (std::size_t index = 0; index < coefficient.size(); ++index)
        magnitude[index] = std::to_integer<std::uint8_t>(coefficient[index]);

    if ((magnitude.front() & UINT8_C(0x80)) != 0)
    {
        for (auto& byte : magnitude) byte = static_cast<std::uint8_t>(~byte);
        for (std::size_t index = magnitude.size(); index > 0; --index)
        {
            if (++magnitude[index - 1] != 0) break;
        }
    }

    if (std::all_of(magnitude.begin(), magnitude.end(), [](auto byte) { return byte == 0; }))
        return 1;

    std::size_t digits = 0;
    while (std::any_of(magnitude.begin(), magnitude.end(), [](auto byte) { return byte != 0; }))
    {
        std::uint16_t carry = 0;
        for (auto& byte : magnitude)
        {
            const std::uint16_t current = static_cast<std::uint16_t>((carry << 8U) | byte);
            byte = static_cast<std::uint8_t>(current / 10U);
            carry = static_cast<std::uint16_t>(current % 10U);
        }
        ++digits;
    }
    return digits;
}

std::optional<std::array<std::byte, 16>> ParseDecimalCoefficient(
    std::string_view text,
    std::uint32_t scale
)
{
    if (text.empty()) return std::nullopt;
    bool negative = false;
    if (text.front() == '-')
    {
        negative = true;
        text.remove_prefix(1);
    }
    if (text.empty()) return std::nullopt;

    const auto point = text.find('.');
    const std::string_view integer = point == std::string_view::npos ? text : text.substr(0, point);
    const std::string_view fraction = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    if (integer.empty() || fraction.size() > scale) return std::nullopt;
    const auto is_digit = [](char character) { return character >= '0' && character <= '9'; };
    if (!std::all_of(integer.begin(), integer.end(), is_digit) ||
        !std::all_of(fraction.begin(), fraction.end(), is_digit))
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16> magnitude{};
    const auto append_digit = [&](std::uint8_t digit) {
        std::uint16_t carry = digit;
        for (std::size_t index = magnitude.size(); index > 0; --index)
        {
            const std::uint16_t current = static_cast<std::uint16_t>(magnitude[index - 1] * 10U + carry);
            magnitude[index - 1] = static_cast<std::uint8_t>(current & UINT16_C(0xff));
            carry = static_cast<std::uint16_t>(current >> 8U);
        }
        return carry == 0;
    };
    for (const char digit : integer)
        if (!append_digit(static_cast<std::uint8_t>(digit - '0'))) return std::nullopt;
    for (const char digit : fraction)
        if (!append_digit(static_cast<std::uint8_t>(digit - '0'))) return std::nullopt;
    for (std::size_t index = fraction.size(); index < scale; ++index)
        if (!append_digit(0)) return std::nullopt;

    if (negative)
    {
        for (auto& byte : magnitude) byte = static_cast<std::uint8_t>(~byte);
        for (std::size_t index = magnitude.size(); index > 0; --index)
        {
            if (++magnitude[index - 1] != 0) break;
        }
    }
    std::array<std::byte, 16> coefficient{};
    for (std::size_t index = 0; index < magnitude.size(); ++index)
        coefficient[index] = static_cast<std::byte>(magnitude[index]);
    return coefficient;
}

int CompareSignedCoefficient(
    const std::array<std::byte, 16>& lhs,
    const std::array<std::byte, 16>& rhs
)
{
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        std::uint8_t left = std::to_integer<std::uint8_t>(lhs[index]);
        std::uint8_t right = std::to_integer<std::uint8_t>(rhs[index]);
        if (index == 0)
        {
            left ^= UINT8_C(0x80);
            right ^= UINT8_C(0x80);
        }
        if (left < right) return -1;
        if (left > right) return 1;
    }
    return 0;
}

value_validation_failure Failure(
    std::string code,
    std::string message,
    std::string_view override_message = {}
)
{
    if (!override_message.empty()) message = override_message;
    return {.code = std::move(code), .message = std::move(message)};
}
}

value_validation_failure validate_semantic_storage_value(
    const storage_value& value,
    semantic_type_kind kind,
    normalization_kind normalization,
    std::uint32_t fixed_size,
    std::uint32_t precision,
    std::uint32_t scale
)
{
    if (std::holds_alternative<std::monostate>(value)) return {};

    if (kind == semantic_type_kind::f32 || kind == semantic_type_kind::f64)
    {
        const auto* number = std::get_if<double>(&value);
        if (number == nullptr || !std::isfinite(*number))
            return Failure("value.real_not_finite", "Real values must be finite.");
        if (kind == semantic_type_kind::f32 && static_cast<double>(static_cast<float>(*number)) != *number)
            return Failure("value.f32_not_exact", "Value is not exactly representable as f32.");
    }
    if (kind == semantic_type_kind::text || kind == semantic_type_kind::json_text)
    {
        const auto* text = std::get_if<std::string>(&value);
        if (text == nullptr)
            return Failure("value.text_type_mismatch", "Text value has the wrong storage type.");
        if (!IsCanonicalText(*text, normalization))
            return Failure("value.normalization_mismatch", "Text value is not in canonical normalized form.");
        if (kind == semantic_type_kind::json_text)
        {
            const auto parsed = nlohmann::json::parse(text->begin(), text->end(), nullptr, false, false);
            if (parsed.is_discarded())
                return Failure("value.json_invalid", "JSON text must contain one valid JSON value.");
        }
    }
    if (kind == semantic_type_kind::blob)
    {
        const auto* bytes = std::get_if<storage_blob>(&value);
        if (bytes == nullptr)
            return Failure("value.blob_type_mismatch", "Blob value has the wrong storage type.");
        if (fixed_size != 0u && bytes->size() != fixed_size)
            return Failure("value.blob_size_mismatch", "Fixed-width blob has the wrong byte length.");
    }
    if (kind == semantic_type_kind::fixed_id)
    {
        const auto* bytes = std::get_if<storage_blob>(&value);
        if (bytes == nullptr || bytes->size() != fixed_size)
            return Failure("value.fixed_id_size_mismatch", "Fixed identifier has the wrong byte length.");
    }
    if (kind == semantic_type_kind::decimal)
    {
        const auto* decimal = std::get_if<decimal_storage_value>(&value);
        if (decimal == nullptr || decimal->scale != scale)
            return Failure("value.decimal_scale_mismatch", "Decimal128 value has the wrong fixed scale.");
        if (DecimalPrecision(decimal->coefficient) > precision)
            return Failure("value.decimal_precision_exceeded", "Decimal128 value exceeds its precision.");
    }
    return {};
}

value_validation_failure validate_storage_check(
    const storage_value& value,
    check_kind kind,
    schema_field_kind field_kind,
    check_unit unit,
    std::string_view expected,
    std::string_view message
)
{
    if (std::holds_alternative<std::monostate>(value)) return {};
    switch (kind)
    {
        case check_kind::minimum_integer:
        case check_kind::maximum_integer:
        {
            bool passes = false;
            if (const auto* signed_value = std::get_if<std::int64_t>(&value))
            {
                const auto bound = ParseInteger<std::int64_t>(expected);
                passes = bound.has_value() && (kind == check_kind::minimum_integer
                    ? *signed_value >= *bound : *signed_value <= *bound);
            }
            else if (const auto* unsigned_value = std::get_if<std::uint64_t>(&value))
            {
                const auto bound = ParseInteger<std::uint64_t>(expected);
                passes = bound.has_value() && (kind == check_kind::minimum_integer
                    ? *unsigned_value >= *bound : *unsigned_value <= *bound);
            }
            if (!passes)
                return Failure("value.integer_check_failed", "Integer value is outside its admitted range.", message);
            return {};
        }
        case check_kind::minimum_real:
        case check_kind::maximum_real:
        {
            bool passes = false;
            if (const auto* real = std::get_if<double>(&value))
            {
                const auto bound = ParseReal(expected);
                passes = bound.has_value() && (kind == check_kind::minimum_real
                    ? *real >= *bound : *real <= *bound);
            }
            else if (const auto* decimal = std::get_if<decimal_storage_value>(&value))
            {
                const auto bound = ParseDecimalCoefficient(expected, decimal->scale);
                if (bound.has_value())
                {
                    const int compared = CompareSignedCoefficient(decimal->coefficient, *bound);
                    passes = kind == check_kind::minimum_real ? compared >= 0 : compared <= 0;
                }
            }
            if (!passes)
                return Failure("value.real_check_failed", "Real value is outside its admitted range.", message);
            return {};
        }
        case check_kind::minimum_length:
        case check_kind::maximum_length:
        case check_kind::fixed_length:
        {
            const auto bound = ParseInteger<std::size_t>(expected);
            std::optional<std::size_t> size;
            bool unit_compatible = false;
            if (const auto* text = std::get_if<std::string>(&value))
            {
                if (unit == check_unit::none)
                {
                    unit_compatible = true;
                    size = Utf8ScalarLength(*text);
                    if (!size.has_value())
                        return Failure("value.text_utf8_invalid", "Text length requires valid UTF-8.", message);
                }
                else if (unit == check_unit::utf8_bytes)
                {
                    unit_compatible = true;
                    size = text->size();
                }
            }
            if (const auto* bytes = std::get_if<storage_blob>(&value); bytes != nullptr)
            {
                if (unit == check_unit::bytes && field_kind == schema_field_kind::blob)
                {
                    unit_compatible = true;
                    size = bytes->size();
                }
                else if (unit == check_unit::none || unit == check_unit::utf8_bytes)
                {
                    size = LogicalTextLength(field_kind, bytes->size());
                    unit_compatible = size.has_value();
                }
            }
            if (!unit_compatible)
                return Failure(
                    "value.length_check_unit_mismatch",
                    "Length-check unit is incompatible with the stored value.",
                    message
                );
            const bool passes = bound.has_value() && size.has_value() &&
                (kind == check_kind::minimum_length ? *size >= *bound :
                 kind == check_kind::maximum_length ? *size <= *bound : *size == *bound);
            if (!passes)
                return Failure("value.length_check_failed", "Value length violates its field check.", message);
            return {};
        }
        case check_kind::valid_json:
        {
            const auto* text = std::get_if<std::string>(&value);
            const auto parsed = text == nullptr
                ? nlohmann::json::value_t::discarded
                : nlohmann::json::parse(text->begin(), text->end(), nullptr, false, false).type();
            if (parsed == nlohmann::json::value_t::discarded)
                return Failure("value.json_invalid", "JSON text must contain one valid JSON value.", message);
            return {};
        }
        case check_kind::decimal_precision:
        {
            const auto* decimal = std::get_if<decimal_storage_value>(&value);
            const auto bound = ParseInteger<std::size_t>(expected);
            if (decimal == nullptr || !bound.has_value() || DecimalPrecision(decimal->coefficient) > *bound)
                return Failure("value.decimal_precision_exceeded", "Decimal128 value exceeds its precision.", message);
            return {};
        }
        case check_kind::decimal_scale:
        {
            const auto* decimal = std::get_if<decimal_storage_value>(&value);
            const auto bound = ParseInteger<std::uint32_t>(expected);
            if (decimal == nullptr || !bound.has_value() || decimal->scale != *bound)
                return Failure("value.decimal_scale_mismatch", "Decimal128 value has the wrong fixed scale.", message);
            return {};
        }
        case check_kind::enum_domain:
            return Failure("value.enum_domain_requires_generated_set", "Enum checks require a generated value set.", message);
    }
    return Failure("value.check_unknown", "Generated validation check is unknown.", message);
}
}
