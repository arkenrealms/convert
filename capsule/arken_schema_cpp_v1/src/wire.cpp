#include "arken_schema_cpp_v1/wire.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace arken::schema::cpp::v1
{
namespace
{
template <typename Target, typename Source>
Target BitCast(const Source& source) noexcept
{
    static_assert(sizeof(Target) == sizeof(Source));
    static_assert(std::is_trivially_copyable_v<Target>);
    static_assert(std::is_trivially_copyable_v<Source>);
    Target target;
    std::memcpy(&target, &source, sizeof(Target));
    return target;
}

struct encoded_field
{
    std::uint32_t tag = 0;
    std::vector<std::byte> payload;
};

struct varuint32_result
{
    std::uint32_t value = 0;
    std::size_t next = 0;
    std::string code;
    std::string message;

    [[nodiscard]] bool ok() const { return code.empty(); }
};

void AddIssue(
    std::vector<wire_issue>& issues,
    const wire_message_descriptor& descriptor,
    std::uint32_t field_tag,
    std::size_t offset,
    std::string code,
    std::string message
)
{
    issues.push_back({
        .model_type_id = descriptor.model_type_id,
        .field_tag = field_tag,
        .offset = offset,
        .code = std::move(code),
        .message = std::move(message),
    });
}

bool ValidateLimits(
    std::vector<wire_issue>& issues,
    const wire_message_descriptor& descriptor,
    const wire_limits& limits
)
{
    if (limits.maximum_message_bytes != 0 && limits.maximum_field_bytes != 0 &&
        limits.maximum_field_occurrences != 0 &&
        limits.maximum_field_bytes <= std::numeric_limits<std::uint32_t>::max())
    {
        return true;
    }
    AddIssue(
        issues,
        descriptor,
        0,
        0,
        "wire.limits_invalid",
        "Wire message and occurrence limits must be greater than zero, and field payloads must fit u32 framing."
    );
    return false;
}

void AppendVarUint32(std::vector<std::byte>& output, std::uint32_t value)
{
    do
    {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fu);
        value >>= 7u;
        if (value != 0) byte |= 0x80u;
        output.push_back(static_cast<std::byte>(byte));
    } while (value != 0);
}

std::size_t VarUint32Size(std::uint32_t value)
{
    std::size_t size = 1;
    while (value >= 0x80u)
    {
        value >>= 7u;
        ++size;
    }
    return size;
}

varuint32_result ReadVarUint32(std::span<const std::byte> bytes, std::size_t offset)
{
    varuint32_result result{.next = offset};
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (result.next >= bytes.size())
        {
            result.code = "wire.varint_truncated";
            result.message = "Wire varuint32 is truncated.";
            return result;
        }
        const auto byte = std::to_integer<std::uint8_t>(bytes[result.next++]);
        if (index == 4 && (byte & 0xf0u) != 0)
        {
            result.code = "wire.varint_overflow";
            result.message = "Wire varuint32 exceeds u32.";
            return result;
        }
        result.value |= static_cast<std::uint32_t>(byte & 0x7fu) << (index * 7u);
        if ((byte & 0x80u) == 0)
        {
            if (index != 0 && (byte & 0x7fu) == 0)
            {
                result.code = "wire.varint_noncanonical";
                result.message = "Wire varuint32 uses a non-minimal encoding.";
            }
            return result;
        }
    }
    result.code = "wire.varint_overflow";
    result.message = "Wire varuint32 exceeds five bytes.";
    return result;
}

template <typename Unsigned>
void AppendBigEndian(std::vector<std::byte>& output, Unsigned value)
{
    static_assert(std::is_unsigned_v<Unsigned>);
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
    {
        const auto shift = static_cast<unsigned>((sizeof(Unsigned) - index - 1u) * 8u);
        output.push_back(static_cast<std::byte>((value >> shift) & Unsigned{0xff}));
    }
}

