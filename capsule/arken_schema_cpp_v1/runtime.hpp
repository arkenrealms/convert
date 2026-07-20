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

#include "arken_schema_core_v1/catalog_view.hpp"
#include "arken_schema_cpp_v1/type_concepts.hpp"

namespace arken::schema::cpp::v1 {
namespace schema_core = ::arken::schema::core::v1;

using numeric_index_entry = schema_core::numeric_index_entry;
using name_index_entry = schema_core::name_index_entry;
using schema_contract_ref = schema_core::schema_contract_ref;
using schema_core::descriptor_slice;
using schema_core::descriptor_span;
using schema_core::find_named;
using schema_core::find_numeric;

template <typename Value, std::uint32_t SemanticTypeId> struct strong_value {
  using value_type = Value;

  static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

  Value value{};

  constexpr strong_value() = default;
  constexpr explicit strong_value(Value initial) : value(std::move(initial)) {}

  [[nodiscard]] bool operator==(const strong_value &) const = default;
  [[nodiscard]] auto operator<=>(const strong_value &) const = default;
};

template <std::size_t Size, std::uint32_t SemanticTypeId> struct fixed_id {
  using value_type = std::array<std::byte, Size>;

  static constexpr std::size_t fixed_size = Size;
  static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

  value_type value{};

  constexpr fixed_id() = default;
  constexpr explicit fixed_id(value_type initial) : value(std::move(initial)) {}

  [[nodiscard]] bool operator==(const fixed_id &) const = default;
  [[nodiscard]] auto operator<=>(const fixed_id &) const = default;
};

template <std::uint32_t Precision, std::uint32_t Scale,
          std::uint32_t SemanticTypeId>
struct decimal128_value {
  using value_type = std::array<std::byte, 16>;

  static constexpr std::uint32_t precision = Precision;
  static constexpr std::uint32_t scale = Scale;
  static constexpr std::uint32_t semantic_type_id = SemanticTypeId;

  value_type value{};

  constexpr decimal128_value() = default;
  constexpr explicit decimal128_value(value_type initial)
      : value(std::move(initial)) {}

  [[nodiscard]] bool operator==(const decimal128_value &) const = default;
  [[nodiscard]] auto operator<=>(const decimal128_value &) const = default;
};

struct validation_issue {
  std::uint32_t model_type_id = 0;
  std::uint32_t field_id = 0;
  std::string code;
  std::string message;
};

template <std::uint32_t ModelTypeId> struct validation_result {
  static constexpr std::uint32_t model_type_id = ModelTypeId;

  std::vector<validation_issue> issues;

  [[nodiscard]] bool ok() const { return issues.empty(); }
};

enum class query_operator : std::uint8_t {
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

[[nodiscard]] constexpr query_operator_mask
query_operator_bit(query_operator operation) {
  return query_operator_mask{1} << static_cast<std::uint8_t>(operation);
}

template <typename Model, typename Value, std::uint32_t ModelTypeId,
          std::uint32_t FieldId, query_operator_mask OperatorMask,
          bool Sortable, bool Cursor>
struct field_token {
  using model_type = Model;
  using value_type = Value;

  static constexpr std::uint32_t model_type_id = ModelTypeId;
  static constexpr std::uint32_t field_id = FieldId;
  static constexpr query_operator_mask operator_mask = OperatorMask;
  static constexpr bool sortable = Sortable;
  static constexpr bool cursor = Cursor;

  std::string_view name;

