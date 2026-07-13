#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace arken::schema::cpp::v1
{
template <typename Value, std::uint32_t SemanticTypeId>
struct strong_value
{
    using value_type = Value;

    static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

    Value value{};

    constexpr strong_value() = default;
    constexpr explicit strong_value(Value initial) : value(std::move(initial)) {}

    [[nodiscard]] bool operator==(const strong_value&) const = default;
    [[nodiscard]] auto operator<=>(const strong_value&) const = default;
};

template <std::size_t Size, std::uint32_t SemanticTypeId>
struct fixed_id
{
    using value_type = std::array<std::byte, Size>;

    static constexpr std::size_t fixed_size = Size;
    static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

    value_type value{};

    constexpr fixed_id() = default;
    constexpr explicit fixed_id(value_type initial) : value(std::move(initial)) {}

    [[nodiscard]] bool operator==(const fixed_id&) const = default;
    [[nodiscard]] auto operator<=>(const fixed_id&) const = default;
};

template <std::uint32_t Precision, std::uint32_t Scale, std::uint32_t SemanticTypeId>
struct decimal128_value
{
    using value_type = std::array<std::byte, 16>;

    static constexpr std::uint32_t precision = Precision;
    static constexpr std::uint32_t scale = Scale;
    static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

    value_type value{};

    constexpr decimal128_value() = default;
    constexpr explicit decimal128_value(value_type initial) : value(std::move(initial)) {}

    [[nodiscard]] bool operator==(const decimal128_value&) const = default;
    [[nodiscard]] auto operator<=>(const decimal128_value&) const = default;
};

struct validation_issue
{
    std::uint32_t model_type_id = 0;
    std::uint32_t field_id = 0;
    std::string code;
    std::string message;
};

template <std::uint32_t ModelTypeId>
struct validation_result
{
    static constexpr std::uint32_t model_type_id = ModelTypeId;

    std::vector<validation_issue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

enum class query_operator : std::uint8_t
{
    equal,
    not_equal,
    in,
    not_in,
    less_than,
    less_than_or_equal,
    greater_than,
    greater_than_or_equal,
    prefix,
    suffix,
    contains,
    is_null,
};

using query_operator_mask = std::uint32_t;

[[nodiscard]] constexpr query_operator_mask query_operator_bit(query_operator operation)
{
    return query_operator_mask{1} << static_cast<std::uint8_t>(operation);
}

template <
    typename Model,
    typename Value,
    std::uint32_t ModelTypeId,
    std::uint32_t FieldId,
    query_operator_mask OperatorMask,
    bool Sortable,
    bool Cursor
>
struct field_token
{
    using model_type = Model;
    using value_type = Value;

    static constexpr std::uint32_t model_type_id = ModelTypeId;
    static constexpr std::uint32_t field_id = FieldId;
    static constexpr query_operator_mask operator_mask = OperatorMask;
    static constexpr bool sortable = Sortable;
    static constexpr bool cursor = Cursor;

    std::string_view name;

    [[nodiscard]] static constexpr bool supports(query_operator operation)
    {
        return (operator_mask & query_operator_bit(operation)) != 0;
    }
};

template <typename Token>
concept field_token_type = requires {
    typename Token::model_type;
    typename Token::value_type;
    { Token::model_type_id } -> std::convertible_to<std::uint32_t>;
    { Token::field_id } -> std::convertible_to<std::uint32_t>;
    { Token::operator_mask } -> std::convertible_to<query_operator_mask>;
    { Token::sortable } -> std::convertible_to<bool>;
    { Token::cursor } -> std::convertible_to<bool>;
    { Token::supports(query_operator::equal) } -> std::convertible_to<bool>;
};

template <field_token_type Token>
struct query_predicate
{
    Token field;
    query_operator operation = query_operator::equal;
    std::vector<typename Token::value_type> values;
};

template <query_operator Operation, field_token_type Token>
    requires(
        Token::supports(Operation) && Operation != query_operator::in &&
        Operation != query_operator::not_in && Operation != query_operator::is_null
    )
[[nodiscard]] query_predicate<Token> where_value(Token field, typename Token::value_type value)
{
    return {
        .field = std::move(field),
        .operation = Operation,
        .values = {std::move(value)},
    };
}

template <query_operator Operation, field_token_type Token>
    requires(
        Token::supports(Operation) &&
        (Operation == query_operator::in || Operation == query_operator::not_in)
    )
[[nodiscard]] query_predicate<Token> where_values(
    Token field, std::vector<typename Token::value_type> values
)
{
    return {
        .field = std::move(field),
        .operation = Operation,
        .values = std::move(values),
    };
}

template <field_token_type Token>
    requires(Token::supports(query_operator::is_null))
[[nodiscard]] query_predicate<Token> where_null(Token field)
{
    return {
        .field = std::move(field),
        .operation = query_operator::is_null,
        .values = {},
    };
}

enum class sort_direction : std::uint8_t
{
    ascending,
    descending,
};

template <field_token_type Token>
struct query_sort
{
    Token field;
    sort_direction direction = sort_direction::ascending;
};

template <field_token_type Token>
    requires(Token::sortable)
[[nodiscard]] query_sort<Token> sort_by(
    Token field, sort_direction direction = sort_direction::ascending
)
{
    return {.field = std::move(field), .direction = direction};
}

template <field_token_type Token>
struct query_cursor
{
    Token field;
    typename Token::value_type value;
};

template <field_token_type Token>
    requires(Token::cursor)
[[nodiscard]] query_cursor<Token> cursor_after(Token field, typename Token::value_type value)
{
    return {.field = std::move(field), .value = std::move(value)};
}

template <typename Model, field_token_type... Tokens>
struct query_surface
{
    using model_type = Model;
    using field_tokens = std::tuple<Tokens...>;

    static_assert((std::same_as<typename Tokens::model_type, Model> && ...));
};
}