template <typename Unsigned>
Unsigned ReadBigEndian(std::span<const std::byte> bytes)
{
    static_assert(std::is_unsigned_v<Unsigned>);
    Unsigned value = 0;
    for (const auto byte : bytes)
        value = static_cast<Unsigned>((value << 8u) | std::to_integer<std::uint8_t>(byte));
    return value;
}

bool ValidUtf8(std::string_view value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first = bytes[index];
        if (first <= 0x7f)
        {
            ++index;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t scalar = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            length = 2;
            scalar = first & 0x1fu;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            length = 3;
            scalar = first & 0x0fu;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            length = 4;
            scalar = first & 0x07u;
        }
        else
        {
            return false;
        }
        if (index + length > value.size()) return false;
        for (std::size_t continuation = 1; continuation < length; ++continuation)
        {
            const auto byte = bytes[index + continuation];
            if ((byte & 0xc0u) != 0x80u) return false;
            scalar = (scalar << 6u) | (byte & 0x3fu);
        }
        if ((length == 3 && scalar < 0x800u) || (length == 4 && scalar < 0x10000u) ||
            (scalar >= 0xd800u && scalar <= 0xdfffu) || scalar > 0x10ffffu)
        {
            return false;
        }
        index += length;
    }
    return true;
}

bool RequireSize(
    std::vector<wire_issue>& issues,
    const wire_message_descriptor& message,
    const wire_field_descriptor& field,
    std::span<const std::byte> payload,
    std::size_t expected,
    std::size_t offset
)
{
    if (payload.size() == expected) return true;
    AddIssue(
        issues,
        message,
        field.field_tag,
        offset,
        "wire.payload_size_mismatch",
        "Wire field payload has the wrong fixed byte length."
    );
    return false;
}

std::optional<std::uint64_t> UnsignedValue(const storage_value& value)
{
    if (const auto* unsigned_value = std::get_if<std::uint64_t>(&value))
        return *unsigned_value;
    if (const auto* signed_value = std::get_if<std::int64_t>(&value);
        signed_value != nullptr && *signed_value >= 0)
    {
        return static_cast<std::uint64_t>(*signed_value);
    }
    return std::nullopt;
}

template <typename Unsigned>
bool AppendUnsignedPayload(
    std::vector<std::byte>& output,
    const storage_value& value
)
{
    const auto integer = UnsignedValue(value);
    if (!integer.has_value() || *integer > std::numeric_limits<Unsigned>::max()) return false;
    AppendBigEndian(output, static_cast<Unsigned>(*integer));
    return true;
}

template <typename Signed, typename Unsigned>
bool AppendSignedPayload(
    std::vector<std::byte>& output,
    const storage_value& value
)
{
    const auto* integer = std::get_if<std::int64_t>(&value);
    if (integer == nullptr || *integer < std::numeric_limits<Signed>::min() ||
        *integer > std::numeric_limits<Signed>::max())
    {
        return false;
    }
    AppendBigEndian(output, static_cast<Unsigned>(static_cast<Signed>(*integer)));
    return true;
}

bool IsNumericEnumWire(wire_type_kind kind)
{
    return kind == wire_type_kind::u8 || kind == wire_type_kind::u16 ||
        kind == wire_type_kind::u32;
}

std::optional<storage_value> NumericEnumWireValue(
    const wire_message_descriptor& message,
    const wire_field_descriptor& field,
    const storage_value& value,
    std::vector<wire_issue>& issues
)
{
    if (field.type_kind != semantic_type_kind::enumeration ||
        field.storage_codec != storage_codec_kind::sqlite_text ||
        !IsNumericEnumWire(field.wire_type))
    {
        return value;
    }
    const auto* storage_name = std::get_if<std::string>(&value);
    const auto found = storage_name == nullptr ? field.enum_values.end() :
        std::find_if(field.enum_values.begin(), field.enum_values.end(), [&](const auto& candidate) {
            return candidate.name == *storage_name;
        });
    if (found != field.enum_values.end())
        return storage_value{static_cast<std::int64_t>(found->value_id)};
    AddIssue(
        issues,
        message,
        field.field_tag,
        0,
        "wire.enum_domain",
        "Stored enum text is outside the governed wire domain."
    );
    return std::nullopt;
}