  [[nodiscard]] static constexpr bool supports(query_operator operation) {
    return (operator_mask & query_operator_bit(operation)) != 0;
  }
};

template <typename Token>
concept field_token_type = requires {
  typename Token::model_type;
  typename Token::value_type;
  { Token::model_type_id } -> type_concepts::convertible_to<std::uint32_t>;
  { Token::field_id } -> type_concepts::convertible_to<std::uint32_t>;
  {
    Token::operator_mask
  } -> type_concepts::convertible_to<query_operator_mask>;
  { Token::sortable } -> type_concepts::convertible_to<bool>;
  { Token::cursor } -> type_concepts::convertible_to<bool>;
  {
    Token::supports(query_operator::equal)
  } -> type_concepts::convertible_to<bool>;
};

template <field_token_type Token> struct query_predicate {
  Token field;
  query_operator operation = query_operator::equal;
  std::vector<typename Token::value_type> values;
};

template <query_operator Operation, field_token_type Token>
  requires(Token::supports(Operation) && Operation != query_operator::in &&
           Operation != query_operator::not_in &&
           Operation != query_operator::is_null)
[[nodiscard]] query_predicate<Token>
where_value(Token field, typename Token::value_type value) {
  return {
      .field = std::move(field),
      .operation = Operation,
      .values = {std::move(value)},
  };
}

template <query_operator Operation, field_token_type Token>
  requires(Token::supports(Operation) && (Operation == query_operator::in ||
                                          Operation == query_operator::not_in))
[[nodiscard]] query_predicate<Token>
where_values(Token field, std::vector<typename Token::value_type> values) {
  return {
      .field = std::move(field),
      .operation = Operation,
      .values = std::move(values),
  };
}

template <field_token_type Token>
  requires(Token::supports(query_operator::is_null))
[[nodiscard]] query_predicate<Token> where_null(Token field) {
  return {
      .field = std::move(field),
      .operation = query_operator::is_null,
      .values = {},
  };
}

enum class sort_direction : std::uint8_t {
  ascending,
  descending,
};

template <field_token_type Token> struct query_sort {
  Token field;
  sort_direction direction = sort_direction::ascending;
};

template <field_token_type Token>
  requires(Token::sortable)
[[nodiscard]] query_sort<Token>
sort_by(Token field, sort_direction direction = sort_direction::ascending) {
  return {.field = std::move(field), .direction = direction};
}

template <field_token_type Token> struct query_cursor {
  Token field;
  typename Token::value_type value;
};

template <field_token_type Token>
  requires(Token::cursor)
[[nodiscard]] query_cursor<Token>
cursor_after(Token field, typename Token::value_type value) {
  return {.field = std::move(field), .value = std::move(value)};
}

template <typename Model, field_token_type... Tokens> struct query_surface {
  using model_type = Model;
  using field_tokens = std::tuple<Tokens...>;

  static_assert((type_concepts::same_as<typename Tokens::model_type, Model> &&
                 ...));
};

enum class semantic_type_kind : std::uint8_t {
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

enum class schema_field_kind : std::uint8_t {
  object_id = 0,
  internal_id = 1,
  text = 2,
  slug = 3,
  display_name = 4,
  url = 5,
  email = 6,
  enumeration = 7,
  boolean = 8,
  timestamp_ms = 9,
  json_text = 10,
  secret = 11,
  ethereum_address = 12,
  hash_hex = 13,
  token_hash = 14,
  key_reference = 15,
  integer = 16,
  real = 17,
  decimal = 18,
  blob = 19,
};

enum class wire_type_kind : std::uint8_t {
  boolean = 1,
  u8 = 2,
  u16 = 3,
  u32 = 4,
  u64 = 5,
  i8 = 6,
  i16 = 7,
  i32 = 8,
  i64 = 9,
  f32 = 10,
  f64 = 11,
  utf8 = 12,
  bytes = 13,
  fixed_bytes = 14,
  decimal128 = 15,
};

enum class wire_byte_order : std::uint8_t {
  none = 0,
  big_endian = 1,
};

enum class wire_compatibility_class : std::uint8_t {
  boolean = 0,
  fixed_unsigned = 1,
  fixed_signed = 2,
  ieee754 = 3,
  utf8 = 4,
  bytes = 5,
  fixed_bytes = 6,
  decimal128 = 7,
};

enum class field_cardinality : std::uint8_t {
  required_one = 0,
  optional_one = 1,
  repeated = 2,
};

enum class wire_duplicate_policy : std::uint8_t {
  reject = 0,
  append = 1,
};

enum class value_representation_kind : std::uint8_t {
  native = 0,
  enum_name = 1,
  hex_text = 2,
};

enum class storage_codec_kind : std::uint8_t {
  sqlite_integer = 1,
  sqlite_real = 2,
  sqlite_text = 3,
  sqlite_blob = 4,
  sqlite_fixed_blob = 5,
  sqlite_ordered_u64 = 6,
  sqlite_ordered_decimal128 = 7,
};

enum class sqlite_storage_class : std::uint8_t {
  integer = 0,
  real = 1,
  text = 2,
  blob = 3,
};

enum class normalization_kind : std::uint8_t {
  none = 0,
  trim = 1,
  lowercase = 2,
  lowercase_trim = 3,
  ethereum_address = 4,
  url = 5,
};

enum class field_source_kind : std::uint8_t {
  canonical = 0,
  imported = 1,
  derived = 2,
  boundary_generated = 3,
};

enum class serialization_policy : std::uint8_t {
  include = 0,
  omit = 1,
};

enum class field_default_kind : std::uint8_t {
  none = 0,
  literal = 1,
  boundary = 2,
};

enum class boundary_default_source : std::uint8_t {
  none = 0,
  request_time = 1,
  generated_id = 2,
  generated_token = 3,
};

enum class check_kind : std::uint8_t {
  minimum_integer = 0,
  maximum_integer = 1,
  minimum_real = 2,
  maximum_real = 3,
  minimum_length = 4,
  maximum_length = 5,
  fixed_length = 6,
  enum_domain = 7,
  valid_json = 8,
  decimal_precision = 9,
  decimal_scale = 10,
};

enum class check_unit : std::uint8_t {
  none = 0,
  utf8_bytes = 1,
  bytes = 2,
};

enum class target_profile : std::uint8_t {
  cpp = 0,
  sqlite = 1,
  csharp = 2,
  javascript = 3,
  fabric = 4,
  http_json = 5,
};

using target_profile_mask = std::uint32_t;

[[nodiscard]] constexpr target_profile_mask
target_profile_bit(target_profile target) {
  return target_profile_mask{1} << static_cast<std::uint8_t>(target);
}

enum class model_kind : std::uint8_t {
  entity = 0,
  join = 1,
  credential = 2,
  ephemeral = 3,
  audit_ledger = 4,
  provenance = 5,
  import_ledger = 6,
};

enum class model_role : std::uint8_t {
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

enum class retention_policy : std::uint8_t {
  durable = 0,
  until_expiry = 1,
  immutable = 2,
  until_promotion = 3,
};

enum class relation_cardinality : std::uint8_t {
  one = 0,
  optional_one = 1,
  many = 2,
};

enum class fabric_lane : std::uint8_t {
  control_reliable = 0,
  state_reliable = 1,
  bulk_reliable = 2,
  realtime_unreliable = 3,
  compatibility = 4,
};

enum class command_idempotency : std::uint8_t {
  none = 0,
  idempotent = 1,
  safe = 2,
};

enum class command_streaming : std::uint8_t {
  unary = 0,
  server_stream = 1,
  client_stream = 2,
  bidirectional = 3,
};

enum class event_delivery : std::uint8_t {
  best_effort = 0,
  at_least_once = 1,
};

enum class event_ordering : std::uint8_t {
  none = 0,
  source = 1,
  aggregate = 2,
  global = 3,
};

enum class event_replay : std::uint8_t {
  none = 0,
  retained = 1,
  resumable = 2,
};

enum class referential_action : std::uint8_t {
  restrict = 0,
  cascade = 1,
  set_null = 2,
};

enum class key_kind : std::uint8_t {
  primary = 0,
  alternate = 1,
};

enum class name_review_status : std::uint8_t {
  canonical = 0,
  review_required = 1,
  approved_exception = 2,
};

struct no_payload {
  [[nodiscard]] bool operator==(const no_payload &) const = default;
};

enum class operation_target_profile : std::uint8_t {
  cpp = 0,
  fabric = 1,
  http_json = 2,
};

using operation_target_mask = std::uint8_t;

[[nodiscard]] constexpr operation_target_mask
operation_target_bit(operation_target_profile target) noexcept {
  return operation_target_mask{1} << static_cast<std::uint8_t>(target);
}

struct model_reference_descriptor {
  schema_core::ModelTypeId model_type_id;
  std::string_view qualified_name;

