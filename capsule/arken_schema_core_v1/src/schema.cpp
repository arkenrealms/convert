#include "arken_schema_core_v1/schema.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace arken::schema::core::v1
{
namespace
{
using json = nlohmann::json;

template <typename Value, typename Projection>
bool HasDuplicate(const std::vector<Value>& values, Projection projection)
{
    std::set<std::string> seen;
    for (const auto& value : values)
    {
        if (!seen.insert(std::string(projection(value))).second)
        {
            return true;
        }
    }
    return false;
}

void AddError(
    ValidationResult& result,
    std::string code,
    std::string model_id,
    std::string path,
    std::string message
)
{
    result.errors.push_back({
        .code = std::move(code),
        .modelId = std::move(model_id),
        .path = std::move(path),
        .message = std::move(message),
    });
}

void AddWarning(
    ValidationResult& result,
    std::string code,
    std::string model_id,
    std::string path,
    std::string message
)
{
    result.warnings.push_back({
        .code = std::move(code),
        .modelId = std::move(model_id),
        .path = std::move(path),
        .message = std::move(message),
    });
}

json DiagnosticJson(const ValidationDiagnostic& diagnostic)
{
    return {
        {"code", diagnostic.code},
        {"modelId", diagnostic.modelId},
        {"path", diagnostic.path},
        {"message", diagnostic.message},
    };
}

bool IsTextKind(FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::ObjectId:
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::Enum:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
        case FieldKind::KeyReference:
            return true;
        case FieldKind::InternalId:
        case FieldKind::Bool:
        case FieldKind::TimestampMs:
        case FieldKind::JsonObject:
        case FieldKind::Secret:
        case FieldKind::Integer:
        case FieldKind::Real:
        case FieldKind::Blob:
            return false;
    }
    return false;
}

bool IsOrderedKind(FieldKind kind)
{
    return IsTextKind(kind) || kind == FieldKind::InternalId || kind == FieldKind::Integer || kind == FieldKind::Real ||
        kind == FieldKind::TimestampMs;
}

bool IsQueryOperatorCompatible(FieldKind kind, QueryOperator query_operator)
{
    switch (query_operator)
    {
        case QueryOperator::Equal:
        case QueryOperator::NotEqual:
        case QueryOperator::In:
        case QueryOperator::NotIn:
        case QueryOperator::IsNull:
            return kind != FieldKind::Blob && kind != FieldKind::Secret;
        case QueryOperator::LessThan:
        case QueryOperator::LessThanOrEqual:
        case QueryOperator::GreaterThan:
        case QueryOperator::GreaterThanOrEqual:
            return IsOrderedKind(kind);
        case QueryOperator::Prefix:
        case QueryOperator::Suffix:
        case QueryOperator::Contains:
            return IsTextKind(kind);
    }
    return false;
}

bool IsIntegerLiteral(std::string_view value)
{
    if (value.empty())
    {
        return false;
    }
    long long parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size();
}

bool IsLiteralDefaultCompatible(const FieldSchema& field)
{
    if (field.defaultValue.kind != DefaultKind::Literal)
    {
        return true;
    }
    switch (field.kind)
    {
        case FieldKind::Enum:
            return std::find(field.enumValues.begin(), field.enumValues.end(), field.defaultValue.value) != field.enumValues.end();
        case FieldKind::Bool:
            return field.defaultValue.value == "true" || field.defaultValue.value == "false";
        case FieldKind::Integer:
        case FieldKind::InternalId:
        case FieldKind::TimestampMs:
            return IsIntegerLiteral(field.defaultValue.value);
        case FieldKind::Secret:
        case FieldKind::Blob:
            return false;
        case FieldKind::ObjectId:
        case FieldKind::Text:
        case FieldKind::Slug:
        case FieldKind::DisplayName:
        case FieldKind::Url:
        case FieldKind::Email:
        case FieldKind::JsonObject:
        case FieldKind::EthereumAddress:
        case FieldKind::HashHex:
        case FieldKind::TokenHash:
        case FieldKind::KeyReference:
        case FieldKind::Real:
            return true;
    }
    return false;
}

bool IsCheckCompatible(FieldKind kind, CheckKind check_kind)
{
    switch (check_kind)
    {
        case CheckKind::MinimumInteger:
        case CheckKind::MaximumInteger:
            return kind == FieldKind::InternalId || kind == FieldKind::Integer || kind == FieldKind::TimestampMs;
        case CheckKind::MinimumLength:
        case CheckKind::MaximumLength:
        case CheckKind::Pattern:
            return IsTextKind(kind);
        case CheckKind::Custom:
            return true;
    }
    return false;
}

bool FieldsExist(const ModelSchema& model, const std::vector<std::string>& fields)
{
    return std::all_of(fields.begin(), fields.end(), [&model](const std::string& field) {
        return FindField(model, field) != nullptr;
    });
}

bool HasRepeatedField(const std::vector<std::string>& fields)
{
    std::set<std::string> seen;
    for (const auto& field : fields)
    {
        if (!seen.insert(field).second)
        {
            return true;
        }
    }
    return false;
}

bool IsUniqueFieldSet(const ModelSchema& model, const std::vector<std::string>& fields)
{
    if (std::any_of(model.keys.begin(), model.keys.end(), [&fields](const KeySchema& key) {
        return key.fields == fields;
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const IndexSchema& index) {
        return index.unique && index.fields == fields;
    });
}

bool IsPrefix(const std::vector<std::string>& candidate, const std::vector<std::string>& fields)
{
    return candidate.size() >= fields.size() &&
        std::equal(fields.begin(), fields.end(), candidate.begin());
}

void HashByte(std::uint64_t& hash, unsigned char value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
}

void HashText(std::uint64_t& hash, std::string_view value)
{
    for (const unsigned char ch : value)
    {
        HashByte(hash, ch);
    }
    HashByte(hash, 0xff);
}

void HashBool(std::uint64_t& hash, bool value)
{
    HashByte(hash, value ? 1 : 0);
    HashByte(hash, 0xfe);
}

void HashStrings(std::uint64_t& hash, const std::vector<std::string>& values)
{
    HashText(hash, std::to_string(values.size()));
    for (const auto& value : values)
    {
        HashText(hash, value);
    }
}

void HashModel(std::uint64_t& hash, const ModelSchema& model)
{
    HashText(hash, model.modelId);
    HashText(hash, model.logicalName);
    HashText(hash, model.storageName);
    HashText(hash, model.ownerCapsule);
    HashText(hash, model.domain);
    HashText(hash, model.description);
    HashText(hash, ModelKindName(model.kind));
    HashText(hash, RetentionPolicyName(model.retention));
    for (const auto& field : model.fields)
    {
        HashText(hash, field.name);
        HashText(hash, field.storageName);
        HashText(hash, FieldKindName(field.kind));
        HashBool(hash, field.nullable);
        HashBool(hash, field.redacted);
        HashText(hash, field.sourceField);
        HashBool(hash, field.immutable);
        HashBool(hash, field.writeOnce);
        HashText(hash, NormalizationKindName(field.normalization));
        HashText(hash, field.importTransform);
        HashText(hash, DefaultKindName(field.defaultValue.kind));
        HashText(hash, field.defaultValue.value);
        HashStrings(hash, field.enumValues);
        for (const auto& check : field.checks)
        {
            HashText(hash, check.checkId);
            HashText(hash, CheckKindName(check.kind));
            HashText(hash, check.value);
            HashText(hash, check.message);
        }
        for (const auto query_operator : field.query.filterOperators)
        {
            HashText(hash, QueryOperatorName(query_operator));
        }
        HashBool(hash, field.query.sortable);
        HashBool(hash, field.query.cursor);
    }
    for (const auto& key : model.keys)
    {
        HashText(hash, key.keyId);
        HashText(hash, KeyKindName(key.kind));
        HashStrings(hash, key.fields);
    }
    for (const auto& index : model.indexes)
    {
        HashText(hash, index.indexId);
        HashStrings(hash, index.fields);
        HashBool(hash, index.unique);
    }
    for (const auto& relation : model.relations)
    {
        HashText(hash, relation.relationId);
        HashText(hash, relation.targetModel);
        HashStrings(hash, relation.sourceFields);
        HashStrings(hash, relation.targetFields);
        HashText(hash, RelationCardinalityName(relation.cardinality));
        HashText(hash, ReferentialActionName(relation.onDelete));
        HashText(hash, ReferentialActionName(relation.onUpdate));
    }
    for (const auto& join : model.joins)
    {
        HashText(hash, join.joinId);
        HashText(hash, join.throughModel);
        HashText(hash, join.sourceRelation);
        HashText(hash, join.targetRelation);
    }
}

json QueryPolicyJson(const QueryPolicy& query)
{
    json operators = json::array();
    for (const auto query_operator : query.filterOperators)
    {
        operators.push_back(std::string(QueryOperatorName(query_operator)));
    }
    return {
        {"filterOperators", std::move(operators)},
        {"sortable", query.sortable},
        {"cursor", query.cursor},
    };
}

json FieldJson(const FieldSchema& field)
{
    json checks = json::array();
    for (const auto& check : field.checks)
    {
        checks.push_back({
            {"checkId", check.checkId},
            {"kind", std::string(CheckKindName(check.kind))},
            {"value", check.value},
            {"message", check.message},
        });
    }
    return {
        {"name", field.name},
        {"storageName", field.storageName},
        {"kind", std::string(FieldKindName(field.kind))},
        {"nullable", field.nullable},
        {"redacted", field.redacted},
        {"sourceField", field.sourceField.empty() ? field.name : field.sourceField},
        {"immutable", field.immutable},
        {"writeOnce", field.writeOnce},
        {"normalization", std::string(NormalizationKindName(field.normalization))},
        {"importTransform", field.importTransform},
        {"default", {
            {"kind", std::string(DefaultKindName(field.defaultValue.kind))},
            {"value", field.defaultValue.value},
        }},
        {"enumValues", field.enumValues},
        {"checks", std::move(checks)},
        {"query", QueryPolicyJson(field.query)},
    };
}

json ModelJson(const ModelSchema& model)
{
    json fields = json::array();
    json keys = json::array();
    json indexes = json::array();
    json relations = json::array();
    json joins = json::array();
    for (const auto& field : model.fields)
    {
        fields.push_back(FieldJson(field));
    }
    for (const auto& key : model.keys)
    {
        keys.push_back({
            {"keyId", key.keyId},
            {"kind", std::string(KeyKindName(key.kind))},
            {"fields", key.fields},
        });
    }
    for (const auto& index : model.indexes)
    {
        indexes.push_back({
            {"indexId", index.indexId},
            {"fields", index.fields},
            {"unique", index.unique},
        });
    }
    for (const auto& relation : model.relations)
    {
        relations.push_back({
            {"relationId", relation.relationId},
            {"targetModel", relation.targetModel},
            {"sourceFields", relation.sourceFields},
            {"targetFields", relation.targetFields},
            {"cardinality", std::string(RelationCardinalityName(relation.cardinality))},
            {"onDelete", std::string(ReferentialActionName(relation.onDelete))},
            {"onUpdate", std::string(ReferentialActionName(relation.onUpdate))},
        });
    }
    for (const auto& join : model.joins)
    {
        joins.push_back({
            {"joinId", join.joinId},
            {"throughModel", join.throughModel},
            {"sourceRelation", join.sourceRelation},
            {"targetRelation", join.targetRelation},
        });
    }
    return {
        {"modelId", model.modelId},
        {"logicalName", model.logicalName},
        {"storageName", model.storageName},
        {"ownerCapsule", model.ownerCapsule},
        {"domain", model.domain},
        {"description", model.description},
        {"kind", std::string(ModelKindName(model.kind))},
        {"retention", std::string(RetentionPolicyName(model.retention))},
        {"fields", std::move(fields)},
        {"keys", std::move(keys)},
        {"indexes", std::move(indexes)},
        {"relations", std::move(relations)},
        {"joins", std::move(joins)},
    };
}
} // namespace

bool ValidationResult::ok() const
{
    return errors.empty();
}

nlohmann::json ValidationResult::ToJson() const
{
    json error_rows = json::array();
    json warning_rows = json::array();
    for (const auto& error : errors)
    {
        error_rows.push_back(DiagnosticJson(error));
    }
    for (const auto& warning : warnings)
    {
        warning_rows.push_back(DiagnosticJson(warning));
    }
    return {
        {"ok", ok()},
        {"status", ok() ? "ready" : "invalid"},
        {"errorCount", errors.size()},
        {"warningCount", warnings.size()},
        {"errors", std::move(error_rows)},
        {"warnings", std::move(warning_rows)},
    };
}

std::string_view FieldKindName(FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::ObjectId: return "objectId";
        case FieldKind::InternalId: return "internalId";
        case FieldKind::Text: return "text";
        case FieldKind::Slug: return "slug";
        case FieldKind::DisplayName: return "displayName";
        case FieldKind::Url: return "url";
        case FieldKind::Email: return "email";
        case FieldKind::Enum: return "enum";
        case FieldKind::Bool: return "bool";
        case FieldKind::TimestampMs: return "timestampMs";
        case FieldKind::JsonObject: return "jsonObject";
        case FieldKind::Secret: return "secret";
        case FieldKind::EthereumAddress: return "ethereumAddress";
        case FieldKind::HashHex: return "hashHex";
        case FieldKind::TokenHash: return "tokenHash";
        case FieldKind::KeyReference: return "keyReference";
        case FieldKind::Integer: return "integer";
        case FieldKind::Real: return "real";
        case FieldKind::Blob: return "blob";
    }
    return "text";
}