std::optional<std::vector<std::byte>> EncodePayload(
    const wire_message_descriptor& message,
    const wire_field_descriptor& field,
    const storage_value& value,
    std::vector<wire_issue>& issues
)
{
    std::vector<std::byte> output;
    const auto converted = NumericEnumWireValue(message, field, value, issues);
    if (!converted.has_value()) return std::nullopt;
    const auto& wire_value = *converted;
    bool valid = true;
    switch (field.wire_type)
    {
    case wire_type_kind::boolean:
    {
        const auto* boolean = std::get_if<bool>(&wire_value);
        valid = boolean != nullptr;
        if (valid) output.push_back(*boolean ? std::byte{1} : std::byte{0});
        break;
    }
    case wire_type_kind::u8:
        valid = AppendUnsignedPayload<std::uint8_t>(output, wire_value);
        break;
    case wire_type_kind::u16:
        valid = AppendUnsignedPayload<std::uint16_t>(output, wire_value);
        break;
    case wire_type_kind::u32:
        valid = AppendUnsignedPayload<std::uint32_t>(output, wire_value);
        break;
    case wire_type_kind::u64:
        valid = AppendUnsignedPayload<std::uint64_t>(output, wire_value);
        break;
    case wire_type_kind::i8:
        valid = AppendSignedPayload<std::int8_t, std::uint8_t>(output, wire_value);
        break;
    case wire_type_kind::i16:
        valid = AppendSignedPayload<std::int16_t, std::uint16_t>(output, wire_value);
        break;
    case wire_type_kind::i32:
        valid = AppendSignedPayload<std::int32_t, std::uint32_t>(output, wire_value);
        break;
    case wire_type_kind::i64:
        valid = AppendSignedPayload<std::int64_t, std::uint64_t>(output, wire_value);
        break;
    case wire_type_kind::f32:
    {
        const auto* real = std::get_if<double>(&wire_value);
        const float narrowed = real == nullptr ? 0.0f : static_cast<float>(*real);
        valid = real != nullptr && std::isfinite(*real) && std::isfinite(narrowed) &&
            static_cast<double>(narrowed) == *real;
        if (valid) AppendBigEndian(output, BitCast<std::uint32_t>(narrowed));
        break;
    }
    case wire_type_kind::f64:
    {
        const auto* real = std::get_if<double>(&wire_value);
        valid = real != nullptr && std::isfinite(*real);
        if (valid) AppendBigEndian(output, BitCast<std::uint64_t>(*real));
        break;
    }
    case wire_type_kind::utf8:
    {
        const auto* text = std::get_if<std::string>(&wire_value);
        valid = text != nullptr && ValidUtf8(*text);
        if (valid)
            output.assign(
                reinterpret_cast<const std::byte*>(text->data()),
                reinterpret_cast<const std::byte*>(text->data() + text->size())
            );
        break;
    }
    case wire_type_kind::bytes:
    case wire_type_kind::fixed_bytes:
    {
        const auto* bytes = std::get_if<storage_blob>(&wire_value);
        valid = bytes != nullptr &&
            (field.wire_type != wire_type_kind::fixed_bytes || bytes->size() == field.fixed_size);
        if (valid) output.assign(bytes->begin(), bytes->end());
        break;
    }
    case wire_type_kind::decimal128:
    {
        const auto* decimal = std::get_if<decimal_storage_value>(&wire_value);
        valid = decimal != nullptr && decimal->scale == field.scale;
        if (valid) output.assign(decimal->coefficient.begin(), decimal->coefficient.end());
        break;
    }
    }
    if (valid) return output;
    AddIssue(
        issues,
        message,
        field.field_tag,
        0,
        "wire.value_type_mismatch",
        "Generated value does not fit the field's governed wire encoding."
    );
    return std::nullopt;
}