  [[nodiscard]] bool
  operator==(const model_reference_descriptor &) const = default;
};

struct fabric_operation_descriptor {
  fabric_lane lane = fabric_lane::control_reliable;
  std::uint32_t maximum_encoded_bytes = 0;
  bool enabled = false;
  bool direct_eligible = false;

  [[nodiscard]] bool
  operator==(const fabric_operation_descriptor &) const = default;
};

struct command_descriptor {
  schema_core::CommandId command_id;
  std::string_view name;
  std::string_view qualified_name;
  std::string_view owner_capsule;
  model_reference_descriptor request;
  model_reference_descriptor response;
  std::span<const model_reference_descriptor> errors;
  operation_target_mask targets = 0;
  fabric_operation_descriptor fabric;
  command_idempotency idempotency = command_idempotency::none;
  command_streaming streaming = command_streaming::unary;
  bool has_request = false;
  bool has_response = false;

  [[nodiscard]] constexpr bool
  supports(operation_target_profile target) const noexcept {
    return (targets & operation_target_bit(target)) != 0;
  }
};

struct event_descriptor {
  schema_core::EventId event_id;
  std::string_view name;
  std::string_view qualified_name;
  std::string_view owner_capsule;
  model_reference_descriptor payload;
  operation_target_mask targets = 0;
  fabric_operation_descriptor fabric;
  event_delivery delivery = event_delivery::best_effort;
  event_ordering ordering = event_ordering::none;
  event_replay replay = event_replay::none;

