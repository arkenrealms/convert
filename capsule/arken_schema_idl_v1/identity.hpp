#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace arken::schema::idl::v1
{
enum class RegistryKind : std::uint8_t
{
    Model,
    Field,
    SemanticType,
    WireType,
    StorageCodec,
    Enum,
    EnumValue,
    Command,
    Event,
};

[[nodiscard]] constexpr bool IsKnownRegistryKind(RegistryKind kind)
{
    switch (kind)
    {
        case RegistryKind::Model:
        case RegistryKind::Field:
        case RegistryKind::SemanticType:
        case RegistryKind::WireType:
        case RegistryKind::StorageCodec:
        case RegistryKind::Enum:
        case RegistryKind::EnumValue:
        case RegistryKind::Command:
        case RegistryKind::Event: return true;
    }
    return false;
}

struct ModelTypeId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const ModelTypeId&) const = default;
};

struct FieldId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const FieldId&) const = default;
};

struct SemanticTypeId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const SemanticTypeId&) const = default;
};

struct WireTypeId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const WireTypeId&) const = default;
};

struct StorageCodecId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const StorageCodecId&) const = default;
};

struct EnumId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const EnumId&) const = default;
};

struct EnumValueId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const EnumValueId&) const = default;
};

struct CommandId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const CommandId&) const = default;
};

struct EventId
{
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] auto operator<=>(const EventId&) const = default;
};

struct RegistryScope
{
    RegistryKind kind = RegistryKind::Model;
    std::optional<ModelTypeId> modelTypeId;
    std::optional<EnumId> enumId;

    [[nodiscard]] bool valid() const
    {
        if (!IsKnownRegistryKind(kind)) return false;
        if (kind == RegistryKind::Field)
            return modelTypeId.has_value() && modelTypeId->valid() && !enumId.has_value();
        if (kind == RegistryKind::EnumValue)
            return enumId.has_value() && enumId->valid() && !modelTypeId.has_value();
        return !modelTypeId.has_value() && !enumId.has_value();
    }
    [[nodiscard]] bool operator==(const RegistryScope& other) const noexcept
    {
        return kind == other.kind && OptionalIdEqual(modelTypeId, other.modelTypeId) &&
            OptionalIdEqual(enumId, other.enumId);
    }

    [[nodiscard]] std::strong_ordering operator<=>(const RegistryScope& other) const noexcept
    {
        const auto kind_order = static_cast<std::uint8_t>(kind) <=> static_cast<std::uint8_t>(other.kind);
        if (kind_order != 0) return kind_order;
        const auto model_order = CompareOptionalId(modelTypeId, other.modelTypeId);
        if (model_order != 0) return model_order;
        return CompareOptionalId(enumId, other.enumId);
    }

private:
    template <typename Id>
    [[nodiscard]] static bool OptionalIdEqual(const std::optional<Id>& left, const std::optional<Id>& right) noexcept
    {
        if (left.has_value() != right.has_value()) return false;
        return !left.has_value() || left->value == right->value;
    }

    template <typename Id>
    [[nodiscard]] static std::strong_ordering CompareOptionalId(
        const std::optional<Id>& left,
        const std::optional<Id>& right
    ) noexcept
    {
        if (left.has_value() != right.has_value())
            return left.has_value() ? std::strong_ordering::greater : std::strong_ordering::less;
        if (!left.has_value()) return std::strong_ordering::equal;
        return left->value <=> right->value;
    }
};

[[nodiscard]] constexpr std::string_view RegistryKindName(RegistryKind kind)
{
    switch (kind)
    {
        case RegistryKind::Model: return "model";
        case RegistryKind::Field: return "field";
        case RegistryKind::SemanticType: return "semantic_type";
        case RegistryKind::WireType: return "wire_type";
        case RegistryKind::StorageCodec: return "storage_codec";
        case RegistryKind::Enum: return "enum";
        case RegistryKind::EnumValue: return "enum_value";
        case RegistryKind::Command: return "command";
        case RegistryKind::Event: return "event";
    }
    return "unknown";
}

static_assert(sizeof(ModelTypeId) == sizeof(std::uint32_t));
static_assert(sizeof(FieldId) == sizeof(std::uint32_t));
static_assert(sizeof(SemanticTypeId) == sizeof(std::uint32_t));
static_assert(sizeof(WireTypeId) == sizeof(std::uint32_t));
static_assert(sizeof(StorageCodecId) == sizeof(std::uint32_t));
static_assert(sizeof(EnumId) == sizeof(std::uint32_t));
static_assert(sizeof(EnumValueId) == sizeof(std::uint32_t));
static_assert(sizeof(CommandId) == sizeof(std::uint32_t));
static_assert(sizeof(EventId) == sizeof(std::uint32_t));
}
