#pragma once
#include "runtime.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace arken::schema::cpp::v1::tool_json {
using Json = nlohmann::json;
inline void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
inline Json parse(std::string_view text, std::size_t maximumBytes) {
  require(text.size() <= maximumBytes, "JSON byte budget exceeded");
  return Json::parse(text.begin(), text.end(),
                     [](int depth, Json::parse_event_t, Json &) {
                       require(depth <= 64, "JSON depth budget exceeded");
                       return true;
                     });
}
template <class T> struct Codec;
template <class T> T readScalar(const Json &input) {
  if constexpr (std::is_same_v<T, std::string>) {
    require(input.is_string(), "string required");
    return input.get<std::string>();
  } else if constexpr (std::is_same_v<T, bool>) {
    require(input.is_boolean(), "boolean required");
    return input.get<bool>();
  } else if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
    require(input.is_string(), "exact integer requires decimal text");
    const auto &text = input.get_ref<const std::string &>();
    T value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    require(error == std::errc{} && end == text.data() + text.size() &&
                std::to_string(value) == text,
            "noncanonical or out-of-range integer");
    return value;
  } else if constexpr (std::is_integral_v<T>) {
    require(input.is_number_integer(), "integer required");
    if (input.is_number_unsigned()) {
      const auto v = input.get<std::uint64_t>();
      require(v <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()),
              "integer out of range");
      return static_cast<T>(v);
    }
    const auto v = input.get<std::int64_t>();
    require(v >= std::numeric_limits<T>::min() &&
                v <= std::numeric_limits<T>::max(),
            "integer out of range");
    return static_cast<T>(v);
  } else {
    static_assert(std::is_floating_point_v<T>);
    require(input.is_number(), "number required");
    const auto v = input.get<double>();
    require(std::isfinite(v), "finite number required");
    return static_cast<T>(v);
  }
}
template <class T> Json writeScalar(const T &value) {
  if constexpr (std::is_integral_v<T> && sizeof(T) == 8)
    return std::to_string(value);
  else
    return value;
}
template <class T, std::uint32_t Id> struct Codec<strong_value<T, Id>> {
  static strong_value<T, Id> read(const Json &value) {
    return strong_value<T, Id>{readScalar<T>(value)};
  }
  static Json write(const strong_value<T, Id> &value) {
    return writeScalar(value.value);
  }
};
template <std::size_t N, std::uint32_t Id> struct Codec<fixed_id<N, Id>> {
  static fixed_id<N, Id> read(const Json &value) {
    require(value.is_string(), "identifier requires text");
    const auto &text = value.get_ref<const std::string &>();
    require(text.size() == 2 * N, "identifier width mismatch");
    fixed_id<N, Id> result;
    auto hex = [](char c) -> unsigned {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      throw std::invalid_argument("identifier must be lowercase hex");
    };
    for (std::size_t i = 0; i < N; ++i)
      result.value[i] =
          static_cast<std::byte>(hex(text[2 * i]) * 16 + hex(text[2 * i + 1]));
    return result;
  }
  static Json write(const fixed_id<N, Id> &value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(2 * N, '0');
    for (std::size_t i = 0; i < N; ++i) {
      const auto n = std::to_integer<unsigned>(value.value[i]);
      out[2 * i] = digits[n / 16];
      out[2 * i + 1] = digits[n % 16];
    }
    return out;
  }
};
template <std::uint32_t Id> struct Codec<strong_value<std::vector<std::byte>, Id>> {
  using Value = strong_value<std::vector<std::byte>, Id>;
  static Value read(const Json &value) {
    require(value.is_string(), "bytes require lowercase hex text");
    const auto &text = value.get_ref<const std::string &>();
    require(text.size() % 2 == 0, "bytes require paired hex digits");
    auto hex = [](char c) -> unsigned {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      throw std::invalid_argument("bytes must be lowercase hex");
    };
    Value result;
    result.value.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
      result.value.push_back(static_cast<std::byte>(hex(text[i]) * 16 + hex(text[i + 1])));
    return result;
  }
  static Json write(const Value &value) {
    require(value.value.size() <= std::numeric_limits<std::size_t>::max() / 2,
            "encoded byte length overflow");
    constexpr char digits[] = "0123456789abcdef";
    std::string out(value.value.size() * 2, '0');
    for (std::size_t i = 0; i < value.value.size(); ++i) {
      const auto byte = std::to_integer<unsigned>(value.value[i]);
      out[2 * i] = digits[byte / 16]; out[2 * i + 1] = digits[byte % 16];
    }
    return out;
  }
};
template <class T> Json write(const T &value) { return Codec<T>::write(value); }
template <class T> T read(const Json &value) { return Codec<T>::read(value); }
} // namespace arken::schema::cpp::v1::tool_json