  [[nodiscard]] constexpr bool
  supports(operation_target_profile target) const noexcept {
    return (targets & operation_target_bit(target)) != 0;
  }
};

template <typename Request, typename Response, std::uint32_t CommandId>
struct command_token {
  using request_type = Request;
  using response_type = Response;

  static constexpr std::uint32_t command_id = CommandId;

  schema_contract_ref contract;
  const command_descriptor *descriptor = nullptr;
};

template <typename Payload, std::uint32_t EventId> struct event_token {
  using payload_type = Payload;

  static constexpr std::uint32_t event_id = EventId;

  schema_contract_ref contract;
  const event_descriptor *descriptor = nullptr;
};

template <typename Token>
concept command_token_type = requires(Token token) {
  typename Token::request_type;
  typename Token::response_type;
  { Token::command_id } -> type_concepts::convertible_to<std::uint32_t>;
  { token.contract } -> type_concepts::convertible_to<schema_contract_ref>;
  {
    token.descriptor
  } -> type_concepts::convertible_to<const command_descriptor *>;
};

template <typename Token>
concept event_token_type = requires(Token token) {
  typename Token::payload_type;
  { Token::event_id } -> type_concepts::convertible_to<std::uint32_t>;
  { token.contract } -> type_concepts::convertible_to<schema_contract_ref>;
  {
    token.descriptor
  } -> type_concepts::convertible_to<const event_descriptor *>;
};

struct field_reference_descriptor {
  schema_core::FieldId field_id;
  std::string_view canonical_name;
};

struct enum_value_descriptor {
  schema_core::EnumValueId value_id;
  std::string_view name;
};

struct check_descriptor {
  std::string_view name;
  check_kind kind = check_kind::minimum_integer;
  check_unit unit = check_unit::none;
  std::string_view value;
  std::string_view message;
  target_profile_mask targets = 0;
  bool generated = false;
};

struct field_descriptor {
  schema_core::ModelTypeId model_type_id;
  schema_core::FieldId field_id;
  schema_core::SemanticTypeId semantic_type_id;
  schema_core::WireTypeId wire_type_id;
  schema_core::StorageCodecId storage_codec_id;
  std::uint32_t semantic_fixed_size = 0;
  std::uint32_t wire_fixed_size = 0;
  std::uint32_t precision = 0;
  std::uint32_t scale = 0;
  std::uint32_t storage_fixed_size = 0;
  std::string_view name;
  std::string_view storage_name;
  std::string_view semantic_type_name;
  std::string_view semantic_type_registry_key;
  schema_field_kind field_kind = schema_field_kind::text;
  semantic_type_kind type_kind = semantic_type_kind::u32;
  wire_type_kind wire_type = wire_type_kind::u32;
  storage_codec_kind storage_codec = storage_codec_kind::sqlite_integer;
  sqlite_storage_class storage_class = sqlite_storage_class::integer;
  normalization_kind normalization = normalization_kind::none;
  field_source_kind source_kind = field_source_kind::canonical;
  serialization_policy serialization = serialization_policy::include;
  field_default_kind default_kind = field_default_kind::none;
  boundary_default_source boundary_default = boundary_default_source::none;
  std::string_view default_literal;
  std::string_view catalog_default_value;
  std::string_view source_field;
  std::string_view import_transform;
  std::span<const enum_value_descriptor> enum_values;
  std::span<const check_descriptor> checks;
  query_operator_mask query_operators = 0;
  field_cardinality cardinality = field_cardinality::required_one;
  std::uint32_t maximum_items = 0;
  bool redacted = false;
  bool immutable = false;
  bool write_once = false;
  bool primary_key = false;
  bool sortable = false;
  bool cursor = false;
  bool storage_exact = false;
  bool storage_order_preserving = false;
  std::string_view catalog_registry_key;
  std::string_view catalog_field_name;
  std::uint32_t catalog_declaration_order = 0;
};

struct key_descriptor {
  std::string_view name;
  key_kind kind = key_kind::alternate;
  std::span<const field_reference_descriptor> fields;
  bool generated = false;
};

struct index_descriptor {
  std::string_view name;
  std::span<const field_reference_descriptor> fields;
  bool unique = false;
  bool generated = false;
};

struct relation_descriptor {
  schema_core::ModelTypeId source_model_type_id;
  schema_core::ModelTypeId target_model_type_id;
  std::string_view name;
  std::string_view target_qualified_name;
  std::span<const field_reference_descriptor> source_fields;
  std::span<const field_reference_descriptor> target_fields;
  relation_cardinality cardinality = relation_cardinality::one;
  referential_action on_delete = referential_action::restrict;
  referential_action on_update = referential_action::restrict;
};

struct join_descriptor {
  std::string_view name;
  schema_core::ModelTypeId through_model_type_id;
  std::string_view through_qualified_name;
  std::string_view source_relation;
  std::string_view target_relation;
};

struct one_of_variant_descriptor {
  std::string_view tag;
  schema_core::EnumValueId tag_value_id;
  std::span<const field_reference_descriptor> fields;
  std::span<const field_reference_descriptor> optional_fields;
};

struct one_of_descriptor {
  std::string_view name;
  field_reference_descriptor discriminator;
  std::span<const one_of_variant_descriptor> variants;
};

struct wire_field_descriptor {
  schema_core::ModelTypeId model_type_id;
  schema_core::FieldId field_tag;
  schema_core::SemanticTypeId semantic_type_id;
  schema_core::WireTypeId wire_type_id;
  std::uint32_t fixed_size = 0;
  std::uint32_t scale = 0;
  std::string_view name;
  semantic_type_kind type_kind = semantic_type_kind::u32;
  value_representation_kind value_representation =
      value_representation_kind::native;
  wire_type_kind wire_type = wire_type_kind::u32;
  wire_byte_order byte_order = wire_byte_order::big_endian;
  wire_compatibility_class compatibility =
      wire_compatibility_class::fixed_unsigned;
  field_cardinality cardinality = field_cardinality::required_one;
  wire_duplicate_policy duplicate_policy = wire_duplicate_policy::reject;
  serialization_policy serialization = serialization_policy::include;
  std::uint32_t maximum_items = 0;
  std::span<const enum_value_descriptor> enum_values;
};

struct wire_message_descriptor {
  schema_core::ModelTypeId model_type_id;
  std::string_view name;
  std::string_view qualified_name;
  schema_core::descriptor_view<wire_field_descriptor> fields;
  std::span<const one_of_descriptor> one_ofs;
  std::span<const numeric_index_entry> field_tag_index;

