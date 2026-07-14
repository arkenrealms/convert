#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "arken_schema_cpp_v1/runtime.hpp"

namespace arken::schema::cpp::v1
{
using storage_blob = std::vector<std::byte>;

struct decimal_storage_value
{
    std::array<std::byte, 16> coefficient{};
    std::uint32_t scale = 0;

    [[nodiscard]] bool operator==(const decimal_storage_value&) const = default;
};

using storage_value = std::variant<
    std::monostate,
    bool,
    std::int64_t,
    std::uint64_t,
    double,
    decimal_storage_value,
    std::string,
    storage_blob
>;

struct binding_field
{
    std::uint32_t field_id = 0;
    std::string_view name;
    storage_value value;
};

using binding_record = std::vector<binding_field>;

template <std::uint32_t ModelTypeId>
struct binding_result
{
    static constexpr std::uint32_t model_type_id = ModelTypeId;

    binding_record record;
    std::vector<validation_issue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

template <typename Value, std::uint32_t ModelTypeId>
struct decode_result
{
    static constexpr std::uint32_t model_type_id = ModelTypeId;

    std::optional<Value> value;
    std::vector<validation_issue> issues;

    [[nodiscard]] bool ok() const { return value.has_value() && issues.empty(); }
};

template <typename Value>
struct value_decode_result
{
    std::optional<Value> value;
    std::string code;
    std::string message;

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

enum class field_update_state : std::uint8_t
{
    unchanged = 0,
    value = 1,
    null_value = 2,
};

template <typename Value, bool Nullable>
struct field_update
{
    field_update_state state = field_update_state::unchanged;
    Value payload{};

    [[nodiscard]] static field_update set(Value value)
    {
        return {.state = field_update_state::value, .payload = std::move(value)};
    }

    [[nodiscard]] static field_update set_null() requires(Nullable)
    {
        return {.state = field_update_state::null_value};
    }