std::optional<storage_value> DecodePayload(
    const wire_message_descriptor& message,
    const wire_field_descriptor& field,
    std::span<const std::byte> payload,
    std::size_t offset,
    std::vector<wire_issue>& issues
)
{
    switch (field.wire_type)
    {
    case wire_type_kind::boolean:
        if (!RequireSize(issues, message, field, payload, 1, offset)) return std::nullopt;
        if (payload[0] != std::byte{0} && payload[0] != std::byte{1})
        {
            AddIssue(issues, message, field.field_tag, offset, "wire.boolean_invalid",
                     "Wire boolean payload must be zero or one.");
            return std::nullopt;
        }
        return storage_value{payload[0] == std::byte{1}};
    case wire_type_kind::u8:
    case wire_type_kind::u16:
    case wire_type_kind::u32:
    case wire_type_kind::u64:
    {
        const std::size_t size = field.wire_type == wire_type_kind::u8 ? 1u :
            field.wire_type == wire_type_kind::u16 ? 2u :
            field.wire_type == wire_type_kind::u32 ? 4u : 8u;
        if (!RequireSize(issues, message, field, payload, size, offset)) return std::nullopt;
        std::uint64_t value = 0;
        if (size == 1) value = ReadBigEndian<std::uint8_t>(payload);
        if (size == 2) value = ReadBigEndian<std::uint16_t>(payload);
        if (size == 4) value = ReadBigEndian<std::uint32_t>(payload);
        if (size == 8) value = ReadBigEndian<std::uint64_t>(payload);
        if (field.type_kind == semantic_type_kind::enumeration &&
            field.storage_codec == storage_codec_kind::sqlite_text)
        {
            const auto found = std::find_if(
                field.enum_values.begin(),
                field.enum_values.end(),
                [value](const auto& candidate) { return candidate.value_id == value; }
            );
            if (found != field.enum_values.end()) return storage_value{std::string(found->name)};
            AddIssue(issues, message, field.field_tag, offset, "wire.enum_domain",
                     "Wire enum ID is outside the governed storage domain.");
            return std::nullopt;
        }
        if (field.type_kind == semantic_type_kind::u64) return storage_value{value};
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            AddIssue(issues, message, field.field_tag, offset, "wire.unsigned_value_overflow",
                     "Wire unsigned value cannot be represented by the generated semantic type.");
            return std::nullopt;
        }
        return storage_value{static_cast<std::int64_t>(value)};
    }
    case wire_type_kind::i8:
        if (!RequireSize(issues, message, field, payload, 1, offset)) return std::nullopt;
        return storage_value{static_cast<std::int64_t>(
            BitCast<std::int8_t>(ReadBigEndian<std::uint8_t>(payload)))};
    case wire_type_kind::i16:
        if (!RequireSize(issues, message, field, payload, 2, offset)) return std::nullopt;
        return storage_value{static_cast<std::int64_t>(
            BitCast<std::int16_t>(ReadBigEndian<std::uint16_t>(payload)))};
    case wire_type_kind::i32:
        if (!RequireSize(issues, message, field, payload, 4, offset)) return std::nullopt;
        return storage_value{static_cast<std::int64_t>(
            BitCast<std::int32_t>(ReadBigEndian<std::uint32_t>(payload)))};
    case wire_type_kind::i64:
        if (!RequireSize(issues, message, field, payload, 8, offset)) return std::nullopt;
        return storage_value{
            BitCast<std::int64_t>(ReadBigEndian<std::uint64_t>(payload))};
    case wire_type_kind::f32:
    {
        if (!RequireSize(issues, message, field, payload, 4, offset)) return std::nullopt;
        const float value = BitCast<float>(ReadBigEndian<std::uint32_t>(payload));
        if (std::isfinite(value)) return storage_value{static_cast<double>(value)};
        break;
    }
    case wire_type_kind::f64:
    {
        if (!RequireSize(issues, message, field, payload, 8, offset)) return std::nullopt;
        const double value = BitCast<double>(ReadBigEndian<std::uint64_t>(payload));
        if (std::isfinite(value)) return storage_value{value};
        break;
    }
    case wire_type_kind::utf8:
    {
        std::string value;
        value.reserve(payload.size());
        for (const auto byte : payload)
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        if (ValidUtf8(value)) return storage_value{std::move(value)};
        AddIssue(issues, message, field.field_tag, offset, "wire.utf8_invalid",
                 "Wire UTF-8 payload is malformed.");
        return std::nullopt;
    }
    case wire_type_kind::bytes:
        return storage_value{storage_blob(payload.begin(), payload.end())};
    case wire_type_kind::fixed_bytes:
        if (!RequireSize(issues, message, field, payload, field.fixed_size, offset))
            return std::nullopt;
        return storage_value{storage_blob(payload.begin(), payload.end())};
    case wire_type_kind::decimal128:
    {
        if (!RequireSize(issues, message, field, payload, 16, offset)) return std::nullopt;
        decimal_storage_value value{.scale = field.scale};
        std::copy(payload.begin(), payload.end(), value.coefficient.begin());
        return storage_value{value};
    }
    }
    AddIssue(issues, message, field.field_tag, offset, "wire.nonfinite_real",
             "Wire floating-point payload must be finite.");
    return std::nullopt;
}