std::string_view NormalizationKindName(NormalizationKind kind)
{
    switch (kind)
    {
        case NormalizationKind::None: return "none";
        case NormalizationKind::Trim: return "trim";
        case NormalizationKind::Lowercase: return "lowercase";
        case NormalizationKind::LowercaseTrim: return "lowercaseTrim";
        case NormalizationKind::EthereumAddress: return "ethereumAddress";
        case NormalizationKind::Url: return "url";
    }
    return "none";
}

std::string_view ModelKindName(ModelKind kind)
{
    switch (kind)
    {
        case ModelKind::Entity: return "entity";
        case ModelKind::Join: return "join";
        case ModelKind::Credential: return "credential";
        case ModelKind::Ephemeral: return "ephemeral";
        case ModelKind::AuditLedger: return "auditLedger";
        case ModelKind::Provenance: return "provenance";
        case ModelKind::ImportLedger: return "importLedger";
    }
    return "entity";
}

std::string_view RetentionPolicyName(RetentionPolicy policy)
{
    switch (policy)
    {
        case RetentionPolicy::Durable: return "durable";
        case RetentionPolicy::UntilExpiry: return "untilExpiry";
        case RetentionPolicy::Immutable: return "immutable";
        case RetentionPolicy::UntilPromotion: return "untilPromotion";
    }
    return "durable";
}