    [[nodiscard]] bool changed() const { return state != field_update_state::unchanged; }
};

template <typename Value, std::uint32_t SemanticTypeId>
[[nodiscard]] storage_value encode_storage_value(
    const strong_value<Value, SemanticTypeId>& semantic_value
)
{
    if constexpr (std::same_as<Value, bool>)
    {
        return semantic_value.value;
    }
    else if constexpr (std::unsigned_integral<Value> && sizeof(Value) == sizeof(std::uint64_t))
    {
        return static_cast<std::uint64_t>(semantic_value.value);
    }
    else if constexpr (std::integral<Value>)
    {
        return static_cast<std::int64_t>(semantic_value.value);
    }
    else if constexpr (std::floating_point<Value>)
    {
        return static_cast<double>(semantic_value.value);
    }
    else if constexpr (std::same_as<Value, std::string>)
    {
        return semantic_value.value;
    }
    else if constexpr (std::same_as<Value, std::vector<std::byte>>)
    {
        return storage_blob(semantic_value.value.begin(), semantic_value.value.end());
    }
    else
    {
        static_assert(std::same_as<Value, void>, "Unsupported generated semantic value type.");
    }
}

template <std::size_t Size, std::uint32_t SemanticTypeId>
[[nodiscard]] storage_value encode_storage_value(const fixed_id<Size, SemanticTypeId>& value)
{
    return storage_blob(value.value.begin(), value.value.end());
}

template <std::uint32_t Precision, std::uint32_t Scale, std::uint32_t SemanticTypeId>
[[nodiscard]] storage_value encode_storage_value(
    const decimal128_value<Precision, Scale, SemanticTypeId>& value
)
{
    return decimal_storage_value{.coefficient = value.value, .scale = Scale};
}

template <typename Value>
    requires std::is_enum_v<Value>
[[nodiscard]] storage_value encode_storage_value(Value value)
{
    using underlying = std::underlying_type_t<Value>;
    static_assert(std::unsigned_integral<underlying> && sizeof(underlying) <= sizeof(std::uint32_t));
    return static_cast<std::int64_t>(static_cast<underlying>(value));
}

namespace detail
{
template <typename Value>
struct strong_value_traits
{
    static constexpr bool value = false;
};

template <typename Value, std::uint32_t SemanticTypeId>
struct strong_value_traits<strong_value<Value, SemanticTypeId>>
{
    static constexpr bool value = true;
    using value_type = Value;
};

template <typename Value>
struct fixed_id_traits
{
    static constexpr bool value = false;
};

template <std::size_t Size, std::uint32_t SemanticTypeId>
struct fixed_id_traits<fixed_id<Size, SemanticTypeId>>
{
    static constexpr bool value = true;
    static constexpr std::size_t size = Size;
};

template <typename Value>
struct decimal_traits
{
    static constexpr bool value = false;
};

template <std::uint32_t Precision, std::uint32_t Scale, std::uint32_t SemanticTypeId>
struct decimal_traits<decimal128_value<Precision, Scale, SemanticTypeId>>
{
    static constexpr bool value = true;
    static constexpr std::uint32_t precision = Precision;
    static constexpr std::uint32_t scale = Scale;
};

template <typename Value>
[[nodiscard]] value_decode_result<Value> DecodeFailure(std::string code, std::string message)
{
    return {.code = std::move(code), .message = std::move(message)};
}
}

template <typename SemanticValue>
[[nodiscard]] value_decode_result<SemanticValue> decode_storage_value(const storage_value& value)
{
    if constexpr (detail::strong_value_traits<SemanticValue>::value)
    {
        using underlying = typename detail::strong_value_traits<SemanticValue>::value_type;
        if constexpr (std::same_as<underlying, bool>)
        {
            const auto* decoded = std::get_if<bool>(&value);
            if (decoded != nullptr) return {.value = SemanticValue{*decoded}};
        }
        else if constexpr (std::unsigned_integral<underlying> && sizeof(underlying) == sizeof(std::uint64_t))
        {
            const auto* decoded = std::get_if<std::uint64_t>(&value);
            if (decoded != nullptr) return {.value = SemanticValue{static_cast<underlying>(*decoded)}};
        }
        else if constexpr (std::unsigned_integral<underlying>)
        {
            const auto* decoded = std::get_if<std::int64_t>(&value);
            if (decoded != nullptr && *decoded >= 0 &&
                static_cast<std::uint64_t>(*decoded) <= std::numeric_limits<underlying>::max())
            {
                return {.value = SemanticValue{static_cast<underlying>(*decoded)}};
            }
        }
        else if constexpr (std::signed_integral<underlying>)
        {
            const auto* decoded = std::get_if<std::int64_t>(&value);
            if (decoded != nullptr && *decoded >= std::numeric_limits<underlying>::min() &&
                *decoded <= std::numeric_limits<underlying>::max())
            {
                return {.value = SemanticValue{static_cast<underlying>(*decoded)}};
            }
        }
        else if constexpr (std::floating_point<underlying>)
        {
            const auto* decoded = std::get_if<double>(&value);
            if (decoded != nullptr && std::isfinite(*decoded))
            {
                const auto narrowed = static_cast<underlying>(*decoded);
                if constexpr (std::same_as<underlying, float>)
                {
                    if (static_cast<double>(narrowed) == *decoded)
                        return {.value = SemanticValue{narrowed}};
                }
                else
                {
                    return {.value = SemanticValue{narrowed}};
                }
            }
        }
        else if constexpr (std::same_as<underlying, std::string>)
        {
            const auto* decoded = std::get_if<std::string>(&value);
            if (decoded != nullptr) return {.value = SemanticValue{*decoded}};
        }
        else if constexpr (std::same_as<underlying, std::vector<std::byte>>)
        {
            const auto* decoded = std::get_if<storage_blob>(&value);
            if (decoded != nullptr)
                return {.value = SemanticValue{std::vector<std::byte>(decoded->begin(), decoded->end())}};
        }
        return detail::DecodeFailure<SemanticValue>(
            "storage.type_mismatch",
            "Stored value does not match the generated semantic type."
        );
    }
    else if constexpr (detail::fixed_id_traits<SemanticValue>::value)
    {
        const auto* decoded = std::get_if<storage_blob>(&value);
        if (decoded == nullptr || decoded->size() != detail::fixed_id_traits<SemanticValue>::size)
        {
            return detail::DecodeFailure<SemanticValue>(
                "storage.fixed_id_size_mismatch",
                "Stored fixed identifier has the wrong byte length."
            );
        }
        typename SemanticValue::value_type bytes{};
        std::copy(decoded->begin(), decoded->end(), bytes.begin());
        return {.value = SemanticValue{std::move(bytes)}};
    }
    else if constexpr (detail::decimal_traits<SemanticValue>::value)
    {
        const auto* decoded = std::get_if<decimal_storage_value>(&value);
        if (decoded == nullptr || decoded->scale != detail::decimal_traits<SemanticValue>::scale)
        {
            return detail::DecodeFailure<SemanticValue>(
                "storage.decimal_scale_mismatch",
                "Stored Decimal128 value has the wrong fixed scale."
            );
        }
        return {.value = SemanticValue{decoded->coefficient}};
    }
    else if constexpr (std::is_enum_v<SemanticValue>)
    {
        using underlying = std::underlying_type_t<SemanticValue>;
        const auto* decoded = std::get_if<std::int64_t>(&value);
        if (decoded != nullptr && *decoded >= 0 &&
            static_cast<std::uint64_t>(*decoded) <= std::numeric_limits<underlying>::max())
        {
            return {.value = static_cast<SemanticValue>(static_cast<underlying>(*decoded))};
        }
        return detail::DecodeFailure<SemanticValue>(
            "storage.enum_value_mismatch",
            "Stored enum value does not fit the generated enum wire type."
        );
    }
    else
    {
        static_assert(std::same_as<SemanticValue, void>, "Unsupported generated decode type.");
    }
}

struct value_validation_failure
{
    std::string code;
    std::string message;

