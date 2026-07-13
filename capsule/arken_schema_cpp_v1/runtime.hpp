#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
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

enum class semantic_type_kind : std::uint8_t
{
    boolean = 0,
    u8 = 1,
    u16 = 2,
    u32 = 3,
    u64 = 4,
    i8 = 5,
    i16 = 6,
    i32 = 7,
    i64 = 8,
    f32 = 9,
    f64 = 10,
    text = 11,
    enumeration = 12,
    blob = 13,
    fixed_id = 14,
    decimal = 15,
    timestamp = 16,
    json_text = 17,
};

enum class storage_codec_kind : std::uint8_t
{
    sqlite_integer = 1,
    sqlite_real = 2,
    sqlite_text = 3,
    sqlite_blob = 4,
    sqlite_fixed_blob = 5,
    sqlite_ordered_u64 = 6,
    sqlite_ordered_decimal128 = 7,
};

enum class sqlite_storage_class : std::uint8_t
{
    integer = 0,
    real = 1,
    text = 2,
    blob = 3,
};

enum class normalization_kind : std::uint8_t
{
    none = 0,
    trim = 1,
    lowercase = 2,
    lowercase_trim = 3,
    ethereum_address = 4,
    url = 5,
};

enum class field_source_kind : std::uint8_t
{
    canonical = 0,
    imported = 1,
    derived = 2,
    boundary_generated = 3,
};

enum class serialization_policy : std::uint8_t
{
    include = 0,
    omit = 1,
};

enum class model_kind : std::uint8_t
{
    entity = 0,
    join = 1,
    credential = 2,
    ephemeral = 3,
    audit_ledger = 4,
    provenance = 5,
    import_ledger = 6,
};

enum class model_role : std::uint8_t
{
    unspecified = 0,
    universal_spine = 1,
    domain_facet = 2,
    definition = 3,
    revision = 4,
    instance = 5,
    occurrence = 6,
    manifestation = 7,
    representation = 8,
    assertion = 9,
    observation = 10,
    authoritative_aggregate = 11,
    ledger = 12,
    registry = 13,
    projection = 14,
    import_record = 15,
    receipt = 16,
    membership = 17,
    value_object = 18,
};

enum class retention_policy : std::uint8_t
{
    durable = 0,
    until_expiry = 1,
    immutable = 2,
    until_promotion = 3,
};

enum class relation_cardinality : std::uint8_t
{
    one = 0,
    optional_one = 1,
    many = 2,
};

enum class referential_action : std::uint8_t
{
    restrict = 0,
    cascade = 1,
    set_null = 2,
};

struct numeric_index_entry
{
    std::uint32_t key = 0;
    std::uint32_t ordinal = 0;
};

struct name_index_entry
{
    std::string_view key;
    std::uint32_t ordinal = 0;
};

template <typename Value, std::size_t Size>
[[nodiscard]] constexpr std::span<const Value> descriptor_span(
    const std::array<Value, Size>& values
) noexcept
{
    return {values.data(), values.size()};
}

template <typename Value, std::size_t Size>
[[nodiscard]] constexpr std::span<const Value> descriptor_slice(
    const std::array<Value, Size>& values,
    std::size_t offset,
    std::size_t count
) noexcept
{
    return descriptor_span(values).subspan(offset, count);
}

template <typename Value>
[[nodiscard]] constexpr const Value* find_numeric(
    std::span<const Value> values,
    std::span<const numeric_index_entry> index,
    std::uint32_t key
) noexcept
{
    std::size_t begin = 0;
    std::size_t end = index.size();
    while (begin < end)
    {
        const std::size_t middle = begin + (end - begin) / 2;
        if (index[middle].key < key)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == index.size() || index[begin].key != key ||
        index[begin].ordinal >= values.size())
    {
        return nullptr;
    }
    return &values[index[begin].ordinal];
}

template <typename Value>
[[nodiscard]] constexpr const Value* find_named(
    std::span<const Value> values,
    std::span<const name_index_entry> index,
    std::string_view key
) noexcept
{
    std::size_t begin = 0;
    std::size_t end = index.size();
    while (begin < end)
    {
        const std::size_t middle = begin + (end - begin) / 2;
        if (index[middle].key < key)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == index.size() || index[begin].key != key ||
        index[begin].ordinal >= values.size())
    {
        return nullptr;
    }
    return &values[index[begin].ordinal];
}

struct field_reference_descriptor
{
    std::uint32_t field_id = 0;
    std::string_view canonical_name;
};