std::string_view DefaultKindName(DefaultKind kind)
{
    switch (kind)
    {
        case DefaultKind::None: return "none";
        case DefaultKind::Literal: return "literal";
        case DefaultKind::BoundaryGenerated: return "boundaryGenerated";
    }
    return "none";
}

std::string_view CheckKindName(CheckKind kind)
{
    switch (kind)
    {
        case CheckKind::MinimumInteger: return "minimumInteger";
        case CheckKind::MaximumInteger: return "maximumInteger";
        case CheckKind::MinimumLength: return "minimumLength";
        case CheckKind::MaximumLength: return "maximumLength";
        case CheckKind::Pattern: return "pattern";
        case CheckKind::Custom: return "custom";
    }
    return "custom";
}

std::string_view KeyKindName(KeyKind kind)
{
    switch (kind)
    {
        case KeyKind::Primary: return "primary";
        case KeyKind::Alternate: return "alternate";
    }
    return "alternate";
}

std::string_view RelationCardinalityName(RelationCardinality cardinality)
{
    switch (cardinality)
    {
        case RelationCardinality::One: return "one";
        case RelationCardinality::OptionalOne: return "optionalOne";
        case RelationCardinality::Many: return "many";
    }
    return "one";
}

std::string_view ReferentialActionName(ReferentialAction action)
{
    switch (action)
    {
        case ReferentialAction::Restrict: return "restrict";
        case ReferentialAction::Cascade: return "cascade";
        case ReferentialAction::SetNull: return "setNull";
    }
    return "restrict";
}

