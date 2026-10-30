#include "arken_schema_idl_v1/allocation.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "allocation_internal.hpp"
#include "sha256.hpp"

namespace arken::schema::idl::v1
{
namespace
{
using Json = nlohmann::json;

constexpr std::string_view kLedgerFormat = "arken_schema_allocation_ledger";
constexpr std::uint32_t kLedgerFormatVersion = 2;

Json ActiveJson(const ActiveAllocation& active)
{
    return {
        {"canonical_name", active.canonicalName},
        {"id", active.id},
        {"owner_capsule", active.ownerCapsule},
    };
}

Json RetiredIdJson(const RetiredIdAllocation& retired)
{
    return {
        {"id", retired.id},
        {"owner_capsule", retired.ownerCapsule},
    };
}

Json RetiredNameJson(const RetiredNameAllocation& retired)
{
    return {
        {"canonical_name", retired.canonicalName},
        {"owner_capsule", retired.ownerCapsule},
    };
}

Json RegistryJson(const AllocationRegistry& registry)
{
    Json active = Json::array();
    for (const auto& allocation : registry.active) active.push_back(ActiveJson(allocation));
    Json retired_ids = Json::array();
    for (const auto& retired : registry.retiredIds) retired_ids.push_back(RetiredIdJson(retired));
    Json retired_names = Json::array();
    for (const auto& retired : registry.retiredNames)
        retired_names.push_back(RetiredNameJson(retired));
    return {
        {"active", std::move(active)},
        {
            "enum_id",
            registry.scope.enumId.has_value()
                ? Json(registry.scope.enumId->value)
                : Json(nullptr),
        },
        {"kind", RegistryKindName(registry.scope.kind)},
        {
            "enum_id",
            registry.scope.enumId.has_value()
                ? Json(registry.scope.enumId->value)
                : Json(nullptr),
        },
        {
            "model_type_id",
            registry.scope.modelTypeId.has_value()
                ? Json(registry.scope.modelTypeId->value)
                : Json(nullptr),
        },
        {"retired_ids", std::move(retired_ids)},
        {"retired_names", std::move(retired_names)},
    };
}

Json LedgerDataJson(const AllocationLedgerData& data)
{
    Json registries = Json::array();
    for (const auto& registry : data.registries) registries.push_back(RegistryJson(registry));
    return {
        {"format_version", data.formatVersion},
        {"registries", std::move(registries)},
    };
}

bool IsLowerHexSha256(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
    });
}

class LedgerReader
{
public:
    LedgerReader(std::string_view source_name, const ParseLimits& limits)
        : sourceName_(source_name), limits_(limits)
    {
    }