std::map<std::uint32_t, std::size_t> RecordCounts(std::span<const binding_field> record)
{
    std::map<std::uint32_t, std::size_t> counts;
    for (const auto& field : record)
        if (!std::holds_alternative<std::monostate>(field.value)) ++counts[field.field_id];
    return counts;
}

void ValidateOneOfs(
    const wire_message_descriptor& descriptor,
    std::span<const binding_field> record,
    std::vector<wire_issue>& issues
)
{
    const auto counts = RecordCounts(record);
    for (const auto& one_of : descriptor.one_ofs)
    {
        const auto discriminator = std::find_if(record.begin(), record.end(), [&](const auto& field) {
            return field.field_id == one_of.discriminator.field_id &&
                !std::holds_alternative<std::monostate>(field.value);
        });
        const auto* value = discriminator == record.end() ? nullptr :
            std::get_if<std::int64_t>(&discriminator->value);
        if (value == nullptr || *value < 0)
        {
            AddIssue(issues, descriptor, one_of.discriminator.field_id, 0,
                     "wire.one_of_discriminator_invalid",
                     "Wire one_of discriminator is missing or is not a governed enum value.");
            continue;
        }
        const auto variant = std::find_if(one_of.variants.begin(), one_of.variants.end(), [&](const auto& candidate) {
            return candidate.tag_value_id == static_cast<std::uint64_t>(*value);
        });
        if (variant == one_of.variants.end())
        {
            AddIssue(issues, descriptor, one_of.discriminator.field_id, 0,
                     "wire.one_of_variant_unknown",
                     "Wire one_of discriminator does not select a declared variant.");
            continue;
        }
        std::set<std::uint32_t> selected_fields;
        for (const auto& field : variant->fields)
        {
            selected_fields.insert(field.field_id);
            if (!counts.contains(field.field_id))
                AddIssue(issues, descriptor, field.field_id, 0, "wire.one_of_field_missing",
                         "Wire one_of selected variant is missing a required field.");
        }
        for (const auto& field : variant->optional_fields) selected_fields.insert(field.field_id);
        for (const auto& candidate : one_of.variants)
        {
            for (const auto& field : candidate.fields)
            {
                if (counts.contains(field.field_id) && !selected_fields.contains(field.field_id))
                    AddIssue(issues, descriptor, field.field_id, 0, "wire.one_of_field_conflict",
                             "Wire one_of contains a field from an unselected variant.");
            }
            for (const auto& field : candidate.optional_fields)
            {
                if (counts.contains(field.field_id) && !selected_fields.contains(field.field_id))
                    AddIssue(issues, descriptor, field.field_id, 0, "wire.one_of_field_conflict",
                             "Wire one_of contains a field from an unselected variant.");
            }
        }
    }
}

