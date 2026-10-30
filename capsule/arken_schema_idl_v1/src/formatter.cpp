#include "arken_schema_idl_v1/idl.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace arken::schema::idl::v1 {
namespace {
std::string QuoteString(std::string_view value) {
  static constexpr char hexadecimal[] = "0123456789abcdef";
  std::string output = "\"";
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (character < 0x20) {
        output += "\\u00";
        output.push_back(hexadecimal[(character >> 4) & 0x0f]);
        output.push_back(hexadecimal[character & 0x0f]);
      } else {
        output.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  output.push_back('"');
  return output;
}

void AppendReservations(
    std::string &output, std::string_view indentation,
    const std::vector<ReservedIdDeclaration> &reserved_ids,
    const std::vector<ReservedNameDeclaration> &reserved_names) {
  std::vector<const ReservedIdDeclaration *> ids;
  ids.reserve(reserved_ids.size());
  for (const auto &reserved : reserved_ids)
    ids.push_back(&reserved);
  std::sort(ids.begin(), ids.end(), [](const auto *lhs, const auto *rhs) {
    if (lhs->registry != rhs->registry)
      return lhs->registry < rhs->registry;
    return lhs->value < rhs->value;
  });
  for (const auto *reserved : ids) {
    output += indentation;
    output += "reserved " + std::string(RegistryKindName(reserved->registry)) +
              " id " + std::to_string(reserved->value) + ";\n";
  }

  std::vector<const ReservedNameDeclaration *> names;
  names.reserve(reserved_names.size());
  for (const auto &reserved : reserved_names)
    names.push_back(&reserved);
  std::sort(names.begin(), names.end(), [](const auto *lhs, const auto *rhs) {
    if (lhs->registry != rhs->registry)
      return lhs->registry < rhs->registry;
    return lhs->value < rhs->value;
  });
  for (const auto *reserved : names) {
    output += indentation;
    output += "reserved " + std::string(RegistryKindName(reserved->registry)) +
              " name " + reserved->value + ";\n";
  }
}

void AppendIdentifierList(std::string &output,
                          const std::vector<std::string> &values) {
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      output += ", ";
    output += values[index];
  }
}

void AppendFieldCatalogIdentity(
    std::string &output, std::string_view indentation,
    const CatalogFieldIdentityDeclaration &identity) {
  output += indentation;
  output += "catalog_identity {\n";
  output += std::string(indentation) + "  registry_key " +
            QuoteString(identity.registryKey) + ";\n";
  output += std::string(indentation) + "  field_name " +
            QuoteString(identity.fieldName) + ";\n";
  output += std::string(indentation) + "  declaration_order " +
            std::to_string(identity.declarationOrder) + ";\n";
  output += indentation;
  output += "}\n";
}

void AppendCatalogMemberIdentity(
    std::string &output, std::string_view indentation,
    const std::optional<CatalogMemberIdentityDeclaration> &identity) {
  if (!identity.has_value())
    return;
  output += std::string(indentation) + "catalog_name " +
            QuoteString(identity->name) + ";\n";
  output += std::string(indentation) + "catalog_order " +
            std::to_string(identity->declarationOrder) + ";\n";
}

bool CatalogMemberLess(
    const std::optional<CatalogMemberIdentityDeclaration> &lhs,
    std::string_view lhs_name,
    const std::optional<CatalogMemberIdentityDeclaration> &rhs,
    std::string_view rhs_name) {
  if (lhs.has_value() != rhs.has_value())
    return lhs.has_value();
  if (lhs.has_value() && lhs->declarationOrder != rhs->declarationOrder)
    return lhs->declarationOrder < rhs->declarationOrder;
  return lhs_name < rhs_name;
}

void AppendFieldRepresentation(
    std::string &output, std::string_view indentation,
    const FieldRepresentationDeclaration &representation,
    std::uint32_t edition) {
  output += indentation;
  output += "representation {\n";
  if (!representation.fieldKind.empty())
    output += std::string(indentation) + "  field_kind " +
              representation.fieldKind + ";\n";
  if (edition == 2) {
    if (!representation.normalization.empty() &&
        representation.normalization != "none")
      output += std::string(indentation) + "  normalization " +
                representation.normalization + ";\n";
    output += indentation;
    output += "}\n";
    return;
  }
  output += std::string(indentation) + "  wire " + representation.wireTypeName +
            ";\n";
  output += std::string(indentation) + "  wire_size " +
            std::to_string(representation.wireSize) + ";\n";
  output += std::string(indentation) + "  storage " +
            representation.storageCodecName + ";\n";
  output += std::string(indentation) + "  storage_size " +
            std::to_string(representation.storageSize) + ";\n";
  output += std::string(indentation) + "  precision " +
            std::to_string(representation.precision) + ";\n";
  output += std::string(indentation) + "  scale " +
            std::to_string(representation.scale) + ";\n";
  output += std::string(indentation) + "  normalization " +
            representation.normalization + ";\n";
  output += indentation;
  output += "}\n";
}

std::string_view DefaultWireName(const SemanticTypeDeclaration &type) {
  if (type.kind == "bool")
    return "bool";
  if (type.kind == "u8")
    return "u8";
  if (type.kind == "u16")
    return "u16";
  if (type.kind == "u32")
    return "u32";
  if (type.kind == "u64")
    return "u64";
  if (type.kind == "i8")
    return "i8";
  if (type.kind == "i16")
    return "i16";
  if (type.kind == "i32")
    return "i32";
  if (type.kind == "i64" || type.kind == "timestamp")
    return "i64";
  if (type.kind == "f32")
    return "f32";
  if (type.kind == "f64")
    return "f64";
  if (type.kind == "text" || type.kind == "json_text")
    return "utf8";
  if (type.kind == "fixed_id")
    return "fixed_bytes";
  if (type.kind == "decimal")
    return "decimal128";
  if (type.kind == "blob")
    return type.fixedSize == 0u ? "bytes" : "fixed_bytes";
  return {};
}

std::optional<std::uint32_t>
DefaultFixedSize(const SemanticTypeDeclaration &type) {
  const std::string_view wire = type.wireTypeName.empty()
                                    ? DefaultWireName(type)
                                    : std::string_view(type.wireTypeName);
  if (type.kind == "fixed_id" || type.kind == "blob")
    return std::nullopt;
  if (wire == "bool" || wire == "u8" || wire == "i8")
    return 1u;
  if (wire == "u16" || wire == "i16")
    return 2u;
  if (wire == "u32" || wire == "i32" || wire == "f32")
    return 4u;
  if (wire == "u64" || wire == "i64" || wire == "f64")
    return 8u;
  if (wire == "decimal128")
    return 16u;
  return 0u;
}

bool HasDefaultTargets(const std::vector<std::string> &targets) {
  auto normalized = targets;
  std::sort(normalized.begin(), normalized.end());
  return normalized == std::vector<std::string>{"cpp",        "csharp",
                                                "fabric",     "http_json",
                                                "javascript", "sqlite"};
}
} // namespace