    AllocationLedgerResult Read(std::string_view ledger_json)
    {
        if (limits_.maximumDiagnostics == 0)
        {
            diagnostics_.push_back({
                .code = "idl.invalid_parse_limits",
                .severity = DiagnosticSeverity::Error,
                .message = "The diagnostic limit must be greater than zero.",
                .sourceName = sourceName_,
            });
            return Finish();
        }
        if (ledger_json.size() > limits_.maximumManifestBytes)
        {
            Add("idl.allocation_manifest_too_large", "Allocation ledger exceeds the configured byte limit.", "/");
            return Finish();
        }
        const Json root = Json::parse(ledger_json.begin(), ledger_json.end(), nullptr, false, false);
        if (root.is_discarded())
        {
            Add("idl.allocation_json_invalid", "Allocation ledger is not valid JSON.", "/");
            return Finish();
        }
        if (!ExactObject(root, "/", {"allocation_fingerprint", "allocation_ledger", "format", "format_version"}))
            return Finish();

        const auto format = String(root, "format", "/format");
        const auto version = U32(root, "format_version", "/format_version");
        const auto fingerprint = String(root, "allocation_fingerprint", "/allocation_fingerprint");
        if (!format.has_value() || !version.has_value() || !fingerprint.has_value())
            return Finish();
        if (*format != kLedgerFormat)
        {
            Add("idl.allocation_format_invalid", "Allocation ledger format identifier is not supported.", "/format");
            return Finish();
        }
        if (*version != kLedgerFormatVersion)
        {
            Add("idl.allocation_version_unsupported", "Allocation ledger version is not supported.", "/format_version");
            return Finish();
        }
        if (!IsLowerHexSha256(*fingerprint))
        {
            Add("idl.allocation_fingerprint_invalid", "Allocation fingerprint must be lowercase SHA-256 hex.", "/allocation_fingerprint");
            return Finish();
        }

        const auto data_value = root.find("allocation_ledger");
        auto data = ReadData(*data_value, "/allocation_ledger");
        if (!data.has_value()) return Finish();
        if (!detail::ValidateAllocationLedgerData(*data, sourceName_, limits_, diagnostics_))
            return Finish();
        const std::string payload = LedgerDataJson(*data).dump();
        if (detail::Sha256Hex(payload) != *fingerprint)
        {
            Add("idl.allocation_fingerprint_mismatch", "Allocation fingerprint does not match the canonical ledger payload.", "/allocation_fingerprint");
            return Finish();
        }
        ledger_ = detail::CreateAllocationLedger(std::move(*data));
        return Finish();
    }

private:
    bool ExactObject(
        const Json& value,
        std::string_view path,
        std::initializer_list<std::string_view> expected
    )
    {
        if (!value.is_object())
        {
            Add("idl.allocation_type_invalid", "Expected JSON object.", path);
            return false;
        }
        for (const auto key : expected)
        {
            if (!value.contains(std::string(key)))
            {
                Add("idl.allocation_member_missing", "Required allocation member is missing.", path);
                return false;
            }
        }
        for (auto member = value.begin(); member != value.end(); ++member)
        {
            const bool known = std::any_of(expected.begin(), expected.end(), [&member](std::string_view key) {
                return member.key() == key;
            });
            if (!known)
            {
                Add("idl.allocation_member_unknown", "Unknown allocation ledger member.", path);
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> String(
        const Json& object,
        std::string_view key,
        std::string_view path
    )
    {
        const auto found = object.find(std::string(key));
        if (found == object.end() || !found->is_string())
        {
            Add("idl.allocation_type_invalid", "Expected JSON string.", path);
            return std::nullopt;
        }
        return found->get<std::string>();
    }

    std::optional<std::uint64_t> Unsigned(const Json& value, std::string_view path)
    {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (value.is_number_integer())
        {
            const auto signed_value = value.get<std::int64_t>();
            if (signed_value >= 0) return static_cast<std::uint64_t>(signed_value);
        }
        Add("idl.allocation_type_invalid", "Expected unsigned integer.", path);
        return std::nullopt;
    }

    std::optional<std::uint32_t> U32(
        const Json& object,
        std::string_view key,
        std::string_view path
    )
    {
        const auto found = object.find(std::string(key));
        if (found == object.end())
        {
            Add("idl.allocation_member_missing", "Required allocation integer is missing.", path);
            return std::nullopt;
        }
        const auto value = Unsigned(*found, path);
        if (!value.has_value()) return std::nullopt;
        if (*value > std::numeric_limits<std::uint32_t>::max())
        {
            Add("idl.allocation_integer_out_of_range", "Allocation integer must fit in u32.", path);
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value);
    }

    std::optional<RegistryKind> ReadKind(std::string_view name, std::string_view path)
    {
        if (name == "model") return RegistryKind::Model;
        if (name == "field") return RegistryKind::Field;
        if (name == "semantic_type") return RegistryKind::SemanticType;
        if (name == "wire_type") return RegistryKind::WireType;
        if (name == "storage_codec") return RegistryKind::StorageCodec;
        if (name == "enum") return RegistryKind::Enum;
        if (name == "enum_value") return RegistryKind::EnumValue;
        if (name == "command") return RegistryKind::Command;
        if (name == "event") return RegistryKind::Event;
        Add("idl.allocation_enum_invalid", "Unknown allocation registry kind.", path);
        return std::nullopt;
    }

    std::optional<AllocationLedgerData> ReadData(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"format_version", "registries"}))
            return std::nullopt;
        const auto version = U32(value, "format_version", path + "/format_version");
        const auto registries = value.find("registries");
        if (!version.has_value()) return std::nullopt;
        if (!registries->is_array())
        {
            Add("idl.allocation_type_invalid", "Allocation registries must be an array.", path + "/registries");
            return std::nullopt;
        }
        if (registries->size() > limits_.maximumModels + limits_.maximumEnums + 7)
        {
            Add("idl.allocation_registry_limit_exceeded", "Allocation registry count exceeds the configured limit.", path + "/registries");
            return std::nullopt;
        }
        AllocationLedgerData data{.formatVersion = *version};
        data.registries.reserve(registries->size());
        for (std::size_t index = 0; index < registries->size(); ++index)
        {
            auto registry = ReadRegistry(
                (*registries)[index],
                path + "/registries/" + std::to_string(index)
            );
            if (!registry.has_value()) return std::nullopt;
            data.registries.push_back(std::move(*registry));
        }
        return data;
    }

