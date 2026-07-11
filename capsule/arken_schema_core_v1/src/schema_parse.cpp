#include "arken_schema_core_v1/schema.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arken::schema::core::v1 {
namespace {
using json = nlohmann::json;

void AddError(FragmentManifestParseResult &result, std::string path,
              std::string message) {
  result.errors.push_back(
      {.path = std::move(path), .message = std::move(message)});
}

template <typename T>
std::optional<T> Read(const json &value, std::string_view key, std::string path,
                      FragmentManifestParseResult &result) {
  if (!value.is_object() || !value.contains(key) || value.at(key).is_null()) {
    AddError(result, path + "." + std::string(key),
             "Required value is missing.");
    return std::nullopt;
  }
  try {
    return value.at(key).get<T>();
  } catch (const json::exception &error) {
    AddError(result, path + "." + std::string(key), error.what());
    return std::nullopt;
  }
}

template <typename Enum, std::size_t Size>
std::optional<Enum>
ParseEnum(const json &value, std::string_view key, std::string path,
          const std::array<std::pair<std::string_view, Enum>, Size> &values,
          FragmentManifestParseResult &result) {
  const auto text = Read<std::string>(value, key, path, result);
  if (!text.has_value()) {
    return std::nullopt;
  }
  for (const auto &[name, parsed] : values) {
    if (*text == name) {
      return parsed;
    }
  }
  AddError(result, path + "." + std::string(key),
           "Unknown enum value: " + *text);
  return std::nullopt;
}

const auto kFieldKinds = std::to_array<std::pair<std::string_view, FieldKind>>({
    {"objectId", FieldKind::ObjectId},
    {"internalId", FieldKind::InternalId},
    {"text", FieldKind::Text},
    {"slug", FieldKind::Slug},
    {"displayName", FieldKind::DisplayName},
    {"url", FieldKind::Url},
    {"email", FieldKind::Email},
    {"enum", FieldKind::Enum},
    {"bool", FieldKind::Bool},
    {"timestampMs", FieldKind::TimestampMs},
    {"jsonText", FieldKind::JsonText},
    {"secret", FieldKind::Secret},
    {"ethereumAddress", FieldKind::EthereumAddress},
    {"hashHex", FieldKind::HashHex},
    {"tokenHash", FieldKind::TokenHash},
    {"keyReference", FieldKind::KeyReference},
    {"integer", FieldKind::Integer},
    {"real", FieldKind::Real},
    {"blob", FieldKind::Blob},
});

const auto kWireTypes = std::to_array<std::pair<std::string_view, WireType>>({
    {"none", WireType::None},
    {"bool", WireType::Bool},
    {"u8", WireType::U8},
    {"u16", WireType::U16},
    {"u32", WireType::U32},
    {"u64", WireType::U64},
    {"i8", WireType::I8},
    {"i16", WireType::I16},
    {"i32", WireType::I32},
    {"i64", WireType::I64},
    {"f32", WireType::F32},
    {"f64", WireType::F64},
    {"utf8", WireType::Utf8},
    {"bytes", WireType::Bytes},
    {"fixedBytes", WireType::FixedBytes},
});

const auto kStorageCodecs =
    std::to_array<std::pair<std::string_view, StorageCodec>>({
        {"none", StorageCodec::None},
        {"sqliteInteger", StorageCodec::SqliteInteger},
        {"sqliteReal", StorageCodec::SqliteReal},
        {"sqliteText", StorageCodec::SqliteText},
        {"sqliteBlob", StorageCodec::SqliteBlob},
        {"sqliteFixedBlob", StorageCodec::SqliteFixedBlob},
    });

const auto kNormalizations =
    std::to_array<std::pair<std::string_view, NormalizationKind>>({
        {"none", NormalizationKind::None},
        {"trim", NormalizationKind::Trim},
        {"lowercase", NormalizationKind::Lowercase},
        {"lowercaseTrim", NormalizationKind::LowercaseTrim},
        {"ethereumAddress", NormalizationKind::EthereumAddress},
        {"url", NormalizationKind::Url},
    });

const auto kModelKinds = std::to_array<std::pair<std::string_view, ModelKind>>({
    {"entity", ModelKind::Entity},
    {"join", ModelKind::Join},
    {"credential", ModelKind::Credential},
    {"ephemeral", ModelKind::Ephemeral},
    {"auditLedger", ModelKind::AuditLedger},
    {"provenance", ModelKind::Provenance},
    {"importLedger", ModelKind::ImportLedger},
});

const auto kModelRoles = std::to_array<std::pair<std::string_view, ModelRole>>({
    {"unspecified", ModelRole::Unspecified},
    {"universalSpine", ModelRole::UniversalSpine},
    {"domainFacet", ModelRole::DomainFacet},
    {"definition", ModelRole::Definition},
    {"revision", ModelRole::Revision},
    {"instance", ModelRole::Instance},
    {"occurrence", ModelRole::Occurrence},
    {"manifestation", ModelRole::Manifestation},
    {"representation", ModelRole::Representation},
    {"assertion", ModelRole::Assertion},
    {"observation", ModelRole::Observation},
    {"authoritativeAggregate", ModelRole::AuthoritativeAggregate},
    {"ledger", ModelRole::Ledger},
    {"registry", ModelRole::Registry},
    {"projection", ModelRole::Projection},
    {"importRecord", ModelRole::ImportRecord},
    {"receipt", ModelRole::Receipt},
    {"membership", ModelRole::Membership},
    {"valueObject", ModelRole::ValueObject},
});

const auto kNameReviewStatuses =
    std::to_array<std::pair<std::string_view, NameReviewStatus>>({
        {"unspecified", NameReviewStatus::Unspecified},
        {"canonical", NameReviewStatus::Canonical},
        {"reviewRequired", NameReviewStatus::ReviewRequired},
        {"approvedException", NameReviewStatus::ApprovedException},
    });

const auto kRetentions =
    std::to_array<std::pair<std::string_view, RetentionPolicy>>({
        {"durable", RetentionPolicy::Durable},
        {"untilExpiry", RetentionPolicy::UntilExpiry},
        {"immutable", RetentionPolicy::Immutable},
        {"untilPromotion", RetentionPolicy::UntilPromotion},
    });

const auto kDefaultKinds =
    std::to_array<std::pair<std::string_view, DefaultKind>>({
        {"none", DefaultKind::None},
        {"literal", DefaultKind::Literal},
        {"boundaryGenerated", DefaultKind::BoundaryGenerated},
    });

const auto kCheckKinds = std::to_array<std::pair<std::string_view, CheckKind>>({
    {"minimumInteger", CheckKind::MinimumInteger},
    {"maximumInteger", CheckKind::MaximumInteger},
    {"minimumReal", CheckKind::MinimumReal},
    {"maximumReal", CheckKind::MaximumReal},
    {"minimumLength", CheckKind::MinimumLength},
    {"maximumLength", CheckKind::MaximumLength},
    {"pattern", CheckKind::Pattern},
    {"custom", CheckKind::Custom},
});

const auto kKeyKinds = std::to_array<std::pair<std::string_view, KeyKind>>({
    {"primary", KeyKind::Primary},
    {"alternate", KeyKind::Alternate},
});

const auto kCardinalities =
    std::to_array<std::pair<std::string_view, RelationCardinality>>({
        {"one", RelationCardinality::One},
        {"optionalOne", RelationCardinality::OptionalOne},
        {"many", RelationCardinality::Many},
    });

const auto kActions =
    std::to_array<std::pair<std::string_view, ReferentialAction>>({
        {"restrict", ReferentialAction::Restrict},
        {"cascade", ReferentialAction::Cascade},
        {"setNull", ReferentialAction::SetNull},
    });

const auto kQueryOperators =
    std::to_array<std::pair<std::string_view, QueryOperator>>({
        {"equal", QueryOperator::Equal},
        {"notEqual", QueryOperator::NotEqual},
        {"in", QueryOperator::In},
        {"notIn", QueryOperator::NotIn},
        {"lessThan", QueryOperator::LessThan},
        {"lessThanOrEqual", QueryOperator::LessThanOrEqual},
        {"greaterThan", QueryOperator::GreaterThan},
        {"greaterThanOrEqual", QueryOperator::GreaterThanOrEqual},
        {"prefix", QueryOperator::Prefix},
        {"suffix", QueryOperator::Suffix},
        {"contains", QueryOperator::Contains},
        {"isNull", QueryOperator::IsNull},
    });

std::optional<FieldSchema> ParseField(const json &value,
                                      const std::string &path,
                                      FragmentManifestParseResult &result) {
  FieldSchema field;
  const auto name = Read<std::string>(value, "name", path, result);
  const auto storage_name =
      Read<std::string>(value, "storageName", path, result);
  const auto kind = ParseEnum(value, "kind", path, kFieldKinds, result);
  const auto semantic_type = value.value("semanticType", std::string{});
  const auto wire_type = ParseEnum(value, "wireType", path, kWireTypes, result);
  const auto wire_size = Read<int>(value, "wireSize", path, result);
  StorageCodec storage_codec = StorageCodec::None;
  if (value.contains("storageCodec")) {
    const auto parsed =
        ParseEnum(value, "storageCodec", path, kStorageCodecs, result);
    if (!parsed) return std::nullopt;
    storage_codec = *parsed;
  }
  const int storage_size = value.value("storageSize", 0);
  const auto nullable = Read<bool>(value, "nullable", path, result);
  const auto redacted = Read<bool>(value, "redacted", path, result);
  const auto source_field =
      Read<std::string>(value, "sourceField", path, result);
  const auto immutable = Read<bool>(value, "immutable", path, result);
  const auto write_once = Read<bool>(value, "writeOnce", path, result);
  const auto normalization =
      ParseEnum(value, "normalization", path, kNormalizations, result);
  const auto import_transform =
      Read<std::string>(value, "importTransform", path, result);
  if (!name || !storage_name || !kind || !wire_type || !wire_size || !nullable || !redacted ||
      !source_field || !immutable || !write_once || !normalization ||
      !import_transform) {
    return std::nullopt;
  }
  field.registryKey = value.value("registryKey", std::string{});
  field.name = *name;
  field.fieldId = value.value("fieldId", std::uint32_t{0});
  field.storageName = *storage_name;
  field.kind = *kind;
  field.semanticType = semantic_type;
  field.wireType = *wire_type;
  field.wireSize = *wire_size;
  field.storageCodec = storage_codec;
  field.storageSize = storage_size;
  field.nullable = *nullable;
  field.redacted = *redacted;
  field.sourceField = *source_field;
  field.immutable = *immutable;
  field.writeOnce = *write_once;
  field.normalization = *normalization;
  field.importTransform = *import_transform;
  const auto &default_value = value.value("default", json::object());
  const auto default_kind = ParseEnum(default_value, "kind", path + ".default",
                                      kDefaultKinds, result);
  const auto default_text =
      Read<std::string>(default_value, "value", path + ".default", result);
  if (!default_kind || !default_text) {
    return std::nullopt;
  }
  field.defaultValue = {.kind = *default_kind, .value = *default_text};
  field.enumValues = value.value("enumValues", std::vector<std::string>{});

  const auto &checks = value.value("checks", json::array());
  if (!checks.is_array()) {
    AddError(result, path + ".checks", "Checks must be an array.");
    return std::nullopt;
  }
  for (std::size_t index = 0; index < checks.size(); ++index) {
    const auto check_path = path + ".checks[" + std::to_string(index) + "]";
    const auto check_id =
        Read<std::string>(checks[index], "checkId", check_path, result);
    const auto check_kind =
        ParseEnum(checks[index], "kind", check_path, kCheckKinds, result);
    const auto check_value =
        Read<std::string>(checks[index], "value", check_path, result);
    const auto message =
        Read<std::string>(checks[index], "message", check_path, result);
    if (check_id && check_kind && check_value && message) {
      field.checks.push_back({
          .checkId = *check_id,
          .kind = *check_kind,
          .value = *check_value,
          .message = *message,
      });
    }
  }

  const auto &query = value.value("query", json::object());
  field.query.sortable = query.value("sortable", false);
  field.query.cursor = query.value("cursor", false);
  const auto &operators = query.value("filterOperators", json::array());
  if (!operators.is_array()) {
    AddError(result, path + ".query.filterOperators",
             "Query operators must be an array.");
    return std::nullopt;
  }
  for (std::size_t index = 0; index < operators.size(); ++index) {
    const json row{{"operator", operators[index]}};
    const auto parsed = ParseEnum(row, "operator",
                                  path + ".query.filterOperators[" +
                                      std::to_string(index) + "]",
                                  kQueryOperators, result);
    if (parsed) {
      field.query.filterOperators.push_back(*parsed);
    }
  }
  return field;
}

std::optional<ModelSchema> ParseModel(const json &value,
                                      const std::string &path,
                                      FragmentManifestParseResult &result) {
  ModelSchema model;
  const auto model_id = Read<std::string>(value, "modelId", path, result);
  const auto logical_name =
      Read<std::string>(value, "logicalName", path, result);
  const auto storage_name =
      Read<std::string>(value, "storageName", path, result);
  const auto owner = Read<std::string>(value, "ownerCapsule", path, result);
  const auto domain = Read<std::string>(value, "domain", path, result);
  const auto description =
      Read<std::string>(value, "description", path, result);
  const auto kind = ParseEnum(value, "kind", path, kModelKinds, result);
  const auto retention =
      ParseEnum(value, "retention", path, kRetentions, result);
  if (!model_id || !logical_name || !storage_name || !owner || !domain ||
      !description || !kind || !retention) {
    return std::nullopt;
  }
  model.registryKey = value.value("registryKey", std::string{});
  model.modelId = *model_id;
  model.logicalName = *logical_name;
  model.storageName = *storage_name;
  model.ownerCapsule = *owner;
  model.domain = *domain;
  model.description = *description;
  model.kind = *kind;
  model.retention = *retention;
  model.typeId = value.value("typeId", std::uint32_t{0});
  model.qualifiedName = value.value("qualifiedName", std::string{});
  model.mutationAuthority = value.value("mutationAuthority", std::string{});
  if (value.contains("role")) {
    const auto role = ParseEnum(value, "role", path, kModelRoles, result);
    if (!role) return std::nullopt;
    model.role = *role;
  }
  if (value.contains("nameReview")) {
    const auto name_review =
        ParseEnum(value, "nameReview", path, kNameReviewStatuses, result);
    if (!name_review) return std::nullopt;
    model.nameReview = *name_review;
  }
  model.sourceAliases =
      value.value("sourceAliases", std::vector<std::string>{});
  model.retiredNames =
      value.value("retiredNames", std::vector<std::string>{});
  model.nameReviewRationale =
      value.value("nameReviewRationale", std::string{});
  model.retiredFieldIds =
      value.value("retiredFieldIds", std::vector<std::uint32_t>{});
  model.retiredFieldNames =
      value.value("retiredFieldNames", std::vector<std::string>{});

  const auto &fields = value.value("fields", json::array());
  if (!fields.is_array()) {
    AddError(result, path + ".fields", "Fields must be an array.");
    return std::nullopt;
  }
  for (std::size_t index = 0; index < fields.size(); ++index) {
    auto field = ParseField(
        fields[index], path + ".fields[" + std::to_string(index) + "]", result);
    if (field) {
      model.fields.push_back(std::move(*field));
    }
  }

  const auto &keys = value.value("keys", json::array());
  const auto &indexes = value.value("indexes", json::array());
  const auto &relations = value.value("relations", json::array());
  const auto &joins = value.value("joins", json::array());
  const auto &one_ofs = value.value("oneOfs", json::array());
  if (!keys.is_array() || !indexes.is_array() || !relations.is_array() ||
      !joins.is_array() || !one_ofs.is_array()) {
    AddError(result, path,
             "Keys, indexes, relations, joins, and one-of contracts must be arrays.");
    return std::nullopt;
  }
  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto row_path = path + ".keys[" + std::to_string(index) + "]";
    const auto id = Read<std::string>(keys[index], "keyId", row_path, result);
    const auto key_kind =
        ParseEnum(keys[index], "kind", row_path, kKeyKinds, result);
    const auto fields_value =
        Read<std::vector<std::string>>(keys[index], "fields", row_path, result);
    if (id && key_kind && fields_value) {
      model.keys.push_back(
          {.keyId = *id, .kind = *key_kind, .fields = *fields_value});
    }
  }
  for (std::size_t index = 0; index < indexes.size(); ++index) {
    const auto row_path = path + ".indexes[" + std::to_string(index) + "]";
    const auto id =
        Read<std::string>(indexes[index], "indexId", row_path, result);
    const auto fields_value = Read<std::vector<std::string>>(
        indexes[index], "fields", row_path, result);
    const auto unique = Read<bool>(indexes[index], "unique", row_path, result);
    if (id && fields_value && unique) {
      model.indexes.push_back(
          {.indexId = *id, .fields = *fields_value, .unique = *unique});
    }
  }
  for (std::size_t index = 0; index < relations.size(); ++index) {
    const auto row_path = path + ".relations[" + std::to_string(index) + "]";
    const auto id =
        Read<std::string>(relations[index], "relationId", row_path, result);
    const auto target =
        Read<std::string>(relations[index], "targetModel", row_path, result);
    const auto source_fields = Read<std::vector<std::string>>(
        relations[index], "sourceFields", row_path, result);
    const auto target_fields = Read<std::vector<std::string>>(
        relations[index], "targetFields", row_path, result);
    const auto cardinality = ParseEnum(relations[index], "cardinality",
                                       row_path, kCardinalities, result);
    const auto on_delete =
        ParseEnum(relations[index], "onDelete", row_path, kActions, result);
    const auto on_update =
        ParseEnum(relations[index], "onUpdate", row_path, kActions, result);
    if (id && target && source_fields && target_fields && cardinality &&
        on_delete && on_update) {
      model.relations.push_back({
          .relationId = *id,
          .targetModel = *target,
          .sourceFields = *source_fields,
          .targetFields = *target_fields,
          .cardinality = *cardinality,
          .onDelete = *on_delete,
          .onUpdate = *on_update,
      });
    }
  }
  for (std::size_t index = 0; index < joins.size(); ++index) {
    const auto row_path = path + ".joins[" + std::to_string(index) + "]";
    const auto id = Read<std::string>(joins[index], "joinId", row_path, result);
    const auto through =
        Read<std::string>(joins[index], "throughModel", row_path, result);
    const auto source =
        Read<std::string>(joins[index], "sourceRelation", row_path, result);
    const auto target =
        Read<std::string>(joins[index], "targetRelation", row_path, result);
    if (id && through && source && target) {
      model.joins.push_back({
          .joinId = *id,
          .throughModel = *through,
          .sourceRelation = *source,
          .targetRelation = *target,
      });
    }
  }
  for (std::size_t index = 0; index < one_ofs.size(); ++index) {
    const auto row_path = path + ".oneOfs[" + std::to_string(index) + "]";
    const auto id =
        Read<std::string>(one_ofs[index], "oneOfId", row_path, result);
    const auto discriminator = Read<std::string>(
        one_ofs[index], "discriminatorField", row_path, result);
    const auto &variants = one_ofs[index].value("variants", json::array());
    if (!id || !discriminator || !variants.is_array()) {
      AddError(result, row_path, "One-of variants must be an array.");
      continue;
    }
    OneOfSchema one_of{
        .oneOfId = *id,
        .discriminatorField = *discriminator,
    };
    for (std::size_t variant_index = 0; variant_index < variants.size();
         ++variant_index) {
      const auto variant_path =
          row_path + ".variants[" + std::to_string(variant_index) + "]";
      const auto tag =
          Read<std::string>(variants[variant_index], "tag", variant_path, result);
      const auto fields_value = Read<std::vector<std::string>>(
          variants[variant_index], "fields", variant_path, result);
      const auto optional_fields = Read<std::vector<std::string>>(
          variants[variant_index], "optionalFields", variant_path, result);
      if (tag && fields_value && optional_fields) {
        one_of.variants.push_back({
            .tag = *tag,
            .fields = *fields_value,
            .optionalFields = *optional_fields,
        });
      }
    }
    model.oneOfs.push_back(std::move(one_of));
  }
  return model;
}
} // namespace