std::string_view QueryOperatorName(QueryOperator query_operator)
{
    switch (query_operator)
    {
        case QueryOperator::Equal: return "equal";
        case QueryOperator::NotEqual: return "notEqual";
        case QueryOperator::In: return "in";
        case QueryOperator::NotIn: return "notIn";
        case QueryOperator::LessThan: return "lessThan";
        case QueryOperator::LessThanOrEqual: return "lessThanOrEqual";
        case QueryOperator::GreaterThan: return "greaterThan";
        case QueryOperator::GreaterThanOrEqual: return "greaterThanOrEqual";
        case QueryOperator::Prefix: return "prefix";
        case QueryOperator::Suffix: return "suffix";
        case QueryOperator::Contains: return "contains";
        case QueryOperator::IsNull: return "isNull";
    }
    return "equal";
}

SchemaCatalog ComposeCatalog(std::string catalog_id, int version, std::vector<SchemaFragment> fragments)
{
    std::sort(fragments.begin(), fragments.end(), [](const SchemaFragment& lhs, const SchemaFragment& rhs) {
        return std::tie(lhs.fragmentId, lhs.ownerCapsule, lhs.version) <
            std::tie(rhs.fragmentId, rhs.ownerCapsule, rhs.version);
    });
    SchemaCatalog catalog{
        .catalogId = std::move(catalog_id),
        .version = version,
        .fragments = std::move(fragments),
    };
    for (auto& fragment : catalog.fragments)
    {
        std::sort(fragment.models.begin(), fragment.models.end(), [](const ModelSchema& lhs, const ModelSchema& rhs) {
            return std::tie(lhs.modelId, lhs.logicalName, lhs.storageName) <
                std::tie(rhs.modelId, rhs.logicalName, rhs.storageName);
        });
        catalog.models.insert(catalog.models.end(), fragment.models.begin(), fragment.models.end());
    }
    return catalog;
}