void ValidateRequiredFields(
    const wire_message_descriptor& descriptor,
    const std::map<std::uint32_t, std::size_t>& counts,
    std::vector<wire_issue>& issues
)
{
    for (const auto& field : descriptor.fields)
    {
        if (field.serialization == serialization_policy::include &&
            field.cardinality == field_cardinality::required_one &&
            !counts.contains(field.field_tag))
        {
            AddIssue(issues, descriptor, field.field_tag, 0, "wire.required_field_missing",
                     "Wire message is missing a required field.");
        }
    }
}
}

wire_encode_result encode_wire_record(
    const wire_message_descriptor& descriptor,
    std::span<const binding_field> record,
    std::span<const wire_unknown_field> unknown_fields,
    const wire_encode_options& options
)
{
    wire_encode_result result;
    if (!ValidateLimits(result.issues, descriptor, options.limits)) return result;
    if (record.size() > options.limits.maximum_field_occurrences)
    {
        AddIssue(result.issues, descriptor, 0, 0, "wire.field_occurrence_limit_exceeded",
                 "Wire record exceeds the configured field occurrence limit.");
        return result;
    }
    if (unknown_fields.size() > options.limits.maximum_unknown_fields)
    {
        AddIssue(result.issues, descriptor, 0, 0, "wire.unknown_field_limit_exceeded",
                 "Wire unknown-field count exceeds the configured limit.");
        return result;
    }
    if (unknown_fields.size() > options.limits.maximum_field_occurrences - record.size())
    {
        AddIssue(result.issues, descriptor, 0, 0, "wire.field_occurrence_limit_exceeded",
                 "Wire known and unknown fields exceed the configured occurrence limit.");
        return result;
    }

    std::vector<encoded_field> encoded;
    encoded.reserve(record.size() + unknown_fields.size());
    std::map<std::uint32_t, std::size_t> counts;
    for (const auto& bound : record)
    {
        const auto* field = descriptor.find_field(bound.field_id);
        if (field == nullptr)
        {
            AddIssue(result.issues, descriptor, bound.field_id, 0, "wire.field_unknown",
                     "Wire record contains an unknown field tag; use the explicit unknown-field channel.");
            continue;
        }
        if (field->serialization == serialization_policy::omit) continue;
        if (std::holds_alternative<std::monostate>(bound.value))
        {
            if (field->cardinality == field_cardinality::required_one)
                AddIssue(result.issues, descriptor, bound.field_id, 0, "wire.required_field_null",
                         "Wire required fields cannot be null.");
            continue;
        }
        const auto count = ++counts[bound.field_id];
        if (field->duplicate_policy == wire_duplicate_policy::reject && count > 1)
        {
            AddIssue(result.issues, descriptor, bound.field_id, 0, "wire.field_duplicate",
                     "Wire singular field occurs more than once.");
            continue;
        }
        if (field->cardinality == field_cardinality::repeated &&
            field->maximum_items != 0 && count > field->maximum_items)
        {
            AddIssue(result.issues, descriptor, bound.field_id, 0, "wire.repeated_limit_exceeded",
                     "Wire repeated field exceeds its governed maximum_items.");
            continue;
        }
        auto payload = EncodePayload(descriptor, *field, bound.value, result.issues);
        if (!payload.has_value()) continue;
        if (payload->size() > options.limits.maximum_field_bytes)
        {
            AddIssue(result.issues, descriptor, bound.field_id, 0, "wire.field_size_limit_exceeded",
                     "Wire field exceeds the configured payload limit.");
            continue;
        }
        encoded.push_back({.tag = bound.field_id, .payload = std::move(*payload)});
    }
    ValidateRequiredFields(descriptor, counts, result.issues);
    ValidateOneOfs(descriptor, record, result.issues);

    std::size_t unknown_bytes = 0;
    for (const auto& unknown : unknown_fields)
    {
        bool valid = true;
        if (unknown.field_tag == 0)
        {
            AddIssue(result.issues, descriptor, 0, 0, "wire.field_tag_zero",
                     "Wire field tag zero is reserved.");
            valid = false;
        }
        else if (descriptor.find_field(unknown.field_tag) != nullptr)
        {
            AddIssue(result.issues, descriptor, unknown.field_tag, 0, "wire.unknown_field_collision",
                     "Explicit unknown field collides with a known field tag.");
            valid = false;
        }
        if (unknown.payload.size() > options.limits.maximum_field_bytes)
        {
            AddIssue(result.issues, descriptor, unknown.field_tag, 0, "wire.field_size_limit_exceeded",
                     "Wire unknown field exceeds the configured payload limit.");
            valid = false;
        }
        if (unknown.payload.size() > options.limits.maximum_unknown_bytes - unknown_bytes)
        {
            AddIssue(result.issues, descriptor, unknown.field_tag, 0, "wire.unknown_byte_limit_exceeded",
                     "Wire unknown-field bytes exceed the configured limit.");
            valid = false;
        }
        else
        {
            unknown_bytes += unknown.payload.size();
        }
        if (valid) encoded.push_back({.tag = unknown.field_tag, .payload = unknown.payload});
    }
    if (!result.issues.empty()) return result;

    std::stable_sort(encoded.begin(), encoded.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.tag < rhs.tag;
    });
    std::vector<std::byte> bytes;
    for (const auto& field : encoded)
    {
        const auto header_size = VarUint32Size(field.tag) +
            VarUint32Size(static_cast<std::uint32_t>(field.payload.size()));
        if (header_size > options.limits.maximum_message_bytes - bytes.size() ||
            field.payload.size() > options.limits.maximum_message_bytes - bytes.size() - header_size)
        {
            AddIssue(result.issues, descriptor, field.tag, bytes.size(),
                     "wire.message_size_limit_exceeded",
                     "Wire message exceeds the configured encoded-size limit.");
            return result;
        }
        AppendVarUint32(bytes, field.tag);
        AppendVarUint32(bytes, static_cast<std::uint32_t>(field.payload.size()));
        bytes.insert(bytes.end(), field.payload.begin(), field.payload.end());
    }
    result.bytes = std::move(bytes);
    return result;
}