    std::optional<AllocationRegistry> ReadRegistry(const Json& value, const std::string& path)
    {
        if (!ExactObject(
                value,
                path + "/",
                {"active", "enum_id", "kind", "model_type_id", "retired_ids", "retired_names"}
            ))
        {
            return std::nullopt;
        }
        const auto kind_name = String(value, "kind", path + "/kind");
        const auto enum_id = value.find("enum_id");
        const auto model_type_id = value.find("model_type_id");
        const auto active = value.find("active");
        const auto retired_ids = value.find("retired_ids");
        const auto retired_names = value.find("retired_names");
        if (!kind_name.has_value()) return std::nullopt;
        const auto kind = ReadKind(*kind_name, path + "/kind");
        if (!kind.has_value()) return std::nullopt;
        if (!active->is_array() || !retired_ids->is_array() || !retired_names->is_array())
        {
            Add("idl.allocation_type_invalid", "Allocation entries must be arrays.", path);
            return std::nullopt;
        }
        const std::size_t active_limit = *kind == RegistryKind::Field
            ? limits_.maximumFieldsPerModel
            : *kind == RegistryKind::EnumValue
                ? limits_.maximumEnumValuesPerEnum
                : limits_.maximumReservations;
        if (active->size() > active_limit)
        {
            Add("idl.allocation_active_limit_exceeded", "Active allocation count exceeds the configured limit.", path);
            return std::nullopt;
        }
        if (retired_ids->size() + retired_names->size() > limits_.maximumReservations)
        {
            Add("idl.allocation_reservation_limit_exceeded", "Allocation retirement count exceeds the configured limit.", path);
            return std::nullopt;
        }
        std::optional<ModelTypeId> model;
        std::optional<EnumId> enumeration;
        if (!model_type_id->is_null())
        {
            const auto numeric = Unsigned(*model_type_id, path + "/model_type_id");
            if (!numeric.has_value() || *numeric > std::numeric_limits<std::uint32_t>::max())
            {
                if (numeric.has_value())
                    Add("idl.allocation_integer_out_of_range", "Model type ID must fit in u32.", path + "/model_type_id");
                return std::nullopt;
            }
            model = ModelTypeId{static_cast<std::uint32_t>(*numeric)};
        }
        if (!enum_id->is_null())
        {
            const auto numeric = Unsigned(*enum_id, path + "/enum_id");
            if (!numeric.has_value() || *numeric > std::numeric_limits<std::uint32_t>::max())
            {
                if (numeric.has_value())
                    Add("idl.allocation_integer_out_of_range", "Enum ID must fit in u32.", path + "/enum_id");
                return std::nullopt;
            }
            enumeration = EnumId{static_cast<std::uint32_t>(*numeric)};
        }
        AllocationRegistry registry{.scope = {.kind = *kind, .modelTypeId = model, .enumId = enumeration}};
        for (std::size_t index = 0; index < active->size(); ++index)
        {
            auto entry = ReadActive((*active)[index], path + "/active/" + std::to_string(index));
            if (!entry.has_value()) return std::nullopt;
            registry.active.push_back(std::move(*entry));
        }
        for (std::size_t index = 0; index < retired_ids->size(); ++index)
        {
            auto entry = ReadRetiredId((*retired_ids)[index], path + "/retired_ids/" + std::to_string(index));
            if (!entry.has_value()) return std::nullopt;
            registry.retiredIds.push_back(std::move(*entry));
        }
        for (std::size_t index = 0; index < retired_names->size(); ++index)
        {
            auto entry = ReadRetiredName((*retired_names)[index], path + "/retired_names/" + std::to_string(index));
            if (!entry.has_value()) return std::nullopt;
            registry.retiredNames.push_back(std::move(*entry));
        }
        return registry;
    }