const ModelSchema* FindModelById(const SchemaCatalog& catalog, std::string_view model_id)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [model_id](const ModelSchema& model) {
        return model.modelId == model_id;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const ModelSchema* FindModelByLogicalName(const SchemaCatalog& catalog, std::string_view logical_name)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [logical_name](const ModelSchema& model) {
        return model.logicalName == logical_name;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const ModelSchema* FindModelByStorageName(const SchemaCatalog& catalog, std::string_view storage_name)
{
    const auto it = std::find_if(catalog.models.begin(), catalog.models.end(), [storage_name](const ModelSchema& model) {
        return model.storageName == storage_name;
    });
    return it == catalog.models.end() ? nullptr : &*it;
}

const FieldSchema* FindField(const ModelSchema& model, std::string_view field_name)
{
    const auto it = std::find_if(model.fields.begin(), model.fields.end(), [field_name](const FieldSchema& field) {
        return field.name == field_name;
    });
    return it == model.fields.end() ? nullptr : &*it;
}

const RelationSchema* FindRelationById(const ModelSchema& model, std::string_view relation_id)
{
    const auto it = std::find_if(model.relations.begin(), model.relations.end(), [relation_id](const RelationSchema& relation) {
        return relation.relationId == relation_id;
    });
    return it == model.relations.end() ? nullptr : &*it;
}

const JoinSchema* FindJoinById(const ModelSchema& model, std::string_view join_id)
{
    const auto it = std::find_if(model.joins.begin(), model.joins.end(), [join_id](const JoinSchema& join) {
        return join.joinId == join_id;
    });
    return it == model.joins.end() ? nullptr : &*it;
}

bool HasIndexPrefix(const ModelSchema& model, const std::vector<std::string>& fields)
{
    if (fields.empty())
    {
        return false;
    }
    if (std::any_of(model.keys.begin(), model.keys.end(), [&fields](const KeySchema& key) {
        return IsPrefix(key.fields, fields);
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [&fields](const IndexSchema& index) {
        return IsPrefix(index.fields, fields);
    });
}

bool IsSingleFieldUnique(const ModelSchema& model, std::string_view field_name)
{
    if (std::any_of(model.keys.begin(), model.keys.end(), [field_name](const KeySchema& key) {
        return key.fields.size() == 1 && key.fields.front() == field_name;
    }))
    {
        return true;
    }
    return std::any_of(model.indexes.begin(), model.indexes.end(), [field_name](const IndexSchema& index) {
        return index.unique && index.fields.size() == 1 && index.fields.front() == field_name;
    });
}

ValidationResult ValidateCatalog(const SchemaCatalog& catalog)
{
    ValidationResult result;
    if (catalog.catalogId.empty())
    {
        AddError(result, "catalog_id_required", {}, "catalogId", "Catalog id is required.");
    }
    if (catalog.version < 1)
    {
        AddError(result, "catalog_version_invalid", {}, "version", "Catalog version must be positive.");
    }
    if (catalog.fragments.empty())
    {
        AddError(result, "catalog_fragments_required", {}, "fragments", "At least one schema fragment is required.");
    }

    std::set<std::string> fragment_ids;
    std::size_t fragment_model_count = 0;
    for (const auto& fragment : catalog.fragments)
    {
        if (fragment.fragmentId.empty() || fragment.ownerCapsule.empty())
        {
            AddError(result, "fragment_identity_required", {}, "fragments", "Fragment id and owner capsule are required.");
        }
        if (!fragment_ids.insert(fragment.fragmentId).second)
        {
            AddError(result, "duplicate_fragment_id", {}, "fragments." + fragment.fragmentId, "Fragment id must be unique.");
        }
        if (fragment.version < 1)
        {
            AddError(result, "fragment_version_invalid", {}, "fragments." + fragment.fragmentId, "Fragment version must be positive.");
        }
        for (const auto& model : fragment.models)
        {
            ++fragment_model_count;
            if (!fragment.ownerCapsule.empty() && model.ownerCapsule != fragment.ownerCapsule)
            {
                AddError(result, "model_owner_mismatch", model.modelId, "ownerCapsule", "Model owner must match its fragment owner.");
            }
            const ModelSchema* composed_model = FindModelById(catalog, model.modelId);
            if (composed_model == nullptr || ModelJson(*composed_model) != ModelJson(model))
            {
                AddError(result, "catalog_fragment_model_mismatch", model.modelId, "models", "Composed models must exactly match their source fragments.");
            }
        }
    }
    if (fragment_model_count != catalog.models.size())
    {
        AddError(result, "catalog_model_count_mismatch", {}, "models", "Composed model count must equal the sum of fragment models.");
    }

    std::set<std::string> model_ids;
    std::set<std::string> logical_names;
    std::set<std::string> storage_names;
    for (const auto& model : catalog.models)
    {
        if (model.modelId.empty() || model.logicalName.empty() || model.storageName.empty() ||
            model.ownerCapsule.empty() || model.domain.empty())
        {
            AddError(result, "model_identity_required", model.modelId, "model", "Model id, names, owner capsule, and domain are required.");
        }
        if (!model_ids.insert(model.modelId).second)
        {
            AddError(result, "duplicate_model_id", model.modelId, "modelId", "Model id must be unique.");
        }
        if (!logical_names.insert(model.logicalName).second)
        {
            AddError(result, "duplicate_logical_name", model.modelId, "logicalName", "Logical model name must be unique.");
        }
        if (!storage_names.insert(model.storageName).second)
        {
            AddError(result, "duplicate_storage_name", model.modelId, "storageName", "Storage name must be unique.");
        }
        if (model.fields.empty())
        {
            AddError(result, "model_fields_required", model.modelId, "fields", "At least one field is required.");
        }
        if (model.kind == ModelKind::Ephemeral && model.retention != RetentionPolicy::UntilExpiry)
        {
            AddError(result, "ephemeral_retention_invalid", model.modelId, "retention", "Ephemeral models must expire explicitly.");
        }
        if (model.kind == ModelKind::AuditLedger && model.retention != RetentionPolicy::Immutable)
        {
            AddError(result, "audit_retention_invalid", model.modelId, "retention", "Audit ledgers must be immutable.");
        }
        if (model.kind == ModelKind::Provenance && model.retention != RetentionPolicy::Immutable)
        {
            AddError(result, "provenance_retention_invalid", model.modelId, "retention", "Provenance rows must be immutable.");
        }
        if (model.kind == ModelKind::ImportLedger &&
            model.retention != RetentionPolicy::Immutable &&
            model.retention != RetentionPolicy::UntilPromotion)
        {
            AddError(result, "import_retention_invalid", model.modelId, "retention", "Import ledgers must be immutable or retained through promotion.");
        }

        std::set<std::string> field_names;
        std::set<std::string> field_storage_names;
        for (const auto& field : model.fields)
        {
            const std::string path = "fields." + field.name;
            if (field.name.empty() || field.storageName.empty())
            {
                AddError(result, "field_identity_required", model.modelId, path, "Field name and storage name are required.");
            }
            if (!field_names.insert(field.name).second)
            {
                AddError(result, "duplicate_field_name", model.modelId, path, "Field name must be unique within a model.");
            }
            if (!field_storage_names.insert(field.storageName).second)
            {
                AddError(result, "duplicate_field_storage_name", model.modelId, path, "Field storage name must be unique within a model.");
            }
            if (field.kind == FieldKind::Enum && field.enumValues.empty())
            {
                AddError(result, "enum_values_required", model.modelId, path, "Enum fields require a model-specific value domain.");
            }
            if (field.kind != FieldKind::Enum && !field.enumValues.empty())
            {
                AddError(result, "enum_values_on_non_enum", model.modelId, path, "Only enum fields may declare enum values.");
            }
            if (HasDuplicate(field.enumValues, [](const std::string& value) -> std::string_view { return value; }))
            {
                AddError(result, "duplicate_enum_value", model.modelId, path, "Enum values must be unique.");
            }
            if (std::any_of(field.enumValues.begin(), field.enumValues.end(), [](const std::string& value) { return value.empty(); }))
            {
                AddError(result, "empty_enum_value", model.modelId, path, "Enum values cannot be empty.");
            }
            if (field.kind == FieldKind::Secret && !field.redacted)
            {
                AddError(result, "secret_field_not_redacted", model.modelId, path, "Secret fields must be redacted.");
            }
            if (field.kind == FieldKind::TokenHash && !field.immutable)
            {
                AddError(result, "token_hash_mutable", model.modelId, path, "Token hashes are immutable; rotation creates a new credential or token row.");
            }
            if (field.writeOnce && field.immutable)
            {
                AddError(result, "write_once_immutable_conflict", model.modelId, path, "A field cannot be both immutable and write-once.");
            }
            if (field.writeOnce && !field.nullable)
            {
                AddError(result, "write_once_field_not_nullable", model.modelId, path, "Write-once fields must begin nullable so one later value can be recorded.");
            }
            if (field.normalization == NormalizationKind::EthereumAddress && field.kind != FieldKind::EthereumAddress)
            {
                AddError(result, "ethereum_normalization_kind_mismatch", model.modelId, path, "Ethereum normalization requires an Ethereum-address field.");
            }
            if (field.normalization == NormalizationKind::Url && field.kind != FieldKind::Url)
            {
                AddError(result, "url_normalization_kind_mismatch", model.modelId, path, "URL normalization requires a URL field.");
            }
            if ((field.normalization == NormalizationKind::Lowercase ||
                 field.normalization == NormalizationKind::LowercaseTrim ||
                 field.normalization == NormalizationKind::Trim) &&
                !IsTextKind(field.kind))
            {
                AddError(result, "text_normalization_kind_mismatch", model.modelId, path, "Text normalization requires a semantic text field.");
            }
            if (field.redacted && (!field.query.filterOperators.empty() || field.query.sortable || field.query.cursor))
            {
                AddError(result, "redacted_field_queryable", model.modelId, path, "Redacted fields cannot expose query capabilities.");
            }
            if (field.defaultValue.kind == DefaultKind::None && !field.defaultValue.value.empty())
            {
                AddError(result, "default_value_without_kind", model.modelId, path, "Default value requires an explicit default kind.");
            }
            if (field.defaultValue.kind == DefaultKind::BoundaryGenerated && field.defaultValue.value.empty())
            {
                AddError(result, "boundary_default_source_required", model.modelId, path, "Boundary-generated defaults require a named source.");
            }
            if (!IsLiteralDefaultCompatible(field))
            {
                AddError(result, "literal_default_kind_mismatch", model.modelId, path, "Literal default must conform to the semantic field kind and enum domain.");
            }
            if (HasDuplicate(field.checks, [](const CheckSchema& check) -> std::string_view { return check.checkId; }))
            {
                AddError(result, "duplicate_check_id", model.modelId, path, "Field check ids must be unique.");
            }
            if (std::any_of(field.checks.begin(), field.checks.end(), [](const CheckSchema& check) { return check.checkId.empty(); }))
            {
                AddError(result, "check_id_required", model.modelId, path, "Every field check requires an id.");
            }
            for (const auto& check : field.checks)
            {
                if (!IsCheckCompatible(field.kind, check.kind))
                {
                    AddError(result, "check_kind_mismatch", model.modelId, path, "Field check is incompatible with the semantic field kind.");
                }
            }
            std::set<QueryOperator> query_operators;
            for (const auto query_operator : field.query.filterOperators)
            {
                if (!query_operators.insert(query_operator).second)
                {
                    AddError(result, "duplicate_query_operator", model.modelId, path, "Query operators must be unique.");
                }
                if (!IsQueryOperatorCompatible(field.kind, query_operator))
                {
                    AddError(result, "query_operator_kind_mismatch", model.modelId, path, "Query operator is incompatible with the semantic field kind.");
                }
                if (query_operator == QueryOperator::IsNull && !field.nullable)
                {
                    AddWarning(result, "is_null_on_required_field", model.modelId, path, "A non-null field does not need an is-null query operator.");
                }
            }
            if (field.query.cursor && !field.query.sortable)
            {
                AddError(result, "cursor_field_not_sortable", model.modelId, path, "Cursor fields must also be sortable.");
            }
            if ((field.query.sortable || field.query.cursor) && !HasIndexPrefix(model, {field.name}))
            {
                AddError(result, "ordered_query_field_not_indexed", model.modelId, path, "Sortable and cursor fields require an index or key prefix.");
            }
        }

        std::set<std::string> key_ids;
        int primary_key_count = 0;
        for (const auto& key : model.keys)
        {
            const std::string path = "keys." + key.keyId;
            if (key.keyId.empty() || key.fields.empty())
            {
                AddError(result, "key_identity_required", model.modelId, path, "Key id and fields are required.");
            }
            if (!key_ids.insert(key.keyId).second)
            {
                AddError(result, "duplicate_key_id", model.modelId, path, "Key id must be unique within a model.");
            }
            if (key.kind == KeyKind::Primary)
            {
                ++primary_key_count;
                for (const auto& field_name : key.fields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        AddError(result, "nullable_primary_key_field", model.modelId, path, "Primary key fields cannot be nullable.");
                    }
                }
            }
            if (HasRepeatedField(key.fields) || !FieldsExist(model, key.fields))
            {
                AddError(result, "key_field_invalid", model.modelId, path, "Every key field must exist exactly once.");
            }
        }
        if (primary_key_count != 1)
        {
            AddError(result, "primary_key_count_invalid", model.modelId, "keys", "Every model requires exactly one primary key.");
        }

        std::set<std::string> index_ids;
        for (const auto& index : model.indexes)
        {
            const std::string path = "indexes." + index.indexId;
            if (index.indexId.empty() || index.fields.empty())
            {
                AddError(result, "index_identity_required", model.modelId, path, "Index id and fields are required.");
            }
            if (!index_ids.insert(index.indexId).second)
            {
                AddError(result, "duplicate_index_id", model.modelId, path, "Index id must be unique within a model.");
            }
            if (HasRepeatedField(index.fields) || !FieldsExist(model, index.fields))
            {
                AddError(result, "index_field_invalid", model.modelId, path, "Every index field must exist exactly once.");
            }
        }

        std::set<std::string> relation_ids;
        for (const auto& relation : model.relations)
        {
            const std::string path = "relations." + relation.relationId;
            if (model.retention == RetentionPolicy::Immutable &&
                (relation.onDelete != ReferentialAction::Restrict ||
                 relation.onUpdate != ReferentialAction::Restrict))
            {
                AddError(
                    result,
                    "immutable_relation_action_invalid",
                    model.modelId,
                    path,
                    "Immutable rows cannot be changed or removed by referential actions."
                );
            }
            if (relation.relationId.empty() || relation.targetModel.empty() || relation.sourceFields.empty() ||
                relation.targetFields.empty())
            {
                AddError(result, "relation_identity_required", model.modelId, path, "Relation id, target, and field mappings are required.");
            }
            if (!relation_ids.insert(relation.relationId).second)
            {
                AddError(result, "duplicate_relation_id", model.modelId, path, "Relation id must be unique within a model.");
            }
            if (relation.sourceFields.size() != relation.targetFields.size())
            {
                AddError(result, "relation_field_arity_mismatch", model.modelId, path, "Relation source and target field counts must match.");
            }
            if (HasRepeatedField(relation.sourceFields) || !FieldsExist(model, relation.sourceFields))
            {
                AddError(result, "relation_source_field_invalid", model.modelId, path, "Relation source fields must exist exactly once.");
            }
            const ModelSchema* target = FindModelById(catalog, relation.targetModel);
            if (target == nullptr)
            {
                AddError(result, "relation_target_missing", model.modelId, path, "Relation target model must exist.");
            }
            else if (HasRepeatedField(relation.targetFields) || !FieldsExist(*target, relation.targetFields))
            {
                AddError(result, "relation_target_field_invalid", model.modelId, path, "Relation target fields must exist exactly once.");
            }
            else if (!HasIndexPrefix(*target, relation.targetFields))
            {
                AddError(result, "relation_target_not_keyed", model.modelId, path, "Relation target fields require a key or index prefix.");
            }
            else if (relation.cardinality != RelationCardinality::Many &&
                     !IsUniqueFieldSet(*target, relation.targetFields))
            {
                AddError(result, "singular_relation_target_not_unique", model.modelId, path, "Singular relation target fields must identify a unique row.");
            }
            if (!HasIndexPrefix(model, relation.sourceFields))
            {
                AddError(result, "relation_source_not_indexed", model.modelId, path, "Relation source fields require an index prefix.");
            }
            if (relation.cardinality == RelationCardinality::One)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        AddError(result, "required_relation_nullable", model.modelId, path, "Required one-to-one relation fields cannot be nullable.");
                    }
                }
            }
            if (relation.onDelete == ReferentialAction::SetNull ||
                relation.onUpdate != ReferentialAction::Restrict)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name);
                        field != nullptr && (field->immutable || field->writeOnce))
                    {
                        AddError(
                            result,
                            "immutable_relation_field_action_invalid",
                            model.modelId,
                            path,
                            "Referential actions cannot rewrite immutable relation fields."
                        );
                    }
                }
            }
            if (relation.cardinality == RelationCardinality::OptionalOne)
            {
                bool has_nullable_source = false;
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && field->nullable)
                    {
                        has_nullable_source = true;
                    }
                }
                if (!has_nullable_source)
                {
                    AddError(result, "nullable_relation_field_required", model.modelId, path, "Optional relations require at least one nullable source field.");
                }
            }
            if (relation.onDelete == ReferentialAction::SetNull || relation.onUpdate == ReferentialAction::SetNull)
            {
                for (const auto& field_name : relation.sourceFields)
                {
                    if (const auto* field = FindField(model, field_name); field != nullptr && !field->nullable)
                    {
                        AddError(result, "set_null_relation_field_required", model.modelId, path, "Set-null actions require every source field to be nullable.");
                    }
                }
            }
        }

        std::set<std::string> join_ids;
        for (const auto& join : model.joins)
        {
            const std::string path = "joins." + join.joinId;
            if (join.joinId.empty() || join.throughModel.empty() || join.sourceRelation.empty() || join.targetRelation.empty())
            {
                AddError(result, "join_identity_required", model.modelId, path, "Join id, through model, and relation ids are required.");
            }
            if (!join_ids.insert(join.joinId).second)
            {
                AddError(result, "duplicate_join_id", model.modelId, path, "Join id must be unique within a model.");
            }
            if (relation_ids.contains(join.joinId))
            {
                AddError(result, "relation_join_id_collision", model.modelId, path, "Relation and join ids share one include namespace and must be unique.");
            }
            if (join.sourceRelation == join.targetRelation)
            {
                AddError(result, "join_relations_must_differ", model.modelId, path, "Join source and target relations must differ.");
            }
            const ModelSchema* through = FindModelById(catalog, join.throughModel);
            if (through == nullptr)
            {
                AddError(result, "join_through_model_missing", model.modelId, path, "Join through model must exist.");
                continue;
            }
            const RelationSchema* source_relation = FindRelationById(*through, join.sourceRelation);
            const RelationSchema* target_relation = FindRelationById(*through, join.targetRelation);
            if (source_relation == nullptr || target_relation == nullptr)
            {
                AddError(result, "join_relation_missing", model.modelId, path, "Join relations must exist on the through model.");
            }
            else if (source_relation->targetModel != model.modelId)
            {
                AddError(result, "join_source_target_mismatch", model.modelId, path, "Join source relation must point back to the declaring model.");
            }
        }
    }
    return result;
}

