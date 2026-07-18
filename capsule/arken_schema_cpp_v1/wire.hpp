#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "arken_schema_cpp_v1/binding.hpp"

namespace arken::schema::cpp::v1
{
inline constexpr std::uint32_t arken_binary_wire_profile_version = 1;

struct wire_limits
{
    std::size_t maximum_message_bytes = 1024u * 1024u;
    std::size_t maximum_field_bytes = 1024u * 1024u;
    std::size_t maximum_field_occurrences = 4096u;
    std::size_t maximum_unknown_fields = 64u;
    std::size_t maximum_unknown_bytes = 64u * 1024u;
};

enum class wire_unknown_field_policy : std::uint8_t
{
    preserve = 0,
    reject = 1,
};

struct wire_encode_options
{
    wire_limits limits;
};

struct wire_decode_options
{
    wire_limits limits;
    wire_unknown_field_policy unknown_fields = wire_unknown_field_policy::preserve;
};

struct wire_unknown_field
{
    std::uint32_t field_tag = 0;
    std::vector<std::byte> payload;

    [[nodiscard]] bool operator==(const wire_unknown_field&) const = default;
};

struct wire_issue
{
    schema_core::ModelTypeId model_type_id;
    schema_core::FieldId field_tag;
    std::size_t offset = 0;
    std::string code;
    std::string message;
};

struct wire_encode_result
{
    std::vector<std::byte> bytes;
    std::vector<wire_issue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

struct wire_record_decode_result
{
    binding_record record;
    std::vector<wire_unknown_field> unknown_fields;
    std::vector<wire_issue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

template <typename Value>
struct wire_model_decode_result
{
    std::optional<Value> value;
    std::vector<wire_unknown_field> unknown_fields;
    std::vector<wire_issue> issues;

    [[nodiscard]] bool ok() const { return value.has_value() && issues.empty(); }
};

[[nodiscard]] wire_encode_result encode_wire_record(
    const wire_message_descriptor& descriptor,
    std::span<const binding_field> record,
    std::span<const wire_unknown_field> unknown_fields = {},
    const wire_encode_options& options = {}
);

[[nodiscard]] wire_record_decode_result decode_wire_record(
    const wire_message_descriptor& descriptor,
    std::span<const std::byte> bytes,
    const wire_decode_options& options = {}
);

void append_wire_validation_issues(
    std::vector<wire_issue>& destination,
    std::span<const validation_issue> source
);
}