    std::optional<ActiveAllocation> ReadActive(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "id", "owner_capsule"}))
            return std::nullopt;
        const auto id = U32(value, "id", path + "/id");
        const auto name = String(value, "canonical_name", path + "/canonical_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        if (!id.has_value() || !name.has_value() || !owner.has_value()) return std::nullopt;
        return ActiveAllocation{.id = *id, .canonicalName = *name, .ownerCapsule = *owner};
    }

    std::optional<RetiredIdAllocation> ReadRetiredId(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"id", "owner_capsule"}))
            return std::nullopt;
        const auto id = U32(value, "id", path + "/id");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        if (!id.has_value() || !owner.has_value()) return std::nullopt;
        return RetiredIdAllocation{.id = *id, .ownerCapsule = *owner};
    }

    std::optional<RetiredNameAllocation> ReadRetiredName(const Json& value, const std::string& path)
    {
        if (!ExactObject(value, path + "/", {"canonical_name", "owner_capsule"}))
            return std::nullopt;
        const auto name = String(value, "canonical_name", path + "/canonical_name");
        const auto owner = String(value, "owner_capsule", path + "/owner_capsule");
        if (!name.has_value() || !owner.has_value()) return std::nullopt;
        return RetiredNameAllocation{.canonicalName = *name, .ownerCapsule = *owner};
    }

    void Add(std::string code, std::string message, std::string_view path)
    {
        if (diagnostics_.size() >= limits_.maximumDiagnostics) return;
        diagnostics_.push_back({
            .code = std::move(code),
            .severity = DiagnosticSeverity::Error,
            .message = std::move(message) + " [" + std::string(path) + "]",
            .sourceName = sourceName_,
        });
    }

    AllocationLedgerResult Finish()
    {
        if (!diagnostics_.empty())
            return {.ledger = std::nullopt, .diagnostics = std::move(diagnostics_)};
        return {.ledger = std::move(ledger_), .diagnostics = {}};
    }

    std::string sourceName_;
    const ParseLimits& limits_;
    std::optional<AllocationLedger> ledger_;
    std::vector<Diagnostic> diagnostics_;
};
}

namespace detail
{
AllocationLedger CreateAllocationLedger(AllocationLedgerData data)
{
    const Json payload_json = LedgerDataJson(data);
    const std::string payload = payload_json.dump();
    const std::string fingerprint = Sha256Hex(payload);
    const Json envelope = {
        {"allocation_fingerprint", fingerprint},
        {"allocation_ledger", payload_json},
        {"format", kLedgerFormat},
        {"format_version", kLedgerFormatVersion},
    };
    return {
        .data = std::move(data),
        .canonicalJson = envelope.dump(),
        .allocationFingerprint = fingerprint,
    };
}
}

AllocationLedgerResult ParseAllocationLedger(
    std::string_view source_name,
    std::string_view ledger_json,
    const ParseLimits& limits
)
{
    return LedgerReader(source_name, limits).Read(ledger_json);
}
}