std::string FingerprintCatalog(const SchemaCatalog& catalog)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    HashText(hash, catalog.catalogId);
    HashText(hash, std::to_string(catalog.version));
    for (const auto& fragment : catalog.fragments)
    {
        HashText(hash, fragment.fragmentId);
        HashText(hash, fragment.ownerCapsule);
        HashText(hash, std::to_string(fragment.version));
        for (const auto& model : fragment.models)
        {
            HashText(hash, model.modelId);
        }
    }
    for (const auto& model : catalog.models)
    {
        HashModel(hash, model);
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

nlohmann::json BuildSchemaManifest(const SchemaCatalog& catalog)
{
    json fragments = json::array();
    json models = json::array();
    for (const auto& fragment : catalog.fragments)
    {
        json model_ids = json::array();
        for (const auto& model : fragment.models)
        {
            model_ids.push_back(model.modelId);
        }
        fragments.push_back({
            {"fragmentId", fragment.fragmentId},
            {"ownerCapsule", fragment.ownerCapsule},
            {"version", fragment.version},
            {"modelIds", std::move(model_ids)},
        });
    }
    for (const auto& model : catalog.models)
    {
        models.push_back(ModelJson(model));
    }
    const ValidationResult validation = ValidateCatalog(catalog);
    return {
        {"mode", "arken-schema-catalog"},
        {"catalogId", catalog.catalogId},
        {"version", catalog.version},
        {"fingerprint", FingerprintCatalog(catalog)},
        {"fragmentCount", catalog.fragments.size()},
        {"modelCount", catalog.models.size()},
        {"fragments", std::move(fragments)},
        {"models", std::move(models)},
        {"validation", validation.ToJson()},
    };
}
} // namespace arken::schema::core::v1