std::string Format(const Document &document) {
  std::string output;
  std::map<std::string, std::uint32_t> catalog_fragment_orders;
  if (document.edition == 2) {
    output += "schema v2;\n";
    output += "module " + document.schemaName + " v" +
              std::to_string(document.schemaVersion) + ";\n";
  } else {
    output += "edition " + std::to_string(document.edition) + ";\n";
    output += "schema " + document.schemaName + " v" +
              std::to_string(document.schemaVersion) + ";\n";
  }
  output += "owner " + document.ownerCapsule + ";\n";
  if (document.catalog.has_value()) {
    output += "catalog " + QuoteString(document.catalog->catalogId) +
              " @catalog_version(" + std::to_string(document.catalog->version) +
              ") {\n";
    std::vector<const CatalogFragmentDeclaration *> fragments;
    fragments.reserve(document.catalog->fragments.size());
    for (const auto &fragment : document.catalog->fragments) {
      fragments.push_back(&fragment);
      catalog_fragment_orders.emplace(fragment.fragmentId,
                                      fragment.declarationOrder);
    }
    std::sort(fragments.begin(), fragments.end(),
              [](const auto *lhs, const auto *rhs) {
                if (lhs->declarationOrder != rhs->declarationOrder)
                  return lhs->declarationOrder < rhs->declarationOrder;
                return lhs->fragmentId < rhs->fragmentId;
              });
    for (const auto *fragment : fragments) {
      output += "  fragment " + QuoteString(fragment->fragmentId) + " {\n";
      output += "    owner_capsule " + fragment->ownerCapsule + ";\n";
      output += "    version " + std::to_string(fragment->version) + ";\n";
      output += "    declaration_order " +
                std::to_string(fragment->declarationOrder) + ";\n";
      output += "  }\n";
    }
    output += "}\n";
  }

  std::vector<const ImportDeclaration *> imports;
  imports.reserve(document.imports.size());
  for (const auto &import : document.imports)
    imports.push_back(&import);
  std::sort(
      imports.begin(), imports.end(),
      [](const auto *lhs, const auto *rhs) { return lhs->path < rhs->path; });
  for (const auto *import : imports)
    output += "import " + QuoteString(import->path) + ";\n";
  AppendReservations(output, "", document.reservedIds, document.reservedNames);

  if (document.edition == 1) {
    std::vector<const WireTypeDeclaration *> wire_types;
    wire_types.reserve(document.wireTypes.size());
    for (const auto &wire : document.wireTypes)
      wire_types.push_back(&wire);
    std::sort(wire_types.begin(), wire_types.end(),
              [](const auto *lhs, const auto *rhs) {
                if (lhs->wireTypeId != rhs->wireTypeId)
                  return lhs->wireTypeId < rhs->wireTypeId;
                return lhs->name < rhs->name;
              });
    for (const auto *wire : wire_types) {
      output += "\nwire_type " + wire->name + " @wire_type_id(" +
                std::to_string(wire->wireTypeId) + ") {\n";
      output += "  bit_width " + std::to_string(wire->bitWidth) + ";\n";
      output += "  fixed_size " + std::to_string(wire->fixedSize) + ";\n";
      output += "  byte_order " + wire->byteOrder + ";\n";
      output += "}\n";
    }
    std::vector<const StorageCodecDeclaration *> storage_codecs;
    storage_codecs.reserve(document.storageCodecs.size());
    for (const auto &codec : document.storageCodecs)
      storage_codecs.push_back(&codec);
    std::sort(storage_codecs.begin(), storage_codecs.end(),
              [](const auto *lhs, const auto *rhs) {
                if (lhs->storageCodecId != rhs->storageCodecId)
                  return lhs->storageCodecId < rhs->storageCodecId;
                return lhs->name < rhs->name;
              });
    for (const auto *codec : storage_codecs) {
      output += "\nstorage_codec " + codec->name + " @storage_codec_id(" +
                std::to_string(codec->storageCodecId) + ") {\n";
      output += "  storage_class " + codec->storageClass + ";\n";
      output += "  fixed_size " + std::to_string(codec->fixedSize) + ";\n";
      output +=
          std::string("  exact ") + (codec->exact ? "true;\n" : "false;\n");
      output += std::string("  order_preserving ") +
                (codec->orderPreserving ? "true;\n" : "false;\n");
      output += "}\n";
    }
  }
  std::vector<const SemanticTypeDeclaration *> semantic_types;
  semantic_types.reserve(document.semanticTypes.size());
  for (const auto &type : document.semanticTypes)
    semantic_types.push_back(&type);
  std::sort(semantic_types.begin(), semantic_types.end(),
            [](const auto *lhs, const auto *rhs) {
              if (lhs->semanticTypeId != rhs->semanticTypeId)
                return lhs->semanticTypeId < rhs->semanticTypeId;
              return lhs->name < rhs->name;
            });
  for (const auto *type : semantic_types) {
    if (document.edition == 2) {
      output += "\ntype " + type->name;
      if (type->semanticTypeIdExplicit)
        output += " @id(" + std::to_string(type->semanticTypeId) + ")";
      output += " {\n";
    } else {
      output += "\nsemantic_type " + type->name + " @semantic_type_id(" +
                std::to_string(type->semanticTypeId) + ") {\n";
    }
    if (!type->semanticName.empty())
      output += "  semantic_name " + QuoteString(type->semanticName) + ";\n";
    if (!type->registryKey.empty())
      output += "  registry_key " + QuoteString(type->registryKey) + ";\n";
    output += "  kind " + type->kind + ";\n";
    const auto default_wire = DefaultWireName(*type);
    if (document.edition == 1 ||
        (!type->wireTypeName.empty() &&
         (default_wire.empty() || type->wireTypeName != default_wire)))
      output += "  wire " + type->wireTypeName + ";\n";
    if (document.edition == 1)
      output += "  storage " + type->storageCodecName + ";\n";
    const auto default_fixed_size = DefaultFixedSize(*type);
    if (document.edition == 1 ||
        (type->fixedSize != 0u && (!default_fixed_size.has_value() ||
                                   type->fixedSize != *default_fixed_size)))
      output += "  fixed_size " + std::to_string(type->fixedSize) + ";\n";
    if (document.edition == 1 || type->precision != 0u)
      output += "  precision " + std::to_string(type->precision) + ";\n";
    if (document.edition == 1 || type->scale != 0u)
      output += "  scale " + std::to_string(type->scale) + ";\n";
    if (document.edition == 1 || type->normalization != "none")
      output += "  normalization " + type->normalization + ";\n";
    if (type->jsonProfile != "none")
      output += "  json_profile " + type->jsonProfile + ";\n";
    if (type->boundaryMapping != "scalar")
      output += "  boundary_mapping " + type->boundaryMapping + ";\n";
    if (type->representation != "automatic")
      output += "  representation " + type->representation + ";\n";
    if (document.edition == 1 || type->temporalRole != "none")
      output += "  temporal_role " + type->temporalRole + ";\n";
    if (document.edition == 1 || type->presence != "explicit")
      output += "  presence " + type->presence + ";\n";
    if (document.edition == 1 || type->defaultPolicy != "none")
      output += "  default " + type->defaultPolicy + ";\n";
    if (document.edition == 1 || type->narrowing != "reject")
      output += "  narrowing " + type->narrowing + ";\n";
    if (!type->enumName.empty())
      output += "  enum " + type->enumName + ";\n";
    if (document.edition == 1 || !HasDefaultTargets(type->targets)) {
      output += "  targets ";
      auto targets = type->targets;
      std::sort(targets.begin(), targets.end());
      for (std::size_t index = 0; index < targets.size(); ++index) {
        if (index != 0)
          output += ", ";
        output += targets[index];
      }
      output += ";\n";
    }
    output += "}\n";
  }

  std::vector<const EnumDeclaration *> enums;
  enums.reserve(document.enums.size());
  for (const auto &declaration : document.enums)
    enums.push_back(&declaration);
  std::sort(enums.begin(), enums.end(), [](const auto *lhs, const auto *rhs) {
    if (lhs->enumId != rhs->enumId)
      return lhs->enumId < rhs->enumId;
    return lhs->name < rhs->name;
  });
  for (const auto *declaration : enums) {
    output += "\nenum " + declaration->name + " @enum_id(" +
              std::to_string(declaration->enumId) + ") {\n";
    if (!declaration->semanticTypeName.empty())
      output += std::string("  ") +
                (document.edition == 2 ? "type " : "semantic_type ") +
                declaration->semanticTypeName + ";\n";
    std::vector<const EnumValueDeclaration *> values;
    values.reserve(declaration->values.size());
    for (const auto &value : declaration->values)
      values.push_back(&value);
    std::sort(values.begin(), values.end(),
              [](const auto *lhs, const auto *rhs) {
                if (lhs->catalogIdentity.has_value() !=
                    rhs->catalogIdentity.has_value())
                  return lhs->catalogIdentity.has_value();
                if (lhs->catalogIdentity.has_value() &&
                    lhs->catalogIdentity->declarationOrder !=
                        rhs->catalogIdentity->declarationOrder)
                  return lhs->catalogIdentity->declarationOrder <
                         rhs->catalogIdentity->declarationOrder;
                if (lhs->valueId != rhs->valueId)
                  return lhs->valueId < rhs->valueId;
                return lhs->name < rhs->name;
              });
    for (const auto *value : values) {
      output += "  value " + value->name + " @value_id(" +
                std::to_string(value->valueId) + ")";
      if (!value->catalogIdentity.has_value())
        output += ";\n";
      else {
        output += " {\n";
        AppendCatalogMemberIdentity(output, "    ", value->catalogIdentity);
        output += "  }\n";
      }
    }
    AppendReservations(output, "  ", declaration->reservedIds,
                       declaration->reservedNames);
    output += "}\n";
  }

  std::vector<const ModelDeclaration *> models;
  models.reserve(document.models.size());
  for (const auto &model : document.models)
    models.push_back(&model);
  std::sort(models.begin(), models.end(),
            [&catalog_fragment_orders](const auto *lhs, const auto *rhs) {
              if (lhs->catalogIdentity.has_value() &&
                  rhs->catalogIdentity.has_value()) {
                const auto lhs_fragment = catalog_fragment_orders.find(
                    lhs->catalogIdentity->fragmentId);
                const auto rhs_fragment = catalog_fragment_orders.find(
                    rhs->catalogIdentity->fragmentId);
                if (lhs_fragment != catalog_fragment_orders.end() &&
                    rhs_fragment != catalog_fragment_orders.end() &&
                    lhs_fragment->second != rhs_fragment->second)
                  return lhs_fragment->second < rhs_fragment->second;
                if (lhs->catalogIdentity->fragmentId !=
                    rhs->catalogIdentity->fragmentId)
                  return lhs->catalogIdentity->fragmentId <
                         rhs->catalogIdentity->fragmentId;
                if (lhs->catalogIdentity->declarationOrder !=
                    rhs->catalogIdentity->declarationOrder)
                  return lhs->catalogIdentity->declarationOrder <
                         rhs->catalogIdentity->declarationOrder;
              }
              if (lhs->typeId != rhs->typeId)
                return lhs->typeId < rhs->typeId;
              return lhs->name < rhs->name;
            });

  for (const auto *model : models) {
    output += "\nmodel " + model->name + " @type_id(" +
              std::to_string(model->typeId) + ")";
    if (model->external)
      output += " @external";
    output += " {\n";
    if (model->catalogIdentity.has_value()) {
      const auto &identity = *model->catalogIdentity;
      output += "  catalog_identity {\n";
      output += "    fragment_id " + QuoteString(identity.fragmentId) + ";\n";
      output += "    registry_key " + QuoteString(identity.registryKey) + ";\n";
      output += "    model_id " + QuoteString(identity.modelId) + ";\n";
      output += "    logical_name " + QuoteString(identity.logicalName) + ";\n";
      output +=
          "    qualified_name " + QuoteString(identity.qualifiedName) + ";\n";
      if (!identity.domain.empty())
        output += "    domain " + QuoteString(identity.domain) + ";\n";
      output += "    declaration_order " +
                std::to_string(identity.declarationOrder) + ";\n";
      output += "  }\n";
    }
    output += "  storage_name " + model->storageName + ";\n";
    output += "  domain " + model->domain + ";\n";
    output += "  description " + QuoteString(model->description) + ";\n";
    output += "  kind " + model->kind + ";\n";
    output += "  retention " + model->retention + ";\n";
    output += "  mutation_authority " + model->mutationAuthority + ";\n";
    output += "  role " + model->role + ";\n";
    auto source_aliases = model->sourceAliases;
    std::sort(source_aliases.begin(), source_aliases.end());
    for (const auto &alias : source_aliases)
      output += "  source_alias " + QuoteString(alias) + ";\n";
    auto retired_names = model->retiredNames;
    std::sort(retired_names.begin(), retired_names.end());
    for (const auto &retired_name : retired_names)
      output += "  retired_name " + retired_name + ";\n";
    auto catalog_retired_names = model->catalogRetiredNames;
    std::sort(catalog_retired_names.begin(), catalog_retired_names.end());
    for (const auto &retired_name : catalog_retired_names)
      output += "  catalog_retired_name " + QuoteString(retired_name) + ";\n";
    output += "  name_review " + model->nameReview + ";\n";
    if (!model->nameReviewRationale.empty())
      output += "  name_review_rationale " +
                QuoteString(model->nameReviewRationale) + ";\n";
    std::vector<const FieldDeclaration *> fields;
    fields.reserve(model->fields.size());
    for (const auto &field : model->fields)
      fields.push_back(&field);
    std::sort(fields.begin(), fields.end(),
              [](const auto *lhs, const auto *rhs) {
                if (lhs->catalogIdentity.has_value() &&
                    rhs->catalogIdentity.has_value() &&
                    lhs->catalogIdentity->declarationOrder !=
                        rhs->catalogIdentity->declarationOrder)
                  return lhs->catalogIdentity->declarationOrder <
                         rhs->catalogIdentity->declarationOrder;
                if (lhs->fieldId != rhs->fieldId)
                  return lhs->fieldId < rhs->fieldId;
                return lhs->name < rhs->name;
              });
    for (const auto *field : fields) {
      if (field->primary) {
        output +=
            "  primary_id @field_id(" + std::to_string(field->fieldId) + ")";
        if (!field->catalogIdentity.has_value())
          output += ";\n";
        else {
          output += " {\n";
          AppendFieldCatalogIdentity(output, "    ", *field->catalogIdentity);
          output += "  }\n";
        }
        continue;
      }
      output += "  field " + field->name + ": " + field->typeName +
                " @field_id(" + std::to_string(field->fieldId) + ")";
      if (field->optional)
        output += " @optional";
      if (field->maximumItems.has_value())
        output += " @max_items(" + std::to_string(*field->maximumItems) + ")";
      if (field->minimum.has_value())
        output += " @min(" + std::to_string(*field->minimum) + ")";
      const bool has_cardinality_attribute = field->optional ||
                                             field->maximumItems.has_value() ||
                                             field->minimum.has_value();
      const bool has_nondefault_policy =
          field->cppName.has_value() || field->csharpName.has_value() ||
          field->storageName != field->name ||
          field->required != !field->optional || field->redacted ||
          field->serialization != "include" || field->immutable ||
          field->writeOnce || field->sourceProvenance != "canonical" ||
          field->sourceField.has_value() || !field->importTransform.empty() ||
          field->defaultValue.kind != "none" || !field->checks.empty() ||
          !field->query.operators.empty() || field->query.sortable ||
          field->query.cursor || field->representation.has_value() ||
          field->temporalRole != "inherit" ||
          !field->allowedEnumValues.empty() ||
          field->catalogIdentity.has_value();
      if (has_cardinality_attribute && !has_nondefault_policy) {
        output += ";\n";
        continue;
      }
      output += " {\n";
      if (field->cppName.has_value())
        output += "    cpp_name " + *field->cppName + ";\n";
      if (field->csharpName.has_value())
        output += "    csharp_name " + *field->csharpName + ";\n";
      if (document.edition == 1 || field->storageName != field->name)
        output += "    storage_name " + field->storageName + ";\n";
      if (document.edition == 1 || field->required != !field->optional)
        output += std::string("    presence ") +
                  (field->required ? "required;\n" : "optional;\n");
      if (document.edition == 1 || field->redacted)
        output += std::string("    redacted ") +
                  (field->redacted ? "true;\n" : "false;\n");
      if (document.edition == 1 || field->serialization != "include")
        output += "    serialization " + field->serialization + ";\n";
      if (document.edition == 1 || field->immutable)
        output += std::string("    immutable ") +
                  (field->immutable ? "true;\n" : "false;\n");
      if (document.edition == 1 || field->writeOnce)
        output += std::string("    write_once ") +
                  (field->writeOnce ? "true;\n" : "false;\n");
      if (document.edition == 1 || field->sourceProvenance != "canonical")
        output += "    source_provenance " + field->sourceProvenance + ";\n";
      if (field->temporalRole != "inherit")
        output += "    temporal_role " + field->temporalRole + ";\n";
      if (!field->allowedEnumValues.empty()) {
        auto allowed_values = field->allowedEnumValues;
        std::sort(allowed_values.begin(), allowed_values.end());
        output += "    allowed_enum_values ";
        for (std::size_t index = 0; index < allowed_values.size(); ++index) {
          if (index != 0)
            output += ", ";
          output += allowed_values[index];
        }
        output += ";\n";
      }
      if (field->sourceField.has_value())
        output +=
            "    source_field " + QuoteString(*field->sourceField) + ";\n";
      if (!field->importTransform.empty())
        output += "    import_transform " + field->importTransform + ";\n";
      if (field->representation.has_value())
        AppendFieldRepresentation(output, "    ", *field->representation,
                                  document.edition);
      if (field->catalogIdentity.has_value())
        AppendFieldCatalogIdentity(output, "    ", *field->catalogIdentity);
      if (field->defaultValue.kind != "none") {
        output += "    default {\n";
        output += "      kind " + field->defaultValue.kind + ";\n";
        if (field->defaultValue.kind == "literal")
          output +=
              "      value " + QuoteString(field->defaultValue.literal) + ";\n";
        else {
          output +=
              "      source " + field->defaultValue.boundarySource + ";\n";
          if (!field->defaultValue.catalogValue.empty())
            output += "      catalog_value " +
                      QuoteString(field->defaultValue.catalogValue) + ";\n";
        }
        output += "    }\n";
      }
      std::vector<const CheckDeclaration *> checks;
      for (const auto &check : field->checks)
        checks.push_back(&check);
      std::sort(checks.begin(), checks.end(),
                [](const auto *lhs, const auto *rhs) {
                  return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                                           rhs->catalogIdentity, rhs->name);
                });
      for (const auto *check : checks) {
        output += "    check " + check->name + " {\n";
        AppendCatalogMemberIdentity(output, "      ", check->catalogIdentity);
        output += "      kind " + check->kind + ";\n";
        if (check->unit != "none")
          output += "      unit " + check->unit + ";\n";
        output += "      value " + QuoteString(check->value) + ";\n";
        if (!check->message.empty())
          output += "      message " + QuoteString(check->message) + ";\n";
        output += "    }\n";
      }
      if (document.edition == 1 || !field->query.operators.empty() ||
          field->query.sortable || field->query.cursor) {
        output += "    query {\n";
        if (!field->query.operators.empty()) {
          auto operators = field->query.operators;
          std::sort(operators.begin(), operators.end());
          output += "      operators ";
          AppendIdentifierList(output, operators);
          output += ";\n";
        }
        if (document.edition == 1 || field->query.sortable)
          output += std::string("      sortable ") +
                    (field->query.sortable ? "true;\n" : "false;\n");
        if (document.edition == 1 || field->query.cursor)
          output += std::string("      cursor ") +
                    (field->query.cursor ? "true;\n" : "false;\n");
        output += "    }\n";
      }
      output += "  }\n";
    }

    std::vector<const KeyDeclaration *> keys;
    for (const auto &key : model->keys)
      keys.push_back(&key);
    std::sort(keys.begin(), keys.end(), [](const auto *lhs, const auto *rhs) {
      return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                               rhs->catalogIdentity, rhs->name);
    });
    for (const auto *key : keys) {
      output += "  key " + key->name + " {\n";
      AppendCatalogMemberIdentity(output, "    ", key->catalogIdentity);
      output += "    kind " + key->kind + ";\n";
      output += "    fields ";
      AppendIdentifierList(output, key->fields);
      output += ";\n  }\n";
    }

    std::vector<const IndexDeclaration *> indexes;
    for (const auto &index : model->indexes)
      indexes.push_back(&index);
    std::sort(indexes.begin(), indexes.end(),
              [](const auto *lhs, const auto *rhs) {
                return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                                         rhs->catalogIdentity, rhs->name);
              });
    for (const auto *index : indexes) {
      output += "  index " + index->name + " {\n";
      AppendCatalogMemberIdentity(output, "    ", index->catalogIdentity);
      output += "    fields ";
      AppendIdentifierList(output, index->fields);
      output += ";\n";
      output +=
          std::string("    unique ") + (index->unique ? "true;\n" : "false;\n");
      output += "  }\n";
    }

    std::vector<const RelationDeclaration *> relations;
    for (const auto &relation : model->relations)
      relations.push_back(&relation);
    std::sort(relations.begin(), relations.end(),
              [](const auto *lhs, const auto *rhs) {
                return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                                         rhs->catalogIdentity, rhs->name);
              });
    for (const auto *relation : relations) {
      output += "  relation " + relation->name + " {\n";
      AppendCatalogMemberIdentity(output, "    ", relation->catalogIdentity);
      output += "    target " + QuoteString(relation->targetModel) + ";\n";
      output += "    source_fields ";
      AppendIdentifierList(output, relation->sourceFields);
      output += ";\n    target_fields ";
      AppendIdentifierList(output, relation->targetFields);
      output += ";\n";
      output += "    cardinality " + relation->cardinality + ";\n";
      output += "    on_delete " + relation->onDelete + ";\n";
      output += "    on_update " + relation->onUpdate + ";\n";
      output += "  }\n";
    }

    std::vector<const JoinDeclaration *> joins;
    for (const auto &join : model->joins)
      joins.push_back(&join);
    std::sort(joins.begin(), joins.end(), [](const auto *lhs, const auto *rhs) {
      return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                               rhs->catalogIdentity, rhs->name);
    });
    for (const auto *join : joins) {
      output += "  join " + join->name + " {\n";
      AppendCatalogMemberIdentity(output, "    ", join->catalogIdentity);
      output += "    through " + QuoteString(join->throughModel) + ";\n";
      output += "    source_relation " + join->sourceRelation + ";\n";
      output += "    target_relation " + join->targetRelation + ";\n";
      output += "  }\n";
    }

    std::vector<const OneOfDeclaration *> one_ofs;
    for (const auto &one_of : model->oneOfs)
      one_ofs.push_back(&one_of);
    std::sort(one_ofs.begin(), one_ofs.end(),
              [](const auto *lhs, const auto *rhs) {
                return CatalogMemberLess(lhs->catalogIdentity, lhs->name,
                                         rhs->catalogIdentity, rhs->name);
              });
    for (const auto *one_of : one_ofs) {
      output += "  one_of " + one_of->name + " {\n";
      AppendCatalogMemberIdentity(output, "    ", one_of->catalogIdentity);
      output += "    discriminator " + one_of->discriminatorField + ";\n";
      std::vector<const OneOfVariantDeclaration *> variants;
      for (const auto &variant : one_of->variants)
        variants.push_back(&variant);
      std::sort(variants.begin(), variants.end(),
                [](const auto *lhs, const auto *rhs) {
                  return CatalogMemberLess(lhs->catalogIdentity, lhs->tag,
                                           rhs->catalogIdentity, rhs->tag);
                });
      for (const auto *variant : variants) {
        output += "    variant " + variant->tag + " {\n";
        AppendCatalogMemberIdentity(output, "      ", variant->catalogIdentity);
        if (!variant->fields.empty()) {
          output += "      fields ";
          AppendIdentifierList(output, variant->fields);
          output += ";\n";
        }
        if (!variant->optionalFields.empty()) {
          output += "      optional_fields ";
          AppendIdentifierList(output, variant->optionalFields);
          output += ";\n";
        }
        output += "    }\n";
      }
      output += "  }\n";
    }
    std::vector<const TemporalContractDeclaration *> temporal_contracts;
    for (const auto &contract : model->temporalContracts)
      temporal_contracts.push_back(&contract);
    std::sort(
        temporal_contracts.begin(), temporal_contracts.end(),
        [](const auto *lhs, const auto *rhs) { return lhs->name < rhs->name; });
    for (const auto *contract : temporal_contracts) {
      output += "  temporal_contract " + contract->name + " {\n";
      output += "    start_field " + contract->startField + ";\n";
      output += "    end_field " + contract->endField + ";\n";
      output += "    ordering " + contract->ordering + ";\n";
      output += std::string("    start_inclusive ") +
                (contract->startInclusive ? "true;\n" : "false;\n");
      output += std::string("    end_inclusive ") +
                (contract->endInclusive ? "true;\n" : "false;\n");
      output += "    open_end " + contract->openEnd + ";\n";
      output += "  }\n";
    }
    AppendReservations(output, "  ", model->reservedIds, model->reservedNames);
    output += "}\n";
  }

  std::vector<const CommandDeclaration *> commands;
  commands.reserve(document.commands.size());
  for (const auto &command : document.commands)
    commands.push_back(&command);
  std::sort(commands.begin(), commands.end(),
            [](const auto *lhs, const auto *rhs) {
              if (lhs->commandId != rhs->commandId)
                return lhs->commandId < rhs->commandId;
              return lhs->name < rhs->name;
            });
  for (const auto *command : commands) {
    output += "\ncommand " + command->name + " @command_id(" +
              std::to_string(command->commandId) + ") {\n";
    output += "  request " + command->requestModelName + ";\n";
    output += "  response " + command->responseModelName + ";\n";
    output += "  errors ";
    auto errors = command->errorModelNames;
    std::sort(errors.begin(), errors.end());
    for (std::size_t index = 0; index < errors.size(); ++index) {
      if (index != 0)
        output += ", ";
      output += errors[index];
    }
    output += ";\n";
    output += "  idempotency " + command->idempotency + ";\n";
    output += "  streaming " + command->streaming + ";\n";
    output += "  targets ";
    auto targets = command->targets;
    std::sort(targets.begin(), targets.end());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      if (index != 0)
        output += ", ";
      output += targets[index];
    }
    output += ";\n";
    if (command->fabricLane.has_value()) {
      output += "  fabric_lane " + *command->fabricLane + ";\n";
      output += "  maximum_encoded_bytes " +
                std::to_string(*command->maximumEncodedBytes) + ";\n";
      output += std::string("  direct_eligible ") +
                (*command->directEligible ? "true;\n" : "false;\n");
    }
    output += "}\n";
  }

  std::vector<const EventDeclaration *> events;
  events.reserve(document.events.size());
  for (const auto &event : document.events)
    events.push_back(&event);
  std::sort(events.begin(), events.end(), [](const auto *lhs, const auto *rhs) {
    if (lhs->eventId != rhs->eventId)
      return lhs->eventId < rhs->eventId;
    return lhs->name < rhs->name;
  });
  for (const auto *event : events) {
    output += "\nevent " + event->name + " @event_id(" +
              std::to_string(event->eventId) + ") {\n";
    output += "  payload " + event->payloadModelName + ";\n";
    output += "  delivery " + event->delivery + ";\n";
    output += "  ordering " + event->ordering + ";\n";
    output += "  replay " + event->replay + ";\n";
    output += "  targets ";
    auto targets = event->targets;
    std::sort(targets.begin(), targets.end());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      if (index != 0)
        output += ", ";
      output += targets[index];
    }
    output += ";\n";
    if (event->fabricLane.has_value()) {
      output += "  fabric_lane " + *event->fabricLane + ";\n";
      output += "  maximum_encoded_bytes " +
                std::to_string(*event->maximumEncodedBytes) + ";\n";
      output += std::string("  direct_eligible ") +
                (*event->directEligible ? "true;\n" : "false;\n");
    }
    output += "}\n";
  }
  return output;
}
} // namespace arken::schema::idl::v1
