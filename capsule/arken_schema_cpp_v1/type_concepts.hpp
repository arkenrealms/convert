#pragma once

#include <type_traits>
#include <utility>

namespace arken::schema::cpp::v1::type_concepts
{
template <typename Left, typename Right>
concept same_as = std::is_same_v<Left, Right> && std::is_same_v<Right, Left>;

template <typename From, typename To>
concept convertible_to = std::is_convertible_v<From, To> && requires {
    static_cast<To>(std::declval<From>());
};

template <typename Value>
concept integral = std::is_integral_v<Value>;

template <typename Value>
concept signed_integral = integral<Value> && std::is_signed_v<Value>;

template <typename Value>
concept unsigned_integral = integral<Value> && !signed_integral<Value>;

template <typename Value>
concept floating_point = std::is_floating_point_v<Value>;
} // namespace arken::schema::cpp::v1::type_concepts