wire_record_decode_result decode_wire_record(
    const wire_message_descriptor& descriptor,
    std::span<const std::byte> bytes,
    const wire_decode_options& options
)
{
    wire_record_decode_result result;
    if (!ValidateLimits(result.issues, descriptor, options.limits)) return result;
    if (bytes.size() > options.limits.maximum_message_bytes)
    {
        AddIssue(result.issues, descriptor, 0, 0, "wire.message_size_limit_exceeded",
                 "Wire message exceeds the configured encoded-size limit.");
        return result;
    }

    std::map<std::uint32_t, std::size_t> counts;
    std::size_t unknown_bytes = 0;
    std::size_t offset = 0;
    std::uint32_t previous_tag = 0;
    std::size_t occurrences = 0;
    while (offset < bytes.size())
    {
        const std::size_t field_offset = offset;
        const auto tag = ReadVarUint32(bytes, offset);
        if (!tag.ok())
        {
            AddIssue(result.issues, descriptor, 0, field_offset, tag.code, tag.message);
            return result;
        }
        offset = tag.next;
        if (tag.value == 0)
        {
            AddIssue(result.issues, descriptor, 0, field_offset, "wire.field_tag_zero",
                     "Wire field tag zero is reserved.");
            return result;
        }
        if (previous_tag > tag.value)
        {
            AddIssue(result.issues, descriptor, tag.value, field_offset, "wire.field_order_noncanonical",
                     "Wire fields must be ordered by nondecreasing field tag.");
            return result;
        }
        previous_tag = tag.value;
        const auto length = ReadVarUint32(bytes, offset);
        if (!length.ok())
        {
            AddIssue(result.issues, descriptor, tag.value, offset, length.code, length.message);
            return result;
        }
        offset = length.next;
        if (length.value > options.limits.maximum_field_bytes)
        {
            AddIssue(result.issues, descriptor, tag.value, offset, "wire.field_size_limit_exceeded",
                     "Wire field exceeds the configured payload limit.");
            return result;
        }
        if (length.value > bytes.size() - offset)
        {
            AddIssue(result.issues, descriptor, tag.value, offset, "wire.field_payload_truncated",
                     "Wire field payload is truncated.");
            return result;
        }
        if (++occurrences > options.limits.maximum_field_occurrences)
        {
            AddIssue(result.issues, descriptor, tag.value, field_offset,
                     "wire.field_occurrence_limit_exceeded",
                     "Wire message exceeds the configured field occurrence limit.");
            return result;
        }
        const auto payload = bytes.subspan(offset, length.value);
        const auto* field = descriptor.find_field(tag.value);
        if (field == nullptr)
        {
            if (options.unknown_fields == wire_unknown_field_policy::reject)
            {
                AddIssue(result.issues, descriptor, tag.value, field_offset, "wire.unknown_field_rejected",
                         "Wire message contains an unknown field tag.");
                return result;
            }
            unknown_bytes += payload.size();
            if (result.unknown_fields.size() >= options.limits.maximum_unknown_fields ||
                unknown_bytes > options.limits.maximum_unknown_bytes)
            {
                AddIssue(result.issues, descriptor, tag.value, field_offset,
                         "wire.unknown_field_limit_exceeded",
                         "Wire unknown fields exceed configured preservation limits.");
                return result;
            }
            result.unknown_fields.push_back({
                .field_tag = tag.value,
                .payload = std::vector<std::byte>(payload.begin(), payload.end()),
            });
        }
        else
        {
            if (field->serialization == serialization_policy::omit)
            {
                AddIssue(result.issues, descriptor, tag.value, field_offset,
                         "wire.field_not_serializable",
                         "Wire message contains a field excluded by its serialization policy.");
                return result;
            }
            const auto count = ++counts[tag.value];
            if (field->duplicate_policy == wire_duplicate_policy::reject && count > 1)
            {
                AddIssue(result.issues, descriptor, tag.value, field_offset, "wire.field_duplicate",
                         "Wire singular field occurs more than once.");
                return result;
            }
            if (field->cardinality == field_cardinality::repeated &&
                field->maximum_items != 0 && count > field->maximum_items)
            {
                AddIssue(result.issues, descriptor, tag.value, field_offset,
                         "wire.repeated_limit_exceeded",
                         "Wire repeated field exceeds its governed maximum_items.");
                return result;
            }
            auto value = DecodePayload(descriptor, *field, payload, offset, result.issues);
            if (!value.has_value()) return result;
            result.record.push_back({
                .field_id = tag.value,
                .name = field->name,
                .value = std::move(*value),
            });
        }
        offset += length.value;
    }
    ValidateRequiredFields(descriptor, counts, result.issues);
    ValidateOneOfs(descriptor, result.record, result.issues);
    return result;
}

void append_wire_validation_issues(
    std::vector<wire_issue>& destination,
    std::span<const validation_issue> source
)
{
    destination.reserve(destination.size() + source.size());
    for (const auto& issue : source)
    {
        destination.push_back({
            .model_type_id = issue.model_type_id,
            .field_tag = issue.field_id,
            .offset = 0,
            .code = issue.code,
            .message = issue.message,
        });
    }
}
}
