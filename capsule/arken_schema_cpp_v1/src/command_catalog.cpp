#include "arken_schema_cpp_v1/command_catalog.hpp"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>

namespace arken::schema::cpp::v1 {
namespace {
namespace idl = arken::schema::idl::v1;
using Json = nlohmann::json;
void require(bool valid, const std::string &message) {
  if (!valid)
    throw std::invalid_argument(message);
}

// Canonical nonnegative decimal strings no larger than the supplied bound.
std::string decimal_bound(std::string_view maximum, bool zero) {
  std::string pattern = zero ? "0|" : "";
  if (maximum.size() > 1)
    pattern += "[1-9][0-9]{0," + std::to_string(maximum.size() - 2) + "}|";
  for (std::size_t i = 0; i < maximum.size(); ++i) {
    const char low = i == 0 ? '1' : '0';
    const char high = static_cast<char>(maximum[i] - 1);
    if (high < low)
      continue;
    pattern += std::string(maximum.substr(0, i));
    pattern += low == high ? std::string(1, low)
                           : std::string("[") + low + "-" + high + "]";
    if (i + 1 < maximum.size())
      pattern += "[0-9]{" + std::to_string(maximum.size() - i - 1) + "}";
    pattern += "|";
  }
  return "(?:" + pattern + std::string(maximum) + ")";
}

const idl::SemanticTypeDefinition &type_for(const idl::SemanticIr &ir,
                                            const idl::SemanticField &field) {
  const auto found =
      std::ranges::find(ir.semanticTypes, field.type.semanticTypeId,
                        &idl::SemanticTypeDefinition::semanticTypeId);
  require(found != ir.semanticTypes.end(),
          "missing semantic type for " + field.name);
  require(found->kind == field.type.kind,
          "inconsistent semantic type for " + field.name);
  return *found;
}

Json scalar_schema(const idl::SemanticIr &ir, const idl::SemanticField &field) {
  require(!field.redacted &&
              field.representation.fieldKind != idl::FieldKind::Secret,
          "secret/redacted field cannot be published: " + field.name);
  const auto &type = type_for(ir, field);
  Json schema = {{"x-arken-semantic-type", type.qualifiedName}};
  using K = idl::SemanticTypeKind;
  switch (type.kind) {
  case K::Bool:
    schema["type"] = "boolean";
    break;
  case K::U8:
  case K::U16:
  case K::U32:
    schema["type"] = "integer";
    schema["minimum"] = 0;
    schema["maximum"] = type.kind == K::U8    ? 255u
                        : type.kind == K::U16 ? 65535u
                                              : 4294967295u;
    break;
  case K::I8:
  case K::I16:
  case K::I32:
    schema["type"] = "integer";
    schema["minimum"] = type.kind == K::I8 ? -128
                        : type.kind == K::I16
                            ? -32768
                            : std::numeric_limits<std::int32_t>::min();
    schema["maximum"] = type.kind == K::I8 ? 127
                        : type.kind == K::I16
                            ? 32767
                            : std::numeric_limits<std::int32_t>::max();
    break;
  case K::U64:
    schema["type"] = "string";
    schema["pattern"] = "^" + decimal_bound("18446744073709551615", true) + "$";
    schema["x-arken-encoding"] = "u64-decimal";
    break;
  case K::I64:
    schema["type"] = "string";
    schema["pattern"] = "^(?:" + decimal_bound("9223372036854775807", true) +
                        "|-" + decimal_bound("9223372036854775808", false) +
                        ")$";
    schema["x-arken-encoding"] = "i64-decimal";
    break;
  case K::F64:
    schema["type"] = "number";
    break;
  case K::Text:
    schema["type"] = "string";
    schema["x-arken-normalization"] =
        idl::NormalizationKindName(type.normalization);
    break;
  case K::FixedId:
    require(type.fixedSize > 0 && type.fixedSize <= 64,
            "unsupported fixed identifier width: " + field.name);
    schema["type"] = "string";
    schema["pattern"] =
        "^[0-9a-f]{" + std::to_string(type.fixedSize * 2) + "}$";
    schema["x-arken-encoding"] = "lowercase-hex";
    break;
  case K::Blob:
    schema["type"] = "string";
    schema["pattern"] = "^(?:[0-9a-f]{2})*$";
    schema["x-arken-encoding"] = "lowercase-hex-bytes";
    break;
  case K::Enum: {
    require(type.definingEnum.has_value(),
            "missing enum declaration: " + field.name);
    const auto enumeration = std::ranges::find(ir.enums, *type.definingEnum,
                                               &idl::SemanticEnum::handle);
    require(enumeration != ir.enums.end(),
            "unknown enum declaration: " + field.name);
    const auto wire = std::ranges::find(ir.wireTypes, type.wireType,
                                        &idl::WireTypeDefinition::handle);
    require(wire != ir.wireTypes.end(),
            "unknown enum wire type: " + field.name);
    const bool text = wire->kind == idl::WirePrimitiveKind::Utf8;
    schema["type"] = text ? "string" : "integer";
    schema["enum"] = Json::array();
    for (const auto &value : enumeration->values) {
      if (!field.allowedEnumValueIds.empty() &&
          std::ranges::find(field.allowedEnumValueIds, value.valueId) ==
              field.allowedEnumValueIds.end())
        continue;
      schema["enum"].push_back(text ? Json(value.name)
                                    : Json(value.valueId.value));
    }
    require(!schema["enum"].empty(), "empty enum domain: " + field.name);
    break;
  }
  default:
    throw std::invalid_argument("unsupported tool JSON semantic type: " +
                                type.qualifiedName);
  }
  require(
      !field.minimum || field.cardinality == idl::FieldCardinality::Repeated,
      "scalar @min has no native validation contract; use a numeric check: " +
          field.name);
  for (const auto &check : field.checks) {
    if (type.kind == K::Enum && check.kind == idl::CheckKind::EnumDomain) {
      const auto enumeration = std::ranges::find(ir.enums, *type.definingEnum,
                                                 &idl::SemanticEnum::handle);
      require(check.generated && check.unit == idl::CheckUnit::None &&
                  check.value == enumeration->qualifiedName,
              "enum check differs from resolved domain: " + field.name);
      continue; // The resolved enum above projects this exact generated domain.
    }
    if (type.kind == K::FixedId && check.kind == idl::CheckKind::FixedLength &&
        check.unit == idl::CheckUnit::Bytes &&
        check.value == std::to_string(type.fixedSize))
      continue; // The hex pattern already enforces the exact byte width.
    // Preserve native length units explicitly. JSON Schema character counts
    // must not stand in for UTF-8 byte budgets; generated codecs enforce the
    // owning binding, including these declared constraints.
    using C = idl::CheckKind;
    if (type.kind == K::Blob &&
        (check.kind == C::MinimumLength || check.kind == C::MaximumLength ||
         check.kind == C::FixedLength)) {
      require(check.unit == idl::CheckUnit::Bytes,
              "unsupported byte length unit: " + field.name);
      const auto bound = Json::parse(check.value, nullptr, false);
      require(bound.is_number_unsigned() ||
                  (bound.is_number_integer() && bound.get<std::int64_t>() >= 0),
              "invalid byte length bound: " + field.name);
      const auto value = bound.get<std::uint64_t>();
      require(value <= std::numeric_limits<std::uint64_t>::max() / 2,
              "encoded byte length overflow: " + field.name);
      auto &length = schema["x-arken-length"]["bytes"];
      const auto key = check.kind == C::MinimumLength ? "minimum"
                       : check.kind == C::MaximumLength ? "maximum" : "fixed";
      require(!length.contains(key) || length[key] == bound,
              "conflicting byte length bound: " + field.name);
      length[key] = bound;
      if (check.kind != C::MaximumLength) schema["minLength"] = value * 2;
      if (check.kind != C::MinimumLength) schema["maxLength"] = value * 2;
      schema["x-arken-native-validation"] = true;
      continue;
    }
    if (type.kind == K::Text &&
        (check.kind == C::MinimumLength || check.kind == C::MaximumLength ||
         check.kind == C::FixedLength)) {
      require(check.unit == idl::CheckUnit::None ||
                  check.unit == idl::CheckUnit::Utf8Bytes,
              "unsupported text length unit: " + field.name);
      const auto bound = Json::parse(check.value, nullptr, false);
      require(bound.is_number_unsigned() ||
                  (bound.is_number_integer() && bound.get<std::int64_t>() >= 0),
              "invalid text length bound: " + field.name);
      const auto unit = check.unit == idl::CheckUnit::Utf8Bytes
                            ? "utf8-bytes" : "unicode-scalars";
      auto &length = schema["x-arken-length"][unit];
      const auto key = check.kind == C::MinimumLength ? "minimum"
                       : check.kind == C::MaximumLength ? "maximum" : "fixed";
      require(!length.contains(key) || length[key] == bound,
              "conflicting text length bound: " + field.name);
      length[key] = bound;
      schema["x-arken-native-validation"] = true;
      continue;
    }
    require(check.unit == idl::CheckUnit::None,
            "unsupported check unit: " + field.name + "." + check.name);
    if (check.kind == C::MinimumInteger || check.kind == C::MaximumInteger ||
        check.kind == C::MinimumReal || check.kind == C::MaximumReal) {
      require(schema["type"] == "integer" || schema["type"] == "number",
              "numeric check on nonnumeric JSON: " + field.name);
      const auto bound = Json::parse(check.value, nullptr, false);
      require(bound.is_number(), "invalid numeric check: " + field.name);
      const auto key =
          check.kind == C::MinimumInteger || check.kind == C::MinimumReal
              ? "minimum"
              : "maximum";
      if (!schema.contains(key) ||
          (std::string_view(key) == "minimum" ? bound > schema[key]
                                              : bound < schema[key]))
        schema[key] = bound;
    } else
      throw std::invalid_argument("unsupported tool JSON check: " + field.name +
                                  "." + check.name);
  }
  require(!schema.contains("minimum") || !schema.contains("maximum") ||
              schema["minimum"] <= schema["maximum"],
          "empty numeric domain: " + field.name);
  require(type.kind != K::Blob || schema.contains("maxLength"),
          "unbounded bytes cannot be published: " + field.name);
  require(!schema.contains("minLength") || !schema.contains("maxLength") ||
              schema["minLength"] <= schema["maxLength"],
          "empty byte length domain: " + field.name);
  return schema;
}

Json model_schema(const idl::SemanticManifest &manifest,
                  const std::optional<idl::SemanticModelReference> &reference) {
  Json schema = {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                 {"type", "object"},
                 {"additionalProperties", false},
                 {"properties", Json::object()}};
  if (!reference)
    return schema;
  const auto model = std::ranges::find(manifest.ir.models, reference->model,
                                       &idl::SemanticModel::handle);
  require(model != manifest.ir.models.end() &&
              model->typeId == reference->typeId &&
              model->qualifiedName == reference->canonicalName,
          "command model reference mismatch");
  require(!model->external && model->oneOfs.empty() &&
              model->temporalContracts.empty(),
          "unsupported tool model constraints: " + model->qualifiedName);
  schema["description"] = model->description;
  schema["x-arken-model"] = model->qualifiedName;
  Json required = Json::array();
  std::set<std::string> names;
  for (const auto &field : model->fields) {
    require(field.serialization == idl::SerializationPolicy::Include,
            "nonserialized tool field: " + field.name);
    require(names.insert(field.name).second,
            "duplicate tool field: " + field.name);
    auto value = scalar_schema(manifest.ir, field);
    if (field.cardinality == idl::FieldCardinality::Repeated) {
      require(field.maximumItems.has_value() && *field.maximumItems > 0,
              "unbounded tool array: " + field.name);
      value = {{"type", "array"},
               {"items", std::move(value)},
               {"maxItems", *field.maximumItems}};
      if (field.minimum) {
        require(*field.minimum <= *field.maximumItems,
                "empty array cardinality domain: " + field.name);
        value["minItems"] = *field.minimum;
        if (*field.minimum > 0)
          required.push_back(field.name);
      }
    } else if (field.required)
      required.push_back(field.name);
    schema["properties"][field.name] = std::move(value);
  }
  if (!required.empty())
    schema["required"] = std::move(required);
  return schema;
}

void append_model_description(std::string &description, const Json &schema,
                              std::string_view label) {
  const auto found = schema.find("description");
  if (found == schema.end()) return;
  const auto &text = found->get_ref<const std::string &>();
  if (text.empty()) return;
  constexpr std::size_t maximumExcerptBytes = 768;
  auto end = std::min(text.size(), maximumExcerptBytes);
  // Preserve the full schema text; discovery gets a UTF-8-safe excerpt only.
  if (end < text.size())
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
  description += "\n\n";
  description += label;
  description.append(text.data(), end);
  if (end < text.size())
    description += " [truncated; full text in model schema]";
}
} // namespace

CommandCatalogResult
GenerateCommandCatalog(const idl::SemanticManifest &manifest) {
  CommandCatalogResult result;
  try {
    require(!manifest.semanticFingerprint.empty(),
            "semantic fingerprint required");
    require(manifest.ir.commands.size() <= 4096,
            "command catalog count exceeds4096");
    std::vector<const idl::SemanticCommand *> commands;
    for (const auto &command : manifest.ir.commands)
      commands.push_back(&command);
    std::ranges::sort(commands, {}, [](const auto *command) {
      return command->qualifiedName;
    });
    Json rows = Json::array();
    std::set<std::string> names;
    for (const auto *command : commands) {
      require(!command->ownerCapsule.empty() &&
                  names.insert(command->qualifiedName).second,
              "duplicate/unowned catalog command");
      require(command->streaming == idl::CommandStreaming::Unary,
              "streaming command requires an operation adapter: " +
                  command->qualifiedName);
      Json errors = Json::array();
      for (const auto &error : command->errors)
        errors.push_back(model_schema(manifest, error));
      auto input = model_schema(manifest, command->request);
      auto output = model_schema(manifest, command->response);
      std::string description = "Invoke " + command->qualifiedName +
                                " through its owning capsule.";
      append_model_description(description, input, "Request model: ");
      append_model_description(description, output, "Result model: ");
      rows.push_back(
          {{"name", command->qualifiedName},
           {"commandId", command->commandId.value},
           {"ownerCapsule", command->ownerCapsule},
           {"description", std::move(description)},
           {"inputSchema", std::move(input)},
           {"outputSchema", std::move(output)},
           {"errorSchemas", std::move(errors)},
           {"idempotency", idl::CommandIdempotencyName(command->idempotency)},
           {"streaming", idl::CommandStreamingName(command->streaming)}});
    }
    result.json = Json{{"schema", "arken.schema.command-catalog.v1"},
                       {"jsonProfile", "arken.schema.tool-json.v1"},
                       {"semanticFingerprint", manifest.semanticFingerprint},
                       {"commands", std::move(rows)}}
                      .dump(2) +
                  "\n";
    require(result.json.size() <= maximumCommandCatalogBytes,
            "command catalog exceeds4MiB");
  } catch (const std::exception &error) {
    result.json.clear();
    result.diagnostics.push_back({.code = "cpp.command_catalog_unsupported",
                                  .message = error.what(),
                                  .sourceName = "command-catalog"});
  }
  return result;
}

CommandCatalogResult
GenerateContextCommandCatalog(const idl::SemanticManifest &manifest) {
  auto result = GenerateCommandCatalog(manifest);
  if (!result.ok()) return result;
  try {
    auto catalog = Json::parse(result.json);
    catalog["schema"] = "arken.schema.context-command-catalog.v1";
    for (auto &command : catalog["commands"]) {
      auto native = std::move(command["inputSchema"]);
      command["inputSchema"] = {
          {"$schema", "https://json-schema.org/draft/2020-12/schema"},
          {"type", "object"}, {"additionalProperties", false},
          {"required", Json::array({"context_id", "context_generation", "arguments"})},
          {"properties", {
              {"context_id", {{"type", "string"}, {"minLength", 1}, {"maxLength", 180},
                  {"description", "Exact active Shell command context identity."}}},
              {"context_generation", {{"type", "string"},
                  {"pattern", "^" + decimal_bound("18446744073709551615", false) + "$"},
                  {"description", "Observed context generation; stale requests reject before delivery."}}},
              {"arguments", std::move(native)}
          }}
      };
    }
    result.json = catalog.dump(2) + "\n";
    require(result.json.size() <= maximumCommandCatalogBytes,
            "context command catalog exceeds4MiB");
  } catch (const std::exception &error) {
    result.json.clear();
    result.diagnostics.push_back({.code = "cpp.context_command_catalog_unsupported",
        .message = error.what(), .sourceName = "context-command-catalog"});
  }
  return result;
}
} // namespace arken::schema::cpp::v1