bool FragmentManifestParseResult::ok() const { return errors.empty(); }

FragmentManifestParseResult
ParseSchemaFragmentManifest(const nlohmann::json &manifest) {
  FragmentManifestParseResult result;
  try {
    if (!manifest.is_object() || !manifest.contains("fragments") ||
        !manifest.at("fragments").is_array()) {
      AddError(result, "fragments", "Manifest must contain a fragments array.");
      return result;
    }
    const auto &fragments = manifest.at("fragments");
    for (std::size_t fragment_index = 0; fragment_index < fragments.size();
         ++fragment_index) {
      const auto path = "fragments[" + std::to_string(fragment_index) + "]";
      const auto fragment_id = Read<std::string>(fragments[fragment_index],
                                                 "fragmentId", path, result);
      const auto owner = Read<std::string>(fragments[fragment_index],
                                           "ownerCapsule", path, result);
      const auto version =
          Read<int>(fragments[fragment_index], "version", path, result);
      const auto &models =
          fragments[fragment_index].value("models", json::array());
      if (!fragment_id || !owner || !version || !models.is_array()) {
        if (!models.is_array()) {
          AddError(result, path + ".models", "Models must be an array.");
        }
        continue;
      }
      SchemaFragment fragment{
          .fragmentId = *fragment_id,
          .ownerCapsule = *owner,
          .version = *version,
      };
      for (std::size_t model_index = 0; model_index < models.size();
           ++model_index) {
        auto model = ParseModel(
            models[model_index],
            path + ".models[" + std::to_string(model_index) + "]", result);
        if (model) {
          fragment.models.push_back(std::move(*model));
        }
      }
      result.fragments.push_back(std::move(fragment));
    }
  } catch (const json::exception &error) {
    AddError(result, "manifest", error.what());
  }
  return result;
}
} // namespace arken::schema::core::v1