    [[nodiscard]] bool ok() const { return code.empty(); }
};

[[nodiscard]] value_validation_failure validate_semantic_storage_value(
    const storage_value& value,
    semantic_type_kind kind,
    normalization_kind normalization,
    std::uint32_t fixed_size,
    std::uint32_t precision,
    std::uint32_t scale
);

[[nodiscard]] value_validation_failure validate_storage_check(
    const storage_value& value,
    check_kind kind,
    std::string_view expected,
    std::string_view message
);

struct erased_query_filter
{
    std::uint32_t field_id = 0;
    std::string_view field_name;
    query_operator operation = query_operator::equal;
    std::vector<storage_value> values;
};

struct erased_query_where
{
    std::vector<erased_query_filter> filters;
    std::vector<erased_query_where> all;
    std::vector<erased_query_where> any;
    std::vector<erased_query_where> none;
};

template <typename Model>
struct typed_query_where
{
    erased_query_where value;

    template <field_token_type Token>
        requires std::same_as<typename Token::model_type, Model>
    typed_query_where& add(query_predicate<Token> predicate)
    {
        erased_query_filter filter{
            .field_id = Token::field_id,
            .field_name = predicate.field.name,
            .operation = predicate.operation,
        };
        filter.values.reserve(predicate.values.size());
        for (const auto& item : predicate.values)
            filter.values.push_back(encode_storage_value(item));
        value.filters.push_back(std::move(filter));
        return *this;
    }

    typed_query_where& add_all(typed_query_where nested)
    {
        value.all.push_back(std::move(nested.value));
        return *this;
    }

    typed_query_where& add_any(typed_query_where nested)
    {
        value.any.push_back(std::move(nested.value));
        return *this;
    }

    typed_query_where& add_none(typed_query_where nested)
    {
        value.none.push_back(std::move(nested.value));
        return *this;
    }
};

struct erased_query_sort
{
    std::uint32_t field_id = 0;
    std::string_view field_name;
    sort_direction direction = sort_direction::ascending;
};

struct erased_query_cursor
{
    erased_query_sort sort;
    binding_record values;
};

template <typename Model>
struct typed_query_options
{
    std::optional<typed_query_where<Model>> where;
    std::optional<erased_query_sort> sort;
    std::optional<erased_query_cursor> cursor;
    std::int64_t limit = 100;
    std::int64_t offset = 0;
    bool include_total_count = false;

    template <field_token_type Token>
        requires std::same_as<typename Token::model_type, Model> && Token::sortable
    typed_query_options& order_by(query_sort<Token> sorting)
    {
        sort = erased_query_sort{
            .field_id = Token::field_id,
            .field_name = sorting.field.name,
            .direction = sorting.direction,
        };
        return *this;
    }

    template <field_token_type Token>
        requires std::same_as<typename Token::model_type, Model> && Token::cursor
    typed_query_options& start_after(query_cursor<Token> position, binding_record primary_key)
    {
        if (!sort.has_value() || sort->field_id != Token::field_id)
        {
            sort = erased_query_sort{
                .field_id = Token::field_id,
                .field_name = position.field.name,
                .direction = sort_direction::ascending,
            };
        }
        erased_query_cursor erased{
            .sort = *sort,
        };
        erased.values.push_back({
            .field_id = Token::field_id,
            .name = position.field.name,
            .value = encode_storage_value(position.value),
        });
        for (auto& field : primary_key)
        {
            if (field.field_id != Token::field_id)
                erased.values.push_back(std::move(field));
        }
        cursor = std::move(erased);
        return *this;
    }
};
}
