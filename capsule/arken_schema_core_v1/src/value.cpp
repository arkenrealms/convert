#include "arken_schema_core_v1/value.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace arken::schema::core::v1
{
namespace
{
using Bytes = Decimal128::CoefficientBytes;

bool IsZero(const Bytes& bytes)
{
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) { return value == 0; });
}

void Negate(Bytes& bytes)
{
    std::uint16_t carry = 1;
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
    {
        const auto value = static_cast<std::uint16_t>(static_cast<std::uint8_t>(~*it)) + carry;
        *it = static_cast<std::uint8_t>(value & UINT16_C(0xff));
        carry = static_cast<std::uint16_t>(value >> 8);
    }
}

bool MultiplyByTenAndAdd(Bytes& bytes, std::uint8_t digit)
{
    std::uint16_t carry = digit;
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
    {
        const auto value = static_cast<std::uint16_t>(*it) * UINT16_C(10) + carry;
        *it = static_cast<std::uint8_t>(value & UINT16_C(0xff));
        carry = static_cast<std::uint16_t>(value >> 8);
    }
    return carry == 0;
}

bool FitsSignedMagnitude(const Bytes& magnitude, bool negative)
{
    if (!negative) return (magnitude.front() & UINT8_C(0x80)) == 0;
    if (magnitude.front() < UINT8_C(0x80)) return true;
    if (magnitude.front() > UINT8_C(0x80)) return false;
    return std::all_of(magnitude.begin() + 1, magnitude.end(), [](std::uint8_t value) { return value == 0; });
}

Bytes Magnitude(Bytes coefficient)
{
    if ((coefficient.front() & UINT8_C(0x80)) != 0) Negate(coefficient);
    return coefficient;
}

std::string MagnitudeDigits(Bytes magnitude)
{
    if (IsZero(magnitude)) return "0";
    std::string reversed;
    while (!IsZero(magnitude))
    {
        std::uint16_t remainder = 0;
        for (auto& byte : magnitude)
        {
            const auto value = static_cast<std::uint16_t>(remainder * UINT16_C(256) + byte);
            byte = static_cast<std::uint8_t>(value / UINT16_C(10));
            remainder = static_cast<std::uint16_t>(value % UINT16_C(10));
        }
        reversed.push_back(static_cast<char>('0' + remainder));
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}
} // namespace

Decimal128::Decimal128(CoefficientBytes coefficient, std::int32_t scale)
    : coefficient_(std::move(coefficient)), scale_(scale)
{
}

std::optional<Decimal128> Decimal128::Parse(std::string_view value, std::int32_t scale)
{
    if (value.empty() || scale < 0 || scale > 38) return std::nullopt;
    bool negative = false;
    if (value.front() == '-')
    {
        negative = true;
        value.remove_prefix(1);
    }
    if (value.empty() || value.front() == '+') return std::nullopt;

    const auto decimal = value.find('.');
    if (decimal != std::string_view::npos && value.find('.', decimal + 1) != std::string_view::npos)
        return std::nullopt;
    const std::string_view integer = decimal == std::string_view::npos ? value : value.substr(0, decimal);
    const std::string_view fraction = decimal == std::string_view::npos ? std::string_view{} : value.substr(decimal + 1);
    if (integer.empty() || fraction.size() > static_cast<std::size_t>(scale)) return std::nullopt;
    if (!std::all_of(integer.begin(), integer.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }) ||
        !std::all_of(fraction.begin(), fraction.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }))
        return std::nullopt;

    Bytes magnitude{};
    const auto append_digits = [&magnitude](std::string_view digits) {
        for (const char ch : digits)
        {
            if (!MultiplyByTenAndAdd(magnitude, static_cast<std::uint8_t>(ch - '0'))) return false;
        }
        return true;
    };
    if (!append_digits(integer) || !append_digits(fraction)) return std::nullopt;
    for (std::size_t index = fraction.size(); index < static_cast<std::size_t>(scale); ++index)
    {
        if (!MultiplyByTenAndAdd(magnitude, 0)) return std::nullopt;
    }
    if (!FitsSignedMagnitude(magnitude, negative)) return std::nullopt;
    if (negative && !IsZero(magnitude)) Negate(magnitude);
    return Decimal128(std::move(magnitude), scale);
}

std::optional<Decimal128> Decimal128::FromCoefficientBytes(CoefficientBytes coefficient, std::int32_t scale)
{
    if (scale < 0 || scale > 38) return std::nullopt;
    return Decimal128(std::move(coefficient), scale);
}

const Decimal128::CoefficientBytes& Decimal128::coefficient_bytes() const
{
    return coefficient_;
}

std::int32_t Decimal128::scale() const
{
    return scale_;
}

std::size_t Decimal128::precision() const
{
    return MagnitudeDigits(Magnitude(coefficient_)).size();
}

std::string Decimal128::ToString() const
{
    const bool negative = (coefficient_.front() & UINT8_C(0x80)) != 0;
    std::string digits = MagnitudeDigits(Magnitude(coefficient_));
    if (scale_ > 0)
    {
        const auto scale = static_cast<std::size_t>(scale_);
        if (digits.size() <= scale) digits.insert(0, scale + 1 - digits.size(), '0');
        digits.insert(digits.size() - scale, 1, '.');
    }
    if (negative && !IsZero(coefficient_)) digits.insert(digits.begin(), '-');
    return digits;
}
} // namespace arken::schema::core::v1