  [[nodiscard]] constexpr const wire_field_descriptor *
  find_field(schema_core::FieldId field_tag) const noexcept {
    const auto *indexed =
        find_numeric(fields.span(), field_tag_index, field_tag.value);
    if (indexed != nullptr && indexed->field_tag == field_tag)
      return indexed;
    for (const auto &field : fields) {
      if (field.field_tag == field_tag)
        return &field;
    }
    return nullptr;
  }
};

struct model_descriptor {
  schema_core::ModelTypeId type_id;
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
  std::span<const key_descriptor> keys;
  std::span<const index_descriptor> indexes;
  std::span<const relation_descriptor> relations;
  std::span<const join_descriptor> joins;
  std::span<const one_of_descriptor> one_ofs;
  std::span<const std::string_view> source_aliases;
  std::span<const std::string_view> retired_names;
  std::span<const std::string_view> resolution_retired_names;
  name_review_status name_review = name_review_status::canonical;
  std::string_view name_review_rationale;
  std::span<const numeric_index_entry> field_id_index;
  std::span<const name_index_entry> field_name_index;
  std::span<const name_index_entry> relation_name_index;
  std::string_view catalog_fragment_id;
  std::string_view catalog_registry_key;
  std::string_view catalog_model_id;
  std::string_view catalog_logical_name;
  std::string_view catalog_qualified_name;
  std::uint32_t catalog_declaration_order = 0;

