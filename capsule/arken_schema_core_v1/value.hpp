#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace arken::schema::core::v1
{
class Decimal128
{
  public:
    using CoefficientBytes = std::array<std::uint8_t, 16>;

    static std::optional<Decimal128> Parse(std::string_view value, std::int32_t scale);
    static std::optional<Decimal128> FromCoefficientBytes(CoefficientBytes coefficient, std::int32_t scale);

    [[nodiscard]] const CoefficientBytes& coefficient_bytes() const;
    [[nodiscard]] std::int32_t scale() const;
    [[nodiscard]] std::size_t precision() const;
    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] bool operator==(const Decimal128& other) const noexcept
    {
        return coefficient_ == other.coefficient_ && scale_ == other.scale_;
    }

    [[nodiscard]] std::strong_ordering operator<=>(const Decimal128& other) const noexcept
    {
        for (std::size_t index = 0; index < coefficient_.size(); ++index)
        {
            if (coefficient_[index] < other.coefficient_[index]) return std::strong_ordering::less;
            if (coefficient_[index] > other.coefficient_[index]) return std::strong_ordering::greater;
        }
        return scale_ <=> other.scale_;
    }

  private:
    Decimal128(CoefficientBytes coefficient, std::int32_t scale);

    CoefficientBytes coefficient_{};
    std::int32_t scale_ = 0;
};
} // namespace arken::schema::core::v1