struct field_descriptor
{
    std::uint32_t model_type_id = 0;
    std::uint32_t field_id = 0;
    std::uint32_t semantic_type_id = 0;
    std::uint32_t wire_type_id = 0;
    std::uint32_t storage_codec_id = 0;
    std::uint32_t semantic_fixed_size = 0;
    std::uint32_t precision = 0;
    std::uint32_t scale = 0;
    std::uint32_t storage_fixed_size = 0;
    std::string_view name;
    std::string_view storage_name;
    std::string_view semantic_type_name;
    semantic_type_kind type_kind = semantic_type_kind::u32;
    storage_codec_kind storage_codec = storage_codec_kind::sqlite_integer;
    sqlite_storage_class storage_class = sqlite_storage_class::integer;
    normalization_kind normalization = normalization_kind::none;
    field_source_kind source_kind = field_source_kind::canonical;
    serialization_policy serialization = serialization_policy::include;
    query_operator_mask query_operators = 0;
    bool required = true;
    bool redacted = false;
    bool immutable = false;
    bool write_once = false;
    bool primary_key = false;
    bool sortable = false;
    bool cursor = false;
    bool storage_exact = false;
    bool storage_order_preserving = false;
};

struct relation_descriptor
{
    std::uint32_t source_model_type_id = 0;
    std::uint32_t target_model_type_id = 0;
    std::string_view name;
    std::string_view target_qualified_name;
    std::span<const field_reference_descriptor> source_fields;
    std::span<const field_reference_descriptor> target_fields;
    relation_cardinality cardinality = relation_cardinality::one;
    referential_action on_delete = referential_action::restrict;
    referential_action on_update = referential_action::restrict;
};

struct model_descriptor
{
    std::uint32_t type_id = 0;
    std::string_view name;
    std::string_view storage_name;
    std::string_view qualified_name;
    std::string_view owner_capsule;
    std::string_view domain;
    std::string_view description;
    std::string_view mutation_authority;
    model_kind kind = model_kind::entity;
    model_role role = model_role::unspecified;
    retention_policy retention = retention_policy::durable;
    std::span<const field_descriptor> fields;
    std::span<const relation_descriptor> relations;
    std::span<const numeric_index_entry> field_id_index;
    std::span<const name_index_entry> field_name_index;
    std::span<const name_index_entry> relation_name_index;

    [[nodiscard]] constexpr const field_descriptor* find_field(std::uint32_t field_id) const noexcept
    {
        return find_numeric(fields, field_id_index, field_id);
    }

    [[nodiscard]] constexpr const field_descriptor* find_field(std::string_view field_name) const noexcept
    {
        return find_named(fields, field_name_index, field_name);
    }

    [[nodiscard]] constexpr const relation_descriptor* find_relation(
        std::string_view relation_name
    ) const noexcept
    {
        return find_named(relations, relation_name_index, relation_name);
    }
};

struct schema_catalog_view
{
    std::uint32_t idl_format_version = 0;
    std::uint32_t edition = 0;
    std::uint32_t schema_version = 0;
    std::string_view source_path;
    std::string_view schema_name;
    std::string_view owner_capsule;
    std::string_view semantic_fingerprint;
    std::span<const field_descriptor> fields;
    std::span<const relation_descriptor> relations;
    std::span<const model_descriptor> models;
    std::span<const numeric_index_entry> model_id_index;
    std::span<const name_index_entry> model_name_index;

    [[nodiscard]] constexpr const model_descriptor* find_model(std::uint32_t type_id) const noexcept
    {
        return find_numeric(models, model_id_index, type_id);
    }

    [[nodiscard]] constexpr const model_descriptor* find_model(std::string_view model_name) const noexcept
    {
        return find_named(models, model_name_index, model_name);
    }

    [[nodiscard]] constexpr const field_descriptor* find_field(
        std::uint32_t model_type_id,
        std::uint32_t field_id
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_field(field_id);
    }

    [[nodiscard]] constexpr const field_descriptor* find_field(
        std::uint32_t model_type_id,
        std::string_view field_name
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_field(field_name);
    }

    [[nodiscard]] constexpr const relation_descriptor* find_relation(
        std::uint32_t model_type_id,
        std::string_view relation_name
    ) const noexcept
    {
        const auto* model = find_model(model_type_id);
        return model == nullptr ? nullptr : model->find_relation(relation_name);
    }
};
}