  [[nodiscard]] constexpr const field_descriptor *
  find_field(schema_core::FieldId field_id) const noexcept {
    return find_numeric(fields, field_id_index, field_id.value);
  }

  [[nodiscard]] constexpr const field_descriptor *
  find_field(std::string_view field_name) const noexcept {
    return find_named(fields, field_name_index, field_name);
  }

  [[nodiscard]] constexpr const relation_descriptor *
  find_relation(std::string_view relation_name) const noexcept {
    return find_named(relations, relation_name_index, relation_name);
  }
};

struct catalog_fragment_descriptor {
  std::string_view fragment_id;
  std::string_view owner_capsule;
  std::uint32_t version = 0;
  std::uint32_t declaration_order = 0;
};

struct schema_catalog_traits {
  using field_descriptor_type = field_descriptor;
  using relation_descriptor_type = relation_descriptor;
  using model_descriptor_type = model_descriptor;
  using wire_field_descriptor_type = wire_field_descriptor;
  using wire_message_descriptor_type = wire_message_descriptor;
  using command_descriptor_type = command_descriptor;
  using event_descriptor_type = event_descriptor;
  using catalog_fragment_descriptor_type = catalog_fragment_descriptor;
};

using schema_catalog_view =
    schema_core::basic_schema_catalog_view<schema_catalog_traits>;
using schema_catalog_model_registry_entry =
    schema_core::schema_catalog_model_registry_entry<schema_catalog_view>;
using schema_catalog_command_registry_entry =
    schema_core::schema_catalog_command_registry_entry<schema_catalog_view>;
using schema_catalog_event_registry_entry =
    schema_core::schema_catalog_event_registry_entry<schema_catalog_view>;
using schema_model_name_index_kind = schema_core::schema_model_name_index_kind;
using schema_catalog_registry_view =
    schema_core::basic_schema_catalog_registry_view<schema_catalog_view>;
} // namespace arken::schema::cpp::v1
