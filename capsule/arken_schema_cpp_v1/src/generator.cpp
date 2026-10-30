#include "arken_schema_cpp_v1/generator.hpp"

#include "arken_schema_idl_v1/allocation.hpp"
#include "generator_registry.hpp"
#include "generator_wire_names.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace arken::schema::cpp::v1 {
namespace idl = ::arken::schema::idl::v1;

namespace {
using generator_detail::GeneratedCommandIdempotencyName;
using generator_detail::GeneratedCommandStreamingName;
using generator_detail::GeneratedEventDeliveryName;
using generator_detail::GeneratedEventOrderingName;
using generator_detail::GeneratedEventReplayName;
using generator_detail::GeneratedFabricLaneName;
using generator_detail::GeneratedFieldCardinalityName;
using generator_detail::GeneratedWireByteOrderName;
using generator_detail::GeneratedWireCompatibilityClassName;

struct SourceGenerationInfo {
  const idl::SemanticSource *source = nullptr;
  std::string relativePath;
  std::string namespaceName;
};

bool HasErrors(const std::vector<idl::Diagnostic> &diagnostics) {
  return std::any_of(
      diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.severity == idl::DiagnosticSeverity::Error;
      });
}

void AddDiagnostic(CppGenerationResult &result, std::string code,
                   std::string message,
                   const idl::SemanticProvenance &provenance = {}) {
  result.diagnostics.push_back({
      .code = std::move(code),
      .severity = idl::DiagnosticSeverity::Error,
      .message = std::move(message),
      .sourceName = provenance.sourcePath,
      .span = provenance.span,
  });
}

bool IsSnakeIdentifier(std::string_view value) {
  if (value.empty() || value.front() < 'a' || value.front() > 'z')
    return false;
  for (const char character : value) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9') || character == '_') {
      continue;
    }
    return false;
  }
  return value.back() != '_';
}

bool IsCppKeyword(std::string_view value) {
  static constexpr auto kKeywords = std::to_array<std::string_view>({
      "alignas",
      "alignof",
      "and",
      "and_eq",
      "asm",
      "atomic_cancel",
      "atomic_commit",
      "atomic_noexcept",
      "auto",
      "bitand",
      "bitor",
      "bool",
      "break",
      "case",
      "catch",
      "char",
      "char8_t",
      "char16_t",
      "char32_t",
      "class",
      "compl",
      "concept",
      "const",
      "consteval",
      "constexpr",
      "constinit",
      "const_cast",
      "continue",
      "co_await",
      "co_return",
      "co_yield",
      "decltype",
      "default",
      "delete",
      "do",
      "double",
      "dynamic_cast",
      "else",
      "enum",
      "explicit",
      "export",
      "extern",
      "false",
      "float",
      "for",
      "friend",
      "goto",
      "if",
      "inline",
      "int",
      "long",
      "mutable",
      "namespace",
      "new",
      "noexcept",
      "not",
      "not_eq",
      "nullptr",
      "operator",
      "or",
      "or_eq",
      "private",
      "protected",
      "public",
      "reflexpr",
      "register",
      "reinterpret_cast",
      "requires",
      "return",
      "short",
      "signed",
      "sizeof",
      "static",
      "static_assert",
      "static_cast",
      "struct",
      "switch",
      "synchronized",
      "template",
      "this",
      "thread_local",
      "throw",
      "true",
      "try",
      "typedef",
      "typeid",
      "typename",
      "union",
      "unsigned",
      "using",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "while",
      "xor",
      "xor_eq",
  });
  return std::find(kKeywords.begin(), kKeywords.end(), value) !=
         kKeywords.end();
}

bool ValidateIdentifier(CppGenerationResult &result, std::string_view value,
                        std::string_view subject,
                        const idl::SemanticProvenance &provenance) {
  if (!IsSnakeIdentifier(value)) {
    AddDiagnostic(result, "schema.cpp.identifier_invalid",
                  std::string(subject) + " '" + std::string(value) +
                      "' is not a canonical snake_case C++ identifier.",
                  provenance);
    return false;
  }
  if (IsCppKeyword(value)) {
    AddDiagnostic(result, "schema.cpp.identifier_reserved",
                  std::string(subject) + " '" + std::string(value) +
                      "' is reserved by C++20.",
                  provenance);
    return false;
  }
  return true;
}

std::string_view CppFieldIdentifier(const idl::SemanticField &field) {
  return field.cppName.has_value() ? std::string_view(*field.cppName)
                                   : std::string_view(field.name);
}

bool ValidateFieldIdentifier(CppGenerationResult &result,
                             const idl::SemanticField &field) {
  if (!IsSnakeIdentifier(field.name))
    return ValidateIdentifier(result, field.name, "field", field.provenance);
  return ValidateIdentifier(result, CppFieldIdentifier(field),
                            field.cppName.has_value() ? "projected field"
                                                      : "field",
                            field.provenance);
}

std::vector<std::string_view> SplitNamespace(std::string_view value) {
  std::vector<std::string_view> components;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t separator = value.find("::", start);
    const std::size_t end =
        separator == std::string_view::npos ? value.size() : separator;
    components.push_back(value.substr(start, end - start));
    if (separator == std::string_view::npos)
      break;
    start = separator + 2;
  }
  return components;
}

bool ValidateOptions(CppGenerationResult &result,
                     const CppGenerationOptions &options) {
  bool valid = true;
  const auto namespace_components = SplitNamespace(options.rootNamespace);
  if (namespace_components.empty())
    valid = false;
  for (const auto component : namespace_components) {
    if (IsSnakeIdentifier(component) && !IsCppKeyword(component))
      continue;
    valid = false;
  }
  if (!valid) {
    AddDiagnostic(result, "schema.cpp.root_namespace_invalid",
                  "The C++ root namespace must contain only non-reserved "
                  "snake_case components.");
  }

  if (options.includeRoot.empty() || options.includeRoot.front() == '/' ||
      options.includeRoot.back() == '/') {
    AddDiagnostic(
        result, "schema.cpp.include_root_invalid",
        "The generated include root must be a non-empty relative path.");
    return false;
  }
  std::size_t start = 0;
  while (start <= options.includeRoot.size()) {
    const std::size_t separator = options.includeRoot.find('/', start);
    const std::size_t end =
        separator == std::string::npos ? options.includeRoot.size() : separator;
    const std::string_view component(options.includeRoot.data() + start,
                                     end - start);
    if (!IsSnakeIdentifier(component) || IsCppKeyword(component)) {
      AddDiagnostic(result, "schema.cpp.include_root_invalid",
                    "The generated include root must contain only non-reserved "
                    "snake_case path components.");
      return false;
    }
    if (separator == std::string::npos)
      break;
    start = separator + 1;
  }
  return valid;
}

std::string ArtifactPath(const idl::SemanticSource &source,
                         const CppGenerationOptions &options) {
  return options.includeRoot + "/" + source.schemaName + "/v" +
         std::to_string(source.schemaVersion) + ".hpp";
}

std::string NamespaceName(const idl::SemanticSource &source,
                          const CppGenerationOptions &options) {
  return options.rootNamespace + "::" + source.schemaName + "::v" +
         std::to_string(source.schemaVersion);
}

std::string EscapeString(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (character >= 0x20 && character <= 0x7e)
        escaped.push_back(static_cast<char>(character));
      else {
        static constexpr char kHex[] = "0123456789abcdef";
        escaped += "\\x";
        escaped.push_back(kHex[(character >> 4) & 0x0f]);
        escaped.push_back(kHex[character & 0x0f]);
        escaped += "\"\"";
      }
      break;
    }
  }
  return escaped;
}

std::string StringLiteral(std::string_view value) {
  return "\"" + EscapeString(value) + "\"";
}

std::string CommentText(std::string_view value) {
  std::string result(value);
  std::replace(result.begin(), result.end(), '\n', ' ');
  std::replace(result.begin(), result.end(), '\r', ' ');
  return result;
}

std::string EnumValueIdentifier(std::string_view canonical_name) {
  return IsCppKeyword(canonical_name) ? "value_" + std::string(canonical_name)
                                      : std::string(canonical_name);
}

template <typename Value, typename Id>
std::vector<const Value *> SortedDefinitions(const std::vector<Value> &values,
                                             idl::SourceHandle source,
                                             Id Value::*id) {
  std::vector<const Value *> result;
  for (const auto &value : values) {
    if (value.source == source)
      result.push_back(&value);
  }
  std::sort(result.begin(), result.end(),
            [id](const auto *lhs, const auto *rhs) {
              if ((lhs->*id).value != (rhs->*id).value)
                return (lhs->*id).value < (rhs->*id).value;
              return lhs->name < rhs->name;
            });
  return result;
}

std::vector<const idl::SemanticModel *>
GeneratedModels(const idl::SemanticIr &ir, idl::SourceHandle source) {
  auto models =
      SortedDefinitions(ir.models, source, &idl::SemanticModel::typeId);
  std::erase_if(models, [](const auto *model) { return model->external; });
  return models;
}

const idl::SemanticTypeDefinition *FindType(const idl::SemanticIr &ir,
                                            idl::SemanticTypeHandle handle) {
  if (!handle.valid() || handle.value >= ir.semanticTypes.size())
    return nullptr;
  return &ir.semanticTypes[handle.value];
}

const idl::WireTypeDefinition *FindWireType(const idl::SemanticIr &ir,
                                            idl::WireTypeHandle handle) {
  if (!handle.valid() || handle.value >= ir.wireTypes.size())
    return nullptr;
  return &ir.wireTypes[handle.value];
}

const idl::StorageCodecDefinition *
FindStorageCodec(const idl::SemanticIr &ir, idl::StorageCodecHandle handle) {
  if (!handle.valid() || handle.value >= ir.storageCodecs.size())
    return nullptr;
  return &ir.storageCodecs[handle.value];
}

const idl::SemanticEnum *FindEnum(const idl::SemanticIr &ir,
                                  idl::EnumHandle handle) {
  if (!handle.valid() || handle.value >= ir.enums.size())
    return nullptr;
  return &ir.enums[handle.value];
}

const idl::SemanticModel *FindModel(const idl::SemanticIr &ir,
                                    idl::ModelHandle handle) {
  if (!handle.valid() || handle.value >= ir.models.size())
    return nullptr;
  return &ir.models[handle.value];
}

template <typename Member>
std::string_view CatalogMemberName(const Member &member) {
  return member.catalogIdentity.has_value()
             ? std::string_view(member.catalogIdentity->name)
             : std::string_view(member.name);
}

template <typename Member>
bool CatalogMemberLess(const Member &lhs, const Member &rhs) {
  if (lhs.catalogIdentity.has_value() != rhs.catalogIdentity.has_value())
    return lhs.catalogIdentity.has_value();
  if (lhs.catalogIdentity.has_value() &&
      lhs.catalogIdentity->declarationOrder !=
          rhs.catalogIdentity->declarationOrder)
    return lhs.catalogIdentity->declarationOrder <
           rhs.catalogIdentity->declarationOrder;
  return CatalogMemberName(lhs) < CatalogMemberName(rhs);
}

bool EnumValueLess(const idl::SemanticEnumValue &lhs,
                   const idl::SemanticEnumValue &rhs) {
  if (lhs.catalogIdentity.has_value() != rhs.catalogIdentity.has_value())
    return lhs.catalogIdentity.has_value();
  if (lhs.catalogIdentity.has_value() &&
      lhs.catalogIdentity->declarationOrder !=
          rhs.catalogIdentity->declarationOrder)
    return lhs.catalogIdentity->declarationOrder <
           rhs.catalogIdentity->declarationOrder;
  if (lhs.valueId != rhs.valueId)
    return lhs.valueId.value < rhs.valueId.value;
  return lhs.name < rhs.name;
}

const idl::SemanticField *
FindField(const idl::SemanticModel &model,
          const idl::SemanticFieldReference &reference) {
  const auto found = std::find_if(model.fields.begin(), model.fields.end(),
                                  [&](const auto &field) {
                                    return field.fieldId == reference.fieldId &&
                                           field.handle == reference.handle;
                                  });
  return found == model.fields.end() ? nullptr : &*found;
}

std::string QualifiedTypeName(CppGenerationResult &result,
                              const idl::SemanticIr &ir,
                              const std::vector<SourceGenerationInfo> &sources,
                              idl::SourceHandle current_source,
                              const idl::SemanticTypeReference &reference,
                              const idl::SemanticProvenance &provenance) {
  const auto *type = FindType(ir, reference.semanticType);
  if (type == nullptr || type->semanticTypeId != reference.semanticTypeId) {
    AddDiagnostic(
        result, "schema.cpp.semantic_type_unresolved",
        "A generated C++ field references an unresolved semantic type.",
        provenance);
    return "void";
  }
  if (!type->source.valid() || type->source.value >= sources.size() ||
      sources[type->source.value].source == nullptr) {
    AddDiagnostic(result, "schema.cpp.semantic_type_source_unresolved",
                  "A generated C++ semantic type references an unresolved "
                  "source fragment.",
                  type->provenance);
    return "void";
  }
  if (type->source == current_source)
    return "::" + sources[current_source.value].namespaceName +
           "::" + type->name;
  return "::" + sources[type->source.value].namespaceName + "::" + type->name;
}

std::string QualifiedModelName(CppGenerationResult &result,
                               const idl::SemanticIr &ir,
                               const std::vector<SourceGenerationInfo> &sources,
                               const idl::SemanticModelReference &reference,
                               const idl::SemanticProvenance &provenance) {
  const auto *model = FindModel(ir, reference.model);
  if (model == nullptr || model->typeId != reference.typeId) {
    AddDiagnostic(result, "schema.cpp.model_reference_unresolved",
                  "A generated C++ operation references an unresolved model.",
                  provenance);
    return "void";
  }
  if (!model->source.valid() || model->source.value >= sources.size() ||
      sources[model->source.value].source == nullptr) {
    AddDiagnostic(result, "schema.cpp.model_reference_source_unresolved",
                  "A generated C++ operation model references an unresolved "
                  "source fragment.",
                  model->provenance);
    return "void";
  }
  return "::" + sources[model->source.value].namespaceName + "::" + model->name;
}

std::string UnderlyingCppType(idl::SemanticTypeKind kind) {
  switch (kind) {
  case idl::SemanticTypeKind::Bool:
    return "bool";
  case idl::SemanticTypeKind::U8:
    return "::std::uint8_t";
  case idl::SemanticTypeKind::U16:
    return "::std::uint16_t";
  case idl::SemanticTypeKind::U32:
    return "::std::uint32_t";
  case idl::SemanticTypeKind::U64:
    return "::std::uint64_t";
  case idl::SemanticTypeKind::I8:
    return "::std::int8_t";
  case idl::SemanticTypeKind::I16:
    return "::std::int16_t";
  case idl::SemanticTypeKind::I32:
    return "::std::int32_t";
  case idl::SemanticTypeKind::I64:
  case idl::SemanticTypeKind::Timestamp:
    return "::std::int64_t";
  case idl::SemanticTypeKind::F32:
    return "float";
  case idl::SemanticTypeKind::F64:
    return "double";
  case idl::SemanticTypeKind::Text:
  case idl::SemanticTypeKind::JsonText:
    return "::std::string";
  case idl::SemanticTypeKind::Blob:
    return "::std::vector<::std::byte>";
  case idl::SemanticTypeKind::Enum:
  case idl::SemanticTypeKind::FixedId:
  case idl::SemanticTypeKind::Decimal:
    break;
  }
  return {};
}

std::string GeneratedQueryOperatorName(idl::QueryOperator operation) {
  switch (operation) {
  case idl::QueryOperator::Equal:
    return "equal";
  case idl::QueryOperator::NotEqual:
    return "not_equal";
  case idl::QueryOperator::In:
    return "in";
  case idl::QueryOperator::NotIn:
    return "not_in";
  case idl::QueryOperator::LessThan:
    return "less_than";
  case idl::QueryOperator::LessThanOrEqual:
    return "less_than_or_equal";
  case idl::QueryOperator::GreaterThan:
    return "greater_than";
  case idl::QueryOperator::GreaterThanOrEqual:
    return "greater_than_or_equal";
  case idl::QueryOperator::Prefix:
    return "prefix";
  case idl::QueryOperator::Suffix:
    return "suffix";
  case idl::QueryOperator::Contains:
    return "contains";
  case idl::QueryOperator::IsNull:
    return "is_null";
  }
  return "equal";
}

std::string QueryMaskExpression(const idl::SemanticQueryPolicy &query) {
  if (query.operators.empty())
    return "0u";
  std::vector<idl::QueryOperator> operations = query.operators;
  std::sort(operations.begin(), operations.end(), [](auto lhs, auto rhs) {
    return static_cast<std::uint8_t>(lhs) < static_cast<std::uint8_t>(rhs);
  });
  operations.erase(std::unique(operations.begin(), operations.end()),
                   operations.end());
  std::string expression;
  for (const auto operation : operations) {
    if (!expression.empty())
      expression += "\n        | ";
    expression += "schema_cpp::query_operator_bit(schema_cpp::query_operator::";
    expression += GeneratedQueryOperatorName(operation);
    expression += ")";
  }
  return expression;
}

std::string GeneratedOperationTargetName(idl::OperationTargetProfile target) {
  switch (target) {
  case idl::OperationTargetProfile::Cpp:
    return "cpp";
  case idl::OperationTargetProfile::Fabric:
    return "fabric";
  case idl::OperationTargetProfile::HttpJson:
    return "http_json";
  }
  return "cpp";
}

std::string OperationTargetMaskExpression(
    const std::vector<idl::OperationTargetProfile> &targets) {
  if (targets.empty())
    return "0u";
  auto ordered = targets;
  std::sort(ordered.begin(), ordered.end(), [](auto lhs, auto rhs) {
    return static_cast<std::uint8_t>(lhs) < static_cast<std::uint8_t>(rhs);
  });
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
  std::string expression;
  for (const auto target : ordered) {
    if (!expression.empty())
      expression += "\n        | ";
    expression += "schema_cpp::operation_target_bit(schema_cpp::operation_"
                  "target_profile::";
    expression += GeneratedOperationTargetName(target);
    expression += ")";
  }
  return expression;
}

std::string GeneratedSemanticTypeKindName(idl::SemanticTypeKind kind) {
  switch (kind) {
  case idl::SemanticTypeKind::Bool:
    return "boolean";
  case idl::SemanticTypeKind::U8:
    return "u8";
  case idl::SemanticTypeKind::U16:
    return "u16";
  case idl::SemanticTypeKind::U32:
    return "u32";
  case idl::SemanticTypeKind::U64:
    return "u64";
  case idl::SemanticTypeKind::I8:
    return "i8";
  case idl::SemanticTypeKind::I16:
    return "i16";
  case idl::SemanticTypeKind::I32:
    return "i32";
  case idl::SemanticTypeKind::I64:
    return "i64";
  case idl::SemanticTypeKind::F32:
    return "f32";
  case idl::SemanticTypeKind::F64:
    return "f64";
  case idl::SemanticTypeKind::Text:
    return "text";
  case idl::SemanticTypeKind::Enum:
    return "enumeration";
  case idl::SemanticTypeKind::Blob:
    return "blob";
  case idl::SemanticTypeKind::FixedId:
    return "fixed_id";
  case idl::SemanticTypeKind::Decimal:
    return "decimal";
  case idl::SemanticTypeKind::Timestamp:
    return "timestamp";
  case idl::SemanticTypeKind::JsonText:
    return "json_text";
  }
  return "u32";
}

std::string GeneratedFieldKindName(idl::FieldKind kind) {
  switch (kind) {
  case idl::FieldKind::ObjectId:
    return "object_id";
  case idl::FieldKind::InternalId:
    return "internal_id";
  case idl::FieldKind::Text:
    return "text";
  case idl::FieldKind::Slug:
    return "slug";
  case idl::FieldKind::DisplayName:
    return "display_name";
  case idl::FieldKind::Url:
    return "url";
  case idl::FieldKind::Email:
    return "email";
  case idl::FieldKind::Enum:
    return "enumeration";
  case idl::FieldKind::Bool:
    return "boolean";
  case idl::FieldKind::TimestampMs:
    return "timestamp_ms";
  case idl::FieldKind::JsonText:
    return "json_text";
  case idl::FieldKind::Secret:
    return "secret";
  case idl::FieldKind::EthereumAddress:
    return "ethereum_address";
  case idl::FieldKind::HashHex:
    return "hash_hex";
  case idl::FieldKind::TokenHash:
    return "token_hash";
  case idl::FieldKind::KeyReference:
    return "key_reference";
  case idl::FieldKind::Integer:
    return "integer";
  case idl::FieldKind::Real:
    return "real";
  case idl::FieldKind::Decimal:
    return "decimal";
  case idl::FieldKind::Blob:
    return "blob";
  }
  return "text";
}

std::string GeneratedWireTypeKindName(idl::WirePrimitiveKind kind) {
  switch (kind) {
  case idl::WirePrimitiveKind::Bool:
    return "boolean";
  case idl::WirePrimitiveKind::U8:
    return "u8";
  case idl::WirePrimitiveKind::U16:
    return "u16";
  case idl::WirePrimitiveKind::U32:
    return "u32";
  case idl::WirePrimitiveKind::U64:
    return "u64";
  case idl::WirePrimitiveKind::I8:
    return "i8";
  case idl::WirePrimitiveKind::I16:
    return "i16";
  case idl::WirePrimitiveKind::I32:
    return "i32";
  case idl::WirePrimitiveKind::I64:
    return "i64";
  case idl::WirePrimitiveKind::F32:
    return "f32";
  case idl::WirePrimitiveKind::F64:
    return "f64";
  case idl::WirePrimitiveKind::Utf8:
    return "utf8";
  case idl::WirePrimitiveKind::Bytes:
    return "bytes";
  case idl::WirePrimitiveKind::FixedBytes:
    return "fixed_bytes";
  case idl::WirePrimitiveKind::Decimal128:
    return "decimal128";
  }
  return "u32";
}

std::string GeneratedStorageCodecKindName(idl::StorageCodecKind kind) {
  switch (kind) {
  case idl::StorageCodecKind::SqliteInteger:
    return "sqlite_integer";
  case idl::StorageCodecKind::SqliteReal:
    return "sqlite_real";
  case idl::StorageCodecKind::SqliteText:
    return "sqlite_text";
  case idl::StorageCodecKind::SqliteBlob:
    return "sqlite_blob";
  case idl::StorageCodecKind::SqliteFixedBlob:
    return "sqlite_fixed_blob";
  case idl::StorageCodecKind::SqliteOrderedU64:
    return "sqlite_ordered_u64";
  case idl::StorageCodecKind::SqliteOrderedDecimal128:
    return "sqlite_ordered_decimal128";
  }
  return "sqlite_integer";
}

std::string
GeneratedValueRepresentationKindName(idl::SemanticTypeKind semantic,
                                     idl::WirePrimitiveKind wire,
                                     idl::StorageCodecKind storage) {
  const bool numeric_enum_wire = wire == idl::WirePrimitiveKind::U8 ||
                                 wire == idl::WirePrimitiveKind::U16 ||
                                 wire == idl::WirePrimitiveKind::U32;
  if (semantic == idl::SemanticTypeKind::Enum && numeric_enum_wire &&
      storage == idl::StorageCodecKind::SqliteText)
    return "enum_name";
  if ((semantic == idl::SemanticTypeKind::Blob ||
       semantic == idl::SemanticTypeKind::FixedId) &&
      wire == idl::WirePrimitiveKind::FixedBytes &&
      storage == idl::StorageCodecKind::SqliteText)
    return "hex_text";
  return "native";
}

std::string
GeneratedSqliteStorageClassName(idl::SqliteStorageClass storage_class) {
  switch (storage_class) {
  case idl::SqliteStorageClass::Integer:
    return "integer";
  case idl::SqliteStorageClass::Real:
    return "real";
  case idl::SqliteStorageClass::Text:
    return "text";
  case idl::SqliteStorageClass::Blob:
    return "blob";
  }
  return "integer";
}

std::string GeneratedCheckKindName(idl::CheckKind kind) {
  switch (kind) {
  case idl::CheckKind::MinimumInteger:
    return "minimum_integer";
  case idl::CheckKind::MaximumInteger:
    return "maximum_integer";
  case idl::CheckKind::MinimumReal:
    return "minimum_real";
  case idl::CheckKind::MaximumReal:
    return "maximum_real";
  case idl::CheckKind::MinimumLength:
    return "minimum_length";
  case idl::CheckKind::MaximumLength:
    return "maximum_length";
  case idl::CheckKind::FixedLength:
    return "fixed_length";
  case idl::CheckKind::EnumDomain:
    return "enum_domain";
  case idl::CheckKind::ValidJson:
    return "valid_json";
  case idl::CheckKind::DecimalPrecision:
    return "decimal_precision";
  case idl::CheckKind::DecimalScale:
    return "decimal_scale";
  }
  return "minimum_integer";
}

std::string GeneratedCheckUnitName(idl::CheckUnit unit) {
  switch (unit) {
  case idl::CheckUnit::None:
    return "none";
  case idl::CheckUnit::Utf8Bytes:
    return "utf8_bytes";
  case idl::CheckUnit::Bytes:
    return "bytes";
  }
  return "none";
}

std::string GeneratedFieldDefaultKindName(idl::FieldDefaultKind kind) {
  switch (kind) {
  case idl::FieldDefaultKind::None:
    return "none";
  case idl::FieldDefaultKind::Literal:
    return "literal";
  case idl::FieldDefaultKind::Boundary:
    return "boundary";
  }
  return "none";
}

std::string
GeneratedBoundaryDefaultSourceName(idl::BoundaryDefaultSource source) {
  switch (source) {
  case idl::BoundaryDefaultSource::None:
    return "none";
  case idl::BoundaryDefaultSource::RequestTime:
    return "request_time";
  case idl::BoundaryDefaultSource::GeneratedId:
    return "generated_id";
  case idl::BoundaryDefaultSource::GeneratedToken:
    return "generated_token";
  }
  return "none";
}

std::string GeneratedTargetProfileName(idl::TargetProfile profile) {
  switch (profile) {
  case idl::TargetProfile::Cpp:
    return "cpp";
  case idl::TargetProfile::Sqlite:
    return "sqlite";
  case idl::TargetProfile::CSharp:
    return "csharp";
  case idl::TargetProfile::JavaScript:
    return "javascript";
  case idl::TargetProfile::Fabric:
    return "fabric";
  case idl::TargetProfile::HttpJson:
    return "http_json";
  }
  return "cpp";
}

std::string
TargetProfileMaskExpression(const std::vector<idl::TargetProfile> &targets) {
  if (targets.empty())
    return "0u";
  std::vector<idl::TargetProfile> sorted = targets;
  std::sort(sorted.begin(), sorted.end(), [](auto lhs, auto rhs) {
    return static_cast<std::uint8_t>(lhs) < static_cast<std::uint8_t>(rhs);
  });
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  std::string expression;
  for (const auto target : sorted) {
    if (!expression.empty())
      expression += "\n        | ";
    expression += "schema_cpp::target_profile_bit(schema_cpp::target_profile::";
    expression += GeneratedTargetProfileName(target);
    expression += ")";
  }
  return expression;
}

std::string OperationTargetProfileMaskExpression(
    const std::vector<idl::OperationTargetProfile> &targets) {
  std::vector<idl::TargetProfile> mapped;
  mapped.reserve(targets.size());
  for (const auto target : targets) {
    switch (target) {
    case idl::OperationTargetProfile::Cpp:
      mapped.push_back(idl::TargetProfile::Cpp);
      break;
    case idl::OperationTargetProfile::Fabric:
      mapped.push_back(idl::TargetProfile::Fabric);
      break;
    case idl::OperationTargetProfile::HttpJson:
      mapped.push_back(idl::TargetProfile::HttpJson);
      break;
    }
  }
  return TargetProfileMaskExpression(mapped);
}

std::string GeneratedNormalizationKindName(idl::NormalizationKind kind) {
  switch (kind) {
  case idl::NormalizationKind::None:
    return "none";
  case idl::NormalizationKind::Trim:
    return "trim";
  case idl::NormalizationKind::Lowercase:
    return "lowercase";
  case idl::NormalizationKind::LowercaseTrim:
    return "lowercase_trim";
  case idl::NormalizationKind::EthereumAddress:
    return "ethereum_address";
  case idl::NormalizationKind::Url:
    return "url";
  }
  return "none";
}

std::string GeneratedFieldSourceKindName(idl::FieldSourceKind kind) {
  switch (kind) {
  case idl::FieldSourceKind::Canonical:
    return "canonical";
  case idl::FieldSourceKind::Imported:
    return "imported";
  case idl::FieldSourceKind::Derived:
    return "derived";
  case idl::FieldSourceKind::BoundaryGenerated:
    return "boundary_generated";
  }
  return "canonical";
}

std::string GeneratedSerializationPolicyName(idl::SerializationPolicy policy) {
  switch (policy) {
  case idl::SerializationPolicy::Include:
    return "include";
  case idl::SerializationPolicy::Omit:
    return "omit";
  }
  return "include";
}

std::string GeneratedModelKindName(idl::ModelKind kind) {
  switch (kind) {
  case idl::ModelKind::Entity:
    return "entity";
  case idl::ModelKind::Join:
    return "join";
  case idl::ModelKind::Credential:
    return "credential";
  case idl::ModelKind::Ephemeral:
    return "ephemeral";
  case idl::ModelKind::AuditLedger:
    return "audit_ledger";
  case idl::ModelKind::Provenance:
    return "provenance";
  case idl::ModelKind::ImportLedger:
    return "import_ledger";
  }
  return "entity";
}

std::string GeneratedModelRoleName(idl::ModelRole role) {
  switch (role) {
  case idl::ModelRole::Unspecified:
    return "unspecified";
  case idl::ModelRole::UniversalSpine:
    return "universal_spine";
  case idl::ModelRole::DomainFacet:
    return "domain_facet";
  case idl::ModelRole::Definition:
    return "definition";
  case idl::ModelRole::Revision:
    return "revision";
  case idl::ModelRole::Instance:
    return "instance";
  case idl::ModelRole::Occurrence:
    return "occurrence";
  case idl::ModelRole::Manifestation:
    return "manifestation";
  case idl::ModelRole::Representation:
    return "representation";
  case idl::ModelRole::Assertion:
    return "assertion";
  case idl::ModelRole::Observation:
    return "observation";
  case idl::ModelRole::AuthoritativeAggregate:
    return "authoritative_aggregate";
  case idl::ModelRole::Ledger:
    return "ledger";
  case idl::ModelRole::Registry:
    return "registry";
  case idl::ModelRole::Projection:
    return "projection";
  case idl::ModelRole::ImportRecord:
    return "import_record";
  case idl::ModelRole::Receipt:
    return "receipt";
  case idl::ModelRole::Membership:
    return "membership";
  case idl::ModelRole::ValueObject:
    return "value_object";
  }
  return "unspecified";
}

std::string GeneratedRetentionPolicyName(idl::RetentionPolicy policy) {
  switch (policy) {
  case idl::RetentionPolicy::Durable:
    return "durable";
  case idl::RetentionPolicy::UntilExpiry:
    return "until_expiry";
  case idl::RetentionPolicy::Immutable:
    return "immutable";
  case idl::RetentionPolicy::UntilPromotion:
    return "until_promotion";
  }
  return "durable";
}

std::string
GeneratedRelationCardinalityName(idl::RelationCardinality cardinality) {
  switch (cardinality) {
  case idl::RelationCardinality::One:
    return "one";
  case idl::RelationCardinality::OptionalOne:
    return "optional_one";
  case idl::RelationCardinality::Many:
    return "many";
  }
  return "one";
}

std::string GeneratedReferentialActionName(idl::ReferentialAction action) {
  switch (action) {
  case idl::ReferentialAction::Restrict:
    return "restrict";
  case idl::ReferentialAction::Cascade:
    return "cascade";
  case idl::ReferentialAction::SetNull:
    return "set_null";
  }
  return "restrict";
}

std::string GeneratedKeyKindName(idl::KeyKind kind) {
  switch (kind) {
  case idl::KeyKind::Primary:
    return "primary";
  case idl::KeyKind::Alternate:
    return "alternate";
  }
  return "alternate";
}

std::string GeneratedNameReviewStatusName(idl::NameReviewStatus status) {
  switch (status) {
  case idl::NameReviewStatus::Canonical:
    return "canonical";
  case idl::NameReviewStatus::ReviewRequired:
    return "review_required";
  case idl::NameReviewStatus::ApprovedException:
    return "approved_exception";
  }
  return "canonical";
}

void ValidateSourceIdentifiers(CppGenerationResult &result,
                               const idl::SemanticIr &ir,
                               const idl::SemanticSource &source) {
  const idl::SemanticProvenance source_provenance{.sourcePath = source.path};
  ValidateIdentifier(result, source.schemaName, "schema", source_provenance);

  for (const auto *type :
       SortedDefinitions(ir.semanticTypes, source.handle,
                         &idl::SemanticTypeDefinition::semanticTypeId)) {
    ValidateIdentifier(result, type->name, "semantic type", type->provenance);
  }
  for (const auto *enumeration :
       SortedDefinitions(ir.enums, source.handle, &idl::SemanticEnum::enumId)) {
    ValidateIdentifier(result, enumeration->name, "enum",
                       enumeration->provenance);
    for (const auto &value : enumeration->values)
      ValidateIdentifier(result, EnumValueIdentifier(value.name),
                         "projected enum value", value.provenance);
  }
  for (const auto *model : GeneratedModels(ir, source.handle)) {
    ValidateIdentifier(result, model->name, "model", model->provenance);
    for (const auto &field : model->fields)
      ValidateFieldIdentifier(result, field);
    for (const auto &one_of : model->oneOfs) {
      ValidateIdentifier(result, one_of.name, "one_of", one_of.provenance);
      for (const auto &variant : one_of.variants)
        ValidateIdentifier(result,
                           model->name + '_' + one_of.name + '_' + variant.tag,
                           "projected one_of variant", variant.provenance);
    }
  }
  for (const auto *command : SortedDefinitions(
           ir.commands, source.handle, &idl::SemanticCommand::commandId)) {
    ValidateIdentifier(result, command->name, "command", command->provenance);
  }
  for (const auto *event : SortedDefinitions(ir.events, source.handle,
                                             &idl::SemanticEvent::eventId)) {
    ValidateIdentifier(result, event->name, "event", event->provenance);
  }
}

void ValidateGeneratedNameCollisions(CppGenerationResult &result,
                                     const idl::SemanticIr &ir,
                                     const idl::SemanticSource &source,
                                     bool emits_registry, bool separate_validation) {
  struct NameOwner {
    std::string subject;
    idl::SemanticProvenance provenance;
  };
  std::map<std::string, NameOwner> names;
  const auto register_name = [&](std::string name, std::string subject,
                                 const auto &provenance) {
    const auto [existing, inserted] = names.emplace(
        name, NameOwner{.subject = subject, .provenance = provenance});
    if (inserted)
      return;
    result.diagnostics.push_back({
        .code = "schema.cpp.generated_name_collision",
        .severity = idl::DiagnosticSeverity::Error,
        .message = "Generated C++ " + subject + " '" + name +
                   "' collides with " + existing->second.subject + ".",
        .sourceName = provenance.sourcePath,
        .span = provenance.span,
        .relatedSpans = {{
            .message = "The first generated name is owned here.",
            .sourceName = existing->second.provenance.sourcePath,
            .span = existing->second.provenance.span,
        }},
    });
  };

  register_name("schema_cpp", "runtime namespace alias",
                idl::SemanticProvenance{.sourcePath = source.path});
  register_name("schema_descriptor_data", "descriptor storage namespace",
                idl::SemanticProvenance{.sourcePath = source.path});
  register_name("schema_catalog", "runtime schema catalog",
                idl::SemanticProvenance{.sourcePath = source.path});
  if (emits_registry) {
    register_name("schema_registry_data", "registry storage namespace",
                  idl::SemanticProvenance{.sourcePath = source.path});
    register_name("schema_registry", "compile-time schema registry",
                  idl::SemanticProvenance{.sourcePath = source.path});
  }
  register_name("commands", "generated command namespace",
                idl::SemanticProvenance{.sourcePath = source.path});
  register_name("events", "generated event namespace",
                idl::SemanticProvenance{.sourcePath = source.path});

  for (const auto *enumeration :
       SortedDefinitions(ir.enums, source.handle, &idl::SemanticEnum::enumId)) {
    register_name(enumeration->name, "enum", enumeration->provenance);
    register_name(enumeration->name + "_enum_id", "enum identity constant",
                  enumeration->provenance);
    register_name("is_valid_" + enumeration->name, "enum domain validator",
                  enumeration->provenance);
  }
  for (const auto *type :
       SortedDefinitions(ir.semanticTypes, source.handle,
                         &idl::SemanticTypeDefinition::semanticTypeId)) {
    bool shared_enum_name = false;
    if (type->kind == idl::SemanticTypeKind::Enum &&
        type->definingEnum.has_value()) {
      const auto *enumeration = FindEnum(ir, *type->definingEnum);
      shared_enum_name = enumeration != nullptr &&
                         enumeration->source == source.handle &&
                         enumeration->name == type->name;
    }
    if (!shared_enum_name)
      register_name(type->name, "semantic type", type->provenance);
    register_name(type->name + "_semantic_type_id",
                  "semantic type identity constant", type->provenance);
  }
  for (const auto *model : GeneratedModels(ir, source.handle)) {
    register_name(model->name, "model", model->provenance);
    register_name(model->name + "_row", "row alias", model->provenance);
    register_name(model->name + "_message", "message alias", model->provenance);
    register_name(model->name + "_validation_result", "validation result alias",
                  model->provenance);
    register_name(model->name + "_type_id", "model identity constant",
                  model->provenance);
    register_name(model->name + "_fields", "field token namespace",
                  model->provenance);
    register_name(model->name + "_query", "query surface", model->provenance);
    register_name(model->name + "_query_options", "typed query options",
                  model->provenance);
    register_name(model->name + "_primary_key", "primary-key type",
                  model->provenance);
    register_name(model->name + "_changes", "typed change set",
                  model->provenance);
    register_name(model->name + "_binding", "storage binding adapter",
                  model->provenance);
    if (separate_validation)
      register_name(model->name + "_validator", "model validator", model->provenance);
    register_name(model->name + "_binding_result", "storage binding result",
                  model->provenance);
    register_name(model->name + "_decode_result", "storage decode result",
                  model->provenance);
    register_name(model->name + "_wire_decode_result", "wire decode result",
                  model->provenance);
    for (const auto &one_of : model->oneOfs) {
      register_name(model->name + '_' + one_of.name, "closed union",
                    one_of.provenance);
      for (const auto &variant : one_of.variants) {
        register_name(model->name + '_' + one_of.name + '_' + variant.tag,
                      "closed union variant", variant.provenance);
      }
    }

    std::map<std::string, idl::SemanticProvenance> field_names;
    for (const auto &field : model->fields) {
      const std::string projected_name(CppFieldIdentifier(field));
      for (const std::string &generated_name :
           {projected_name, projected_name + "_token"}) {
        const auto [existing, inserted] =
            field_names.emplace(generated_name, field.provenance);
        if (inserted)
          continue;
        result.diagnostics.push_back({
            .code = "schema.cpp.generated_name_collision",
            .severity = idl::DiagnosticSeverity::Error,
            .message = "Generated C++ field token name '" + generated_name +
                       "' collides within model '" + model->name + "'.",
            .sourceName = field.provenance.sourcePath,
            .span = field.provenance.span,
            .relatedSpans = {{
                .message =
                    "The first generated field token name is owned here.",
                .sourceName = existing->second.sourcePath,
                .span = existing->second.span,
            }},
        });
      }
    }
  }

  const auto validate_operation_names = [&](const auto &operations,
                                            std::string_view subject) {
    std::map<std::string, idl::SemanticProvenance> generated_names;
    for (const auto *operation : operations) {
      for (const std::string &generated_name : {
               operation->name,
               operation->name + "_token",
           }) {
        const auto [existing, inserted] =
            generated_names.emplace(generated_name, operation->provenance);
        if (inserted)
          continue;
        result.diagnostics.push_back({
            .code = "schema.cpp.generated_name_collision",
            .severity = idl::DiagnosticSeverity::Error,
            .message = "Generated C++ " + std::string(subject) + " name '" +
                       generated_name +
                       "' collides within its operation namespace.",
            .sourceName = operation->provenance.sourcePath,
            .span = operation->provenance.span,
            .relatedSpans = {{
                .message = "The first generated operation name is owned here.",
                .sourceName = existing->second.sourcePath,
                .span = existing->second.span,
            }},
        });
      }
    }
  };
  validate_operation_names(SortedDefinitions(ir.commands, source.handle,
                                             &idl::SemanticCommand::commandId),
                           "command");
  validate_operation_names(
      SortedDefinitions(ir.events, source.handle, &idl::SemanticEvent::eventId),
      "event");
}

std::string EnumUnderlyingType(CppGenerationResult &result,
                               const idl::SemanticIr &ir,
                               const idl::SemanticEnum &enumeration) {
  const auto *semantic_type = FindType(ir, enumeration.semanticType);
  if (semantic_type == nullptr ||
      semantic_type->semanticTypeId != enumeration.semanticTypeId ||
      !semantic_type->wireType.valid() ||
      semantic_type->wireType.value >= ir.wireTypes.size()) {
    AddDiagnostic(
        result, "schema.cpp.enum_wire_type_unresolved",
        "A generated C++ enum does not resolve to its governed wire type.",
        enumeration.provenance);
    return "::std::uint32_t";
  }
  switch (ir.wireTypes[semantic_type->wireType.value].kind) {
  case idl::WirePrimitiveKind::U8:
    return "::std::uint8_t";
  case idl::WirePrimitiveKind::U16:
    return "::std::uint16_t";
  case idl::WirePrimitiveKind::U32:
    return "::std::uint32_t";
  case idl::WirePrimitiveKind::Utf8:
    return "::std::uint32_t";
  default:
    AddDiagnostic(result, "schema.cpp.enum_wire_type_invalid",
                  "A generated C++ enum requires a u8, u16, or u32 wire type.",
                  enumeration.provenance);
    return "::std::uint32_t";
  }
}

void EmitEnums(CppGenerationResult &result, std::ostringstream &output,
               const idl::SemanticIr &ir, const idl::SemanticSource &source) {
  for (const auto *enumeration :
       SortedDefinitions(ir.enums, source.handle, &idl::SemanticEnum::enumId)) {
    output << "enum class " << enumeration->name << " : "
           << EnumUnderlyingType(result, ir, *enumeration) << "\n{\n";
    std::vector<const idl::SemanticEnumValue *> values;
    for (const auto &value : enumeration->values)
      values.push_back(&value);
    std::sort(values.begin(), values.end(),
              [](const auto *lhs, const auto *rhs) {
                return EnumValueLess(*lhs, *rhs);
              });
    for (std::size_t index = 0; index < values.size(); ++index) {
      output << "    " << EnumValueIdentifier(values[index]->name) << " = "
             << values[index]->valueId.value << "u";
      if (index + 1 != values.size())
        output << ',';
      output << '\n';
    }
    output << "};\n";
    output << "inline constexpr ::std::uint32_t " << enumeration->name
           << "_enum_id = " << enumeration->enumId.value << "u;\n\n";
    output << "[[nodiscard]] constexpr bool is_valid_" << enumeration->name
           << "(" << enumeration->name << " value) noexcept\n{\n"
           << "    switch (value)\n    {\n";
    for (const auto *value : values)
      output << "        case " << enumeration->name
             << "::" << EnumValueIdentifier(value->name) << ":\n";
    output << "            return true;\n"
           << "    }\n"
           << "    return false;\n"
           << "}\n\n";
    const auto *semantic_type = FindType(ir, enumeration->semanticType);
    const auto *storage =
        semantic_type == nullptr
            ? nullptr
            : FindStorageCodec(ir, semantic_type->storageCodec);
    if (storage != nullptr &&
        storage->kind == idl::StorageCodecKind::SqliteText) {
      output << "[[nodiscard]] constexpr ::std::string_view "
             << "arken_schema_enum_storage_name(" << enumeration->name
             << " value) noexcept\n{\n"
             << "    switch (value)\n    {\n";
      for (const auto *value : values)
        output << "        case " << enumeration->name
               << "::" << EnumValueIdentifier(value->name) << ": return "
               << StringLiteral(CatalogMemberName(*value)) << ";\n";
      output << "    }\n"
             << "    return {};\n"
             << "}\n\n"
             << "[[nodiscard]] constexpr ::std::optional<" << enumeration->name
             << "> "
             << "arken_schema_enum_from_storage_name(" << enumeration->name
             << "*, ::std::string_view value) noexcept\n{\n";
      for (const auto *value : values)
        output << "    if (value == "
               << StringLiteral(CatalogMemberName(*value)) << ") return "
               << enumeration->name << "::" << EnumValueIdentifier(value->name)
               << ";\n";
      output << "    return ::std::nullopt;\n"
             << "}\n\n";
    }
  }
}

void EmitSemanticTypes(CppGenerationResult &result, std::ostringstream &output,
                       const idl::SemanticIr &ir,
                       const idl::SemanticSource &source,
                       const std::vector<SourceGenerationInfo> &sources) {
  for (const auto *type :
       SortedDefinitions(ir.semanticTypes, source.handle,
                         &idl::SemanticTypeDefinition::semanticTypeId)) {
    std::string target;
    if (type->kind == idl::SemanticTypeKind::FixedId) {
      target = "schema_cpp::fixed_id<" + std::to_string(type->fixedSize) +
               "u, " + std::to_string(type->semanticTypeId.value) + "u>";
    } else if (type->kind == idl::SemanticTypeKind::Decimal) {
      target = "schema_cpp::decimal128_value<" +
               std::to_string(type->precision) + "u, " +
               std::to_string(type->scale) + "u, " +
               std::to_string(type->semanticTypeId.value) + "u>";
    } else if (type->kind == idl::SemanticTypeKind::Enum) {
      if (!type->definingEnum.has_value()) {
        AddDiagnostic(
            result, "schema.cpp.enum_type_unresolved",
            "An enum semantic type does not reference its defining enum.",
            type->provenance);
        continue;
      }
      const auto *enumeration = FindEnum(ir, *type->definingEnum);
      if (enumeration == nullptr) {
        AddDiagnostic(
            result, "schema.cpp.enum_type_unresolved",
            "An enum semantic type references an unresolved defining enum.",
            type->provenance);
        continue;
      }
      if (enumeration->source == source.handle)
        target = enumeration->name;
      else if (enumeration->source.valid() &&
               enumeration->source.value < sources.size() &&
               sources[enumeration->source.value].source != nullptr)
        target = "::" + sources[enumeration->source.value].namespaceName +
                 "::" + enumeration->name;
      else {
        AddDiagnostic(
            result, "schema.cpp.enum_source_unresolved",
            "An enum semantic type references an unresolved source fragment.",
            type->provenance);
        continue;
      }
    } else {
      const std::string underlying = UnderlyingCppType(type->kind);
      if (underlying.empty()) {
        AddDiagnostic(result, "schema.cpp.semantic_type_unsupported",
                      "A semantic type has no C++ target mapping.",
                      type->provenance);
        continue;
      }
      target = "schema_cpp::strong_value<" + underlying + ", " +
               std::to_string(type->semanticTypeId.value) + "u>";
    }

    if (target != type->name)
      output << "using " << type->name << " = " << target << ";\n";
    output << "inline constexpr ::std::uint32_t " << type->name
           << "_semantic_type_id = " << type->semanticTypeId.value << "u;\n\n";
  }
}

std::vector<const idl::SemanticFieldReference *> SortedFieldReferences(
    const std::vector<idl::SemanticFieldReference> &references) {
  std::vector<const idl::SemanticFieldReference *> result;
  for (const auto &reference : references)
    result.push_back(&reference);
  std::sort(result.begin(), result.end(), [](const auto *lhs, const auto *rhs) {
    if (lhs->fieldId.value != rhs->fieldId.value)
      return lhs->fieldId.value < rhs->fieldId.value;
    return lhs->canonicalName < rhs->canonicalName;
  });
  return result;
}

bool FieldDeclarationOrderLess(const idl::SemanticField *lhs,
                               const idl::SemanticField *rhs) {
  if (lhs->catalogIdentity.has_value() && rhs->catalogIdentity.has_value() &&
      lhs->catalogIdentity->declarationOrder !=
          rhs->catalogIdentity->declarationOrder)
    return lhs->catalogIdentity->declarationOrder <
           rhs->catalogIdentity->declarationOrder;
  if (lhs->fieldId.value != rhs->fieldId.value)
    return lhs->fieldId.value < rhs->fieldId.value;
  return lhs->name < rhs->name;
}

void EmitOneOfs(CppGenerationResult &result, std::ostringstream &output,
                const idl::SemanticIr &ir, const idl::SemanticSource &source,
                const idl::SemanticModel &model,
                const std::vector<SourceGenerationInfo> &sources) {
  std::vector<const idl::SemanticOneOf *> one_ofs;
  for (const auto &one_of : model.oneOfs)
    one_ofs.push_back(&one_of);
  std::sort(
      one_ofs.begin(), one_ofs.end(),
      [](const auto *lhs, const auto *rhs) { return lhs->name < rhs->name; });
  for (const auto *one_of : one_ofs) {
    std::vector<const idl::SemanticOneOfVariant *> variants;
    for (const auto &variant : one_of->variants)
      variants.push_back(&variant);
    std::sort(
        variants.begin(), variants.end(),
        [](const auto *lhs, const auto *rhs) { return lhs->tag < rhs->tag; });

    for (const auto *variant : variants) {
      output << "struct " << model.name << '_' << one_of->name << '_'
             << variant->tag << "\n{\n";
      for (const auto *reference : SortedFieldReferences(variant->fields)) {
        const auto *field = FindField(model, *reference);
        if (field == nullptr) {
          AddDiagnostic(result, "schema.cpp.one_of_field_unresolved",
                        "A generated C++ one_of variant references an "
                        "unresolved required field.",
                        variant->provenance);
          continue;
        }
        const std::string type = QualifiedTypeName(
            result, ir, sources, source.handle, field->type, field->provenance);
        output << "    " << type << ' ' << CppFieldIdentifier(*field)
               << "{};\n";
      }
      for (const auto *reference :
           SortedFieldReferences(variant->optionalFields)) {
        const auto *field = FindField(model, *reference);
        if (field == nullptr) {
          AddDiagnostic(result, "schema.cpp.one_of_field_unresolved",
                        "A generated C++ one_of variant references an "
                        "unresolved optional field.",
                        variant->provenance);
          continue;
        }
        const std::string type = QualifiedTypeName(
            result, ir, sources, source.handle, field->type, field->provenance);
        output << "    ::std::optional<" << type << "> "
               << CppFieldIdentifier(*field) << "{};\n";
      }
      output << "};\n\n";
    }

    output << "using " << model.name << '_' << one_of->name
           << " = ::std::variant<\n";
    for (std::size_t index = 0; index < variants.size(); ++index) {
      output << "    " << model.name << '_' << one_of->name << '_'
             << variants[index]->tag;
      if (index + 1 != variants.size())
        output << ',';
      output << '\n';
    }
    output << ">;\n\n";
  }
}

void EmitModel(CppGenerationResult &result, std::ostringstream &output,
               const idl::SemanticIr &ir, const idl::SemanticSource &source,
               const idl::SemanticModel &model,
               const std::vector<SourceGenerationInfo> &sources) {
  EmitOneOfs(result, output, ir, source, model, sources);

  std::vector<const idl::SemanticField *> fields;
  for (const auto &field : model.fields)
    fields.push_back(&field);
  std::sort(fields.begin(), fields.end(), FieldDeclarationOrderLess);

  output << "inline constexpr ::std::uint32_t " << model.name
         << "_type_id = " << model.typeId.value << "u;\n\n";
  output << "struct " << model.name << "\n{\n";
  for (const auto *field : fields) {
    const std::string type = QualifiedTypeName(
        result, ir, sources, source.handle, field->type, field->provenance);
    output << "    ";
    if (field->cardinality == idl::FieldCardinality::Repeated)
      output << "::std::vector<";
    else if (!field->required)
      output << "::std::optional<";
    output << type;
    if (field->cardinality == idl::FieldCardinality::Repeated ||
        !field->required)
      output << '>';
    output << ' ' << CppFieldIdentifier(*field) << "{};\n";
  }
  output << "};\n\n";
  output << "using " << model.name << "_row = " << model.name << ";\n";
  output << "using " << model.name << "_message = " << model.name << ";\n";
  output << "using " << model.name
         << "_validation_result = schema_cpp::validation_result<"
         << model.typeId.value << "u>;\n\n";

  const auto primary_key =
      std::find_if(model.keys.begin(), model.keys.end(), [](const auto &key) {
        return key.kind == idl::KeyKind::Primary;
      });
  if (primary_key == model.keys.end()) {
    AddDiagnostic(result, "schema.cpp.primary_key_missing",
                  "A generated C++ model requires one resolved primary key.",
                  model.provenance);
  } else {
    output << "struct " << model.name << "_primary_key\n{\n";
    for (const auto &reference : primary_key->fields) {
      const auto *field = FindField(model, reference);
      if (field == nullptr) {
        AddDiagnostic(
            result, "schema.cpp.primary_key_field_unresolved",
            "A generated C++ primary key references an unresolved field.",
            primary_key->provenance);
        continue;
      }
      output << "    "
             << QualifiedTypeName(result, ir, sources, source.handle,
                                  field->type, field->provenance)
             << ' ' << CppFieldIdentifier(*field) << "{};\n";
    }
    output << "};\n\n";
  }

  output << "struct " << model.name << "_changes\n{\n";
  for (const auto *field : fields) {
    if (field->primaryKey || field->immutable)
      continue;
    const std::string type = QualifiedTypeName(
        result, ir, sources, source.handle, field->type, field->provenance);
    output << "    schema_cpp::field_update<";
    if (field->cardinality == idl::FieldCardinality::Repeated)
      output << "::std::vector<" << type << ">";
    else
      output << type;
    output << ", "
           << (field->required ||
                       field->cardinality == idl::FieldCardinality::Repeated
                   ? "false"
                   : "true")
           << "> " << CppFieldIdentifier(*field) << "{};\n";
  }
  output << "};\n\n";

  output << "namespace " << model.name << "_fields\n{\n";
  for (const auto *field : fields) {
    const std::string type = QualifiedTypeName(
        result, ir, sources, source.handle, field->type, field->provenance);
    const auto cpp_name = CppFieldIdentifier(*field);
    output << "using " << cpp_name << "_token = schema_cpp::field_token<\n"
           << "    " << model.name << ",\n"
           << "    " << type << ",\n"
           << "    " << model.typeId.value << "u,\n"
           << "    " << field->fieldId.value << "u,\n"
           << "    " << QueryMaskExpression(field->query) << ",\n"
           << "    " << (field->query.sortable ? "true" : "false") << ",\n"
           << "    " << (field->query.cursor ? "true" : "false") << "\n"
           << ">;\n";
    output << "inline constexpr " << cpp_name << "_token " << cpp_name << "{\""
           << EscapeString(field->name) << "\"};\n\n";
  }
  output << "}\n\n";

  std::vector<const idl::SemanticField *> query_fields;
  for (const auto *field : fields) {
    if (!field->query.operators.empty() || field->query.sortable ||
        field->query.cursor)
      query_fields.push_back(field);
  }
  output << "using " << model.name << "_query = schema_cpp::query_surface<"
         << model.name;
  for (const auto *field : query_fields)
    output << ", " << model.name << "_fields::" << CppFieldIdentifier(*field)
           << "_token";
  output << ">;\n\n";
  output << "using " << model.name
         << "_query_options = schema_cpp::typed_query_options<" << model.name
         << ">;\n\n";
}

struct DescriptorModelLayout {
  const idl::SemanticModel *model = nullptr;
  std::vector<const idl::SemanticField *> fields;
  std::vector<const idl::SemanticRelation *> relations;
  std::size_t fieldOffset = 0;
  std::size_t relationOffset = 0;
};

std::vector<const idl::SemanticField *>
SortedFields(const idl::SemanticModel &model) {
  std::vector<const idl::SemanticField *> fields;
  for (const auto &field : model.fields)
    fields.push_back(&field);
  std::sort(fields.begin(), fields.end(), FieldDeclarationOrderLess);
  return fields;
}

std::vector<const idl::SemanticRelation *>
SortedRelations(const idl::SemanticModel &model) {
  std::vector<const idl::SemanticRelation *> relations;
  for (const auto &relation : model.relations)
    relations.push_back(&relation);
  std::sort(relations.begin(), relations.end(),
            [](const auto *lhs, const auto *rhs) {
              if (CatalogMemberLess(*lhs, *rhs))
                return true;
              if (CatalogMemberLess(*rhs, *lhs))
                return false;
              if (lhs->targetTypeId.value != rhs->targetTypeId.value)
                return lhs->targetTypeId.value < rhs->targetTypeId.value;
              return lhs->targetQualifiedName < rhs->targetQualifiedName;
            });
  return relations;
}

std::vector<DescriptorModelLayout>
BuildDescriptorLayout(const idl::SemanticIr &ir,
                      const idl::SemanticSource &source) {
  std::vector<DescriptorModelLayout> layout;
  std::size_t field_offset = 0;
  std::size_t relation_offset = 0;
  for (const auto *model : GeneratedModels(ir, source.handle)) {
    auto fields = SortedFields(*model);
    auto relations = SortedRelations(*model);
    layout.push_back({
        .model = model,
        .fields = std::move(fields),
        .relations = std::move(relations),
        .fieldOffset = field_offset,
        .relationOffset = relation_offset,
    });
    field_offset += layout.back().fields.size();
    relation_offset += layout.back().relations.size();
  }
  return layout;
}

std::size_t FieldCount(const std::vector<DescriptorModelLayout> &layout) {
  if (layout.empty())
    return 0;
  const auto &last = layout.back();
  return last.fieldOffset + last.fields.size();
}

std::size_t RelationCount(const std::vector<DescriptorModelLayout> &layout) {
  if (layout.empty())
    return 0;
  const auto &last = layout.back();
  return last.relationOffset + last.relations.size();
}

std::vector<const idl::SemanticCheck *>
SortedChecks(const idl::SemanticField &field) {
  std::vector<const idl::SemanticCheck *> checks;
  checks.reserve(field.checks.size());
  for (const auto &check : field.checks)
    checks.push_back(&check);
  std::sort(checks.begin(), checks.end(), [](const auto *lhs, const auto *rhs) {
    if (CatalogMemberLess(*lhs, *rhs))
      return true;
    if (CatalogMemberLess(*rhs, *lhs))
      return false;
    return std::tie(lhs->kind, lhs->unit, lhs->value) <
           std::tie(rhs->kind, rhs->unit, rhs->value);
  });
  return checks;
}

std::vector<const idl::SemanticEnumValue *>
SortedEnumValues(const idl::SemanticIr &ir, const idl::SemanticField &field) {
  std::vector<const idl::SemanticEnumValue *> values;
  if (!field.type.definingEnum.has_value())
    return values;
  const auto *enumeration = FindEnum(ir, *field.type.definingEnum);
  if (enumeration == nullptr)
    return values;
  values.reserve(enumeration->values.size());
  for (const auto &value : enumeration->values)
    if (field.allowedEnumValueIds.empty() ||
        std::find(field.allowedEnumValueIds.begin(),
                  field.allowedEnumValueIds.end(),
                  value.valueId) != field.allowedEnumValueIds.end())
      values.push_back(&value);
  std::sort(values.begin(), values.end(), [](const auto *lhs, const auto *rhs) {
    return EnumValueLess(*lhs, *rhs);
  });
  return values;
}

void EmitFieldContractTables(std::ostringstream &output,
                             const idl::SemanticIr &ir,
                             const std::vector<DescriptorModelLayout> &layout) {
  std::size_t field_ordinal = 0;
  for (const auto &model_layout : layout) {
    for (const auto *field : model_layout.fields) {
      const auto enum_values = SortedEnumValues(ir, *field);
      output
          << "inline constexpr ::std::array<schema_cpp::enum_value_descriptor, "
          << enum_values.size() << "u> field_enum_values_" << field_ordinal
          << "{{\n";
      for (const auto *value : enum_values) {
        output << "    {.value_id = " << value->valueId.value
               << "u, .name = " << StringLiteral(CatalogMemberName(*value))
               << "},\n";
      }
      output << "}};\n";

      const auto checks = SortedChecks(*field);
      output << "inline constexpr ::std::array<schema_cpp::check_descriptor, "
             << checks.size() << "u> field_checks_" << field_ordinal << "{{\n";
      for (const auto *check : checks) {
        output << "    {\n"
               << "        .name = " << StringLiteral(CatalogMemberName(*check))
               << ",\n"
               << "        .kind = schema_cpp::check_kind::"
               << GeneratedCheckKindName(check->kind) << ",\n"
               << "        .unit = schema_cpp::check_unit::"
               << GeneratedCheckUnitName(check->unit) << ",\n"
               << "        .value = " << StringLiteral(check->value) << ",\n"
               << "        .message = " << StringLiteral(check->message)
               << ",\n"
               << "        .targets = "
               << TargetProfileMaskExpression(check->targets) << ",\n"
               << "        .generated = "
               << (check->generated ? "true" : "false") << ",\n"
               << "    },\n";
      }
      output << "}};\n\n";
      ++field_ordinal;
    }
  }
}

void EmitFieldDescriptorTable(
    CppGenerationResult &result, std::ostringstream &output,
    const idl::SemanticIr &ir,
    const std::vector<DescriptorModelLayout> &layout) {
  std::size_t field_ordinal = 0;
  output << "inline constexpr ::std::array<schema_cpp::field_descriptor, "
         << FieldCount(layout) << "u> all_fields{{\n";
  for (const auto &model_layout : layout) {
    for (const auto *field : model_layout.fields) {
      const auto *type = FindType(ir, field->type.semanticType);
      if (type == nullptr ||
          type->semanticTypeId != field->type.semanticTypeId) {
        AddDiagnostic(result, "schema.cpp.descriptor_type_unresolved",
                      "A generated field descriptor references an unresolved "
                      "semantic type.",
                      field->provenance);
      }
      const auto *wire = FindWireType(ir, field->representation.wireType);
      const auto *storage =
          FindStorageCodec(ir, field->representation.storageCodec);
      if (wire == nullptr || storage == nullptr) {
        AddDiagnostic(result, "schema.cpp.descriptor_codec_unresolved",
                      "A generated field descriptor references an unresolved "
                      "wire type or storage codec.",
                      field->provenance);
      }
      const std::uint32_t semantic_fixed_size =
          type == nullptr ? 0u : type->fixedSize;
      const std::uint32_t wire_fixed_size =
          field->representation.wireSize != 0u
              ? field->representation.wireSize
              : (wire == nullptr ? 0u : wire->fixedSize);
      const std::uint32_t storage_fixed_size =
          field->representation.storageSize != 0u
              ? field->representation.storageSize
              : (storage == nullptr ? 0u : storage->fixedSize);
      output
          << "    {\n"
          << "        .model_type_id = " << model_layout.model->typeId.value
          << "u,\n"
          << "        .field_id = " << field->fieldId.value << "u,\n"
          << "        .semantic_type_id = " << field->type.semanticTypeId.value
          << "u,\n"
          << "        .wire_type_id = "
          << field->representation.wireTypeId.value << "u,\n"
          << "        .storage_codec_id = "
          << field->representation.storageCodecId.value << "u,\n"
          << "        .semantic_fixed_size = " << semantic_fixed_size << "u,\n"
          << "        .wire_fixed_size = " << wire_fixed_size << "u,\n"
          << "        .precision = " << field->representation.precision
          << "u,\n"
          << "        .scale = " << field->representation.scale << "u,\n"
          << "        .storage_fixed_size = " << storage_fixed_size << "u,\n"
          << "        .name = " << StringLiteral(field->name) << ",\n"
          << "        .storage_name = " << StringLiteral(field->storageName)
          << ",\n"
          << "        .semantic_type_name = "
          << StringLiteral(type == nullptr ? field->type.canonicalName
                                           : (type->semanticName.empty()
                                                  ? type->qualifiedName
                                                  : type->semanticName))
          << ",\n"
          << "        .semantic_type_registry_key = "
          << StringLiteral(type == nullptr ? std::string{} : type->registryKey)
          << ",\n"
          << "        .field_kind = schema_cpp::schema_field_kind::"
          << GeneratedFieldKindName(field->representation.fieldKind) << ",\n"
          << "        .type_kind = schema_cpp::semantic_type_kind::"
          << GeneratedSemanticTypeKindName(field->type.kind) << ",\n"
          << "        .wire_type = schema_cpp::wire_type_kind::"
          << GeneratedWireTypeKindName(
                 wire == nullptr ? idl::WirePrimitiveKind::U32 : wire->kind)
          << ",\n"
          << "        .storage_codec = schema_cpp::storage_codec_kind::"
          << GeneratedStorageCodecKindName(
                 storage == nullptr ? idl::StorageCodecKind::SqliteInteger
                                    : storage->kind)
          << ",\n"
          << "        .storage_class = schema_cpp::sqlite_storage_class::"
          << GeneratedSqliteStorageClassName(
                 storage == nullptr ? idl::SqliteStorageClass::Integer
                                    : storage->storageClass)
          << ",\n"
          << "        .normalization = schema_cpp::normalization_kind::"
          << GeneratedNormalizationKindName(field->representation.normalization)
          << ",\n"
          << "        .source_kind = schema_cpp::field_source_kind::"
          << GeneratedFieldSourceKindName(field->sourceKind) << ",\n"
          << "        .serialization = schema_cpp::serialization_policy::"
          << GeneratedSerializationPolicyName(field->serialization) << ",\n"
          << "        .default_kind = schema_cpp::field_default_kind::"
          << GeneratedFieldDefaultKindName(field->defaultValue.kind) << ",\n"
          << "        .boundary_default = schema_cpp::boundary_default_source::"
          << GeneratedBoundaryDefaultSourceName(
                 field->defaultValue.boundarySource)
          << ",\n"
          << "        .default_literal = "
          << StringLiteral(field->defaultValue.literal) << ",\n"
          << "        .catalog_default_value = "
          << StringLiteral(field->defaultValue.catalogValue) << ",\n"
          << "        .source_field = "
          << StringLiteral(field->sourceField.value_or(std::string{})) << ",\n"
          << "        .import_transform = "
          << StringLiteral(field->importTransform) << ",\n"
          << "        .enum_values = "
             "schema_cpp::descriptor_span(field_enum_values_"
          << field_ordinal << "),\n"
          << "        .checks = schema_cpp::descriptor_span(field_checks_"
          << field_ordinal << "),\n"
          << "        .query_operators = " << QueryMaskExpression(field->query)
          << ",\n"
          << "        .cardinality = schema_cpp::field_cardinality::"
          << GeneratedFieldCardinalityName(field->cardinality) << ",\n"
          << "        .maximum_items = " << field->maximumItems.value_or(0u)
          << "u,\n"
          << "        .redacted = " << (field->redacted ? "true" : "false")
          << ",\n"
          << "        .immutable = " << (field->immutable ? "true" : "false")
          << ",\n"
          << "        .write_once = " << (field->writeOnce ? "true" : "false")
          << ",\n"
          << "        .primary_key = " << (field->primaryKey ? "true" : "false")
          << ",\n"
          << "        .sortable = "
          << (field->query.sortable ? "true" : "false") << ",\n"
          << "        .cursor = " << (field->query.cursor ? "true" : "false")
          << ",\n"
          << "        .storage_exact = "
          << (storage != nullptr && storage->exact ? "true" : "false") << ",\n"
          << "        .storage_order_preserving = "
          << (storage != nullptr && storage->orderPreserving ? "true" : "false")
          << ",\n"
          << "        .catalog_registry_key = "
          << StringLiteral(field->catalogIdentity.has_value()
                               ? field->catalogIdentity->registryKey
                               : std::string{})
          << ",\n"
          << "        .catalog_field_name = "
          << StringLiteral(field->catalogIdentity.has_value()
                               ? field->catalogIdentity->fieldName
                               : std::string{})
          << ",\n"
          << "        .catalog_declaration_order = "
          << (field->catalogIdentity.has_value()
                  ? field->catalogIdentity->declarationOrder
                  : 0u)
          << "u,\n"
          << "    },\n";
      ++field_ordinal;
    }
  }
  output << "}};\n\n";
}

void EmitFieldReferenceArray(
    std::ostringstream &output, std::string_view name,
    const std::vector<idl::SemanticFieldReference> &references) {
  output << "inline constexpr "
            "::std::array<schema_cpp::field_reference_descriptor, "
         << references.size() << "u> " << name << "{{\n";
  for (const auto &reference : references) {
    output << "    {.field_id = " << reference.fieldId.value
           << "u, .canonical_name = " << StringLiteral(reference.canonicalName)
           << "},\n";
  }
  output << "}};\n";
}

void EmitRelationDescriptorTable(
    std::ostringstream &output,
    const std::vector<DescriptorModelLayout> &layout) {
  std::size_t relation_ordinal = 0;
  for (const auto &model_layout : layout) {
    for (const auto *relation : model_layout.relations) {
      EmitFieldReferenceArray(
          output, "relation_source_fields_" + std::to_string(relation_ordinal),
          relation->sourceFields);
      EmitFieldReferenceArray(
          output, "relation_target_fields_" + std::to_string(relation_ordinal),
          relation->targetFields);
      output << '\n';
      ++relation_ordinal;
    }
  }

  output << "inline constexpr ::std::array<schema_cpp::relation_descriptor, "
         << RelationCount(layout) << "u> all_relations{{\n";
  relation_ordinal = 0;
  for (const auto &model_layout : layout) {
    for (const auto *relation : model_layout.relations) {
      output << "    {\n"
             << "        .source_model_type_id = "
             << model_layout.model->typeId.value << "u,\n"
             << "        .target_model_type_id = "
             << relation->targetTypeId.value << "u,\n"
             << "        .name = "
             << StringLiteral(CatalogMemberName(*relation)) << ",\n"
             << "        .target_qualified_name = "
             << StringLiteral(relation->targetQualifiedName) << ",\n"
             << "        .source_fields = "
                "schema_cpp::descriptor_span(relation_source_fields_"
             << relation_ordinal << "),\n"
             << "        .target_fields = "
                "schema_cpp::descriptor_span(relation_target_fields_"
             << relation_ordinal << "),\n"
             << "        .cardinality = schema_cpp::relation_cardinality::"
             << GeneratedRelationCardinalityName(relation->cardinality) << ",\n"
             << "        .on_delete = schema_cpp::referential_action::"
             << GeneratedReferentialActionName(relation->onDelete) << ",\n"
             << "        .on_update = schema_cpp::referential_action::"
             << GeneratedReferentialActionName(relation->onUpdate) << ",\n"
             << "    },\n";
      ++relation_ordinal;
    }
  }
  output << "}};\n\n";
}

template <typename Value>
std::vector<const Value *> SortedByName(const std::vector<Value> &values) {
  std::vector<const Value *> sorted;
  sorted.reserve(values.size());
  for (const auto &value : values)
    sorted.push_back(&value);
  std::sort(sorted.begin(), sorted.end(), [](const auto *lhs, const auto *rhs) {
    return CatalogMemberLess(*lhs, *rhs);
  });
  return sorted;
}

void EmitStringViewArray(std::ostringstream &output, std::string_view name,
                         std::vector<std::string> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  output << "inline constexpr ::std::array<::std::string_view, "
         << values.size() << "u> " << name << "{{\n";
  for (const auto &value : values)
    output << "    " << StringLiteral(value) << ",\n";
  output << "}};\n";
}

std::uint32_t OneOfTagValueId(const idl::SemanticIr &ir,
                              const idl::SemanticOneOf &one_of,
                              std::string_view tag) {
  const auto *enumeration = FindEnum(ir, one_of.enumeration);
  if (enumeration == nullptr)
    return 0;
  const auto found =
      std::find_if(enumeration->values.begin(), enumeration->values.end(),
                   [tag](const auto &value) { return value.name == tag; });
  return found == enumeration->values.end() ? 0 : found->valueId.value;
}

void EmitModelContractTables(std::ostringstream &output,
                             const idl::SemanticIr &ir,
                             const std::vector<DescriptorModelLayout> &layout) {
  for (std::size_t model_ordinal = 0; model_ordinal < layout.size();
       ++model_ordinal) {
    const auto &model = *layout[model_ordinal].model;
    const auto keys = SortedByName(model.keys);
    for (std::size_t ordinal = 0; ordinal < keys.size(); ++ordinal) {
      EmitFieldReferenceArray(output,
                              "key_fields_" + std::to_string(model_ordinal) +
                                  "_" + std::to_string(ordinal),
                              keys[ordinal]->fields);
    }
    output << "inline constexpr ::std::array<schema_cpp::key_descriptor, "
           << keys.size() << "u> model_keys_" << model_ordinal << "{{\n";
    for (std::size_t ordinal = 0; ordinal < keys.size(); ++ordinal) {
      output << "    {\n"
             << "        .name = "
             << StringLiteral(CatalogMemberName(*keys[ordinal])) << ",\n"
             << "        .kind = schema_cpp::key_kind::"
             << GeneratedKeyKindName(keys[ordinal]->kind) << ",\n"
             << "        .fields = schema_cpp::descriptor_span(key_fields_"
             << model_ordinal << "_" << ordinal << "),\n"
             << "        .generated = "
             << (keys[ordinal]->generated ? "true" : "false") << ",\n"
             << "    },\n";
    }
    output << "}};\n\n";

    const auto indexes = SortedByName(model.indexes);
    for (std::size_t ordinal = 0; ordinal < indexes.size(); ++ordinal) {
      EmitFieldReferenceArray(output,
                              "index_fields_" + std::to_string(model_ordinal) +
                                  "_" + std::to_string(ordinal),
                              indexes[ordinal]->fields);
    }
    output << "inline constexpr ::std::array<schema_cpp::index_descriptor, "
           << indexes.size() << "u> model_indexes_" << model_ordinal << "{{\n";
    for (std::size_t ordinal = 0; ordinal < indexes.size(); ++ordinal) {
      output << "    {\n"
             << "        .name = "
             << StringLiteral(CatalogMemberName(*indexes[ordinal])) << ",\n"
             << "        .fields = schema_cpp::descriptor_span(index_fields_"
             << model_ordinal << "_" << ordinal << "),\n"
             << "        .unique = "
             << (indexes[ordinal]->unique ? "true" : "false") << ",\n"
             << "        .generated = "
             << (indexes[ordinal]->generated ? "true" : "false") << ",\n"
             << "    },\n";
    }
    output << "}};\n\n";

    const auto joins = SortedByName(model.joins);
    output << "inline constexpr ::std::array<schema_cpp::join_descriptor, "
           << joins.size() << "u> model_joins_" << model_ordinal << "{{\n";
    for (const auto *join : joins) {
      output << "    {\n"
             << "        .name = " << StringLiteral(CatalogMemberName(*join))
             << ",\n"
             << "        .through_model_type_id = " << join->throughTypeId.value
             << "u,\n"
             << "        .through_qualified_name = "
             << StringLiteral(join->throughQualifiedName) << ",\n"
             << "        .source_relation = "
             << StringLiteral(join->sourceRelation) << ",\n"
             << "        .target_relation = "
             << StringLiteral(join->targetRelation) << ",\n"
             << "    },\n";
    }
    output << "}};\n\n";

    const auto one_ofs = SortedByName(model.oneOfs);
    for (std::size_t one_of_ordinal = 0; one_of_ordinal < one_ofs.size();
         ++one_of_ordinal) {
      std::vector<const idl::SemanticOneOfVariant *> variants;
      for (const auto &variant : one_ofs[one_of_ordinal]->variants)
        variants.push_back(&variant);
      std::sort(variants.begin(), variants.end(),
                [](const auto *lhs, const auto *rhs) {
                  if (lhs->catalogIdentity.has_value() !=
                      rhs->catalogIdentity.has_value())
                    return lhs->catalogIdentity.has_value();
                  if (lhs->catalogIdentity.has_value() &&
                      lhs->catalogIdentity->declarationOrder !=
                          rhs->catalogIdentity->declarationOrder)
                    return lhs->catalogIdentity->declarationOrder <
                           rhs->catalogIdentity->declarationOrder;
                  return lhs->tag < rhs->tag;
                });
      for (std::size_t variant_ordinal = 0; variant_ordinal < variants.size();
           ++variant_ordinal) {
        const auto prefix = "one_of_" + std::to_string(model_ordinal) + "_" +
                            std::to_string(one_of_ordinal) + "_variant_" +
                            std::to_string(variant_ordinal);
        EmitFieldReferenceArray(output, prefix + "_fields",
                                variants[variant_ordinal]->fields);
        EmitFieldReferenceArray(output, prefix + "_optional_fields",
                                variants[variant_ordinal]->optionalFields);
      }
      output << "inline constexpr "
                "::std::array<schema_cpp::one_of_variant_descriptor, "
             << variants.size() << "u> one_of_variants_" << model_ordinal << "_"
             << one_of_ordinal << "{{\n";
      for (std::size_t variant_ordinal = 0; variant_ordinal < variants.size();
           ++variant_ordinal) {
        const auto prefix = "one_of_" + std::to_string(model_ordinal) + "_" +
                            std::to_string(one_of_ordinal) + "_variant_" +
                            std::to_string(variant_ordinal);
        output << "    {\n"
               << "        .tag = "
               << StringLiteral(
                      variants[variant_ordinal]->catalogIdentity.has_value()
                          ? variants[variant_ordinal]->catalogIdentity->name
                          : variants[variant_ordinal]->tag)
               << ",\n"
               << "        .tag_value_id = "
               << OneOfTagValueId(ir, *one_ofs[one_of_ordinal],
                                  variants[variant_ordinal]->tag)
               << "u,\n"
               << "        .fields = schema_cpp::descriptor_span(" << prefix
               << "_fields),\n"
               << "        .optional_fields = schema_cpp::descriptor_span("
               << prefix << "_optional_fields),\n"
               << "    },\n";
      }
      output << "}};\n";
    }
    output << "inline constexpr ::std::array<schema_cpp::one_of_descriptor, "
           << one_ofs.size() << "u> model_one_ofs_" << model_ordinal << "{{\n";
    for (std::size_t ordinal = 0; ordinal < one_ofs.size(); ++ordinal) {
      const auto &discriminator = one_ofs[ordinal]->discriminator;
      output
          << "    {\n"
          << "        .name = "
          << StringLiteral(CatalogMemberName(*one_ofs[ordinal])) << ",\n"
          << "        .discriminator = {.field_id = "
          << discriminator.fieldId.value << "u, .canonical_name = "
          << StringLiteral(discriminator.canonicalName) << "},\n"
          << "        .variants = schema_cpp::descriptor_span(one_of_variants_"
          << model_ordinal << "_" << ordinal << "),\n"
          << "    },\n";
    }
    output << "}};\n\n";

    EmitStringViewArray(output,
                        "model_source_aliases_" + std::to_string(model_ordinal),
                        model.sourceAliases);
    EmitStringViewArray(
        output, "model_retired_names_" + std::to_string(model_ordinal),
        model.catalogRetiredNames.empty() ? model.retiredNames
                                          : model.catalogRetiredNames);
    EmitStringViewArray(output,
                        "model_resolution_retired_names_" +
                            std::to_string(model_ordinal),
                        model.retiredNames);
    output << '\n';
  }
}

void EmitDescriptorIndexes(std::ostringstream &output,
                           const std::vector<DescriptorModelLayout> &layout) {
  for (std::size_t model_ordinal = 0; model_ordinal < layout.size();
       ++model_ordinal) {
    const auto &model_layout = layout[model_ordinal];
    std::vector<std::pair<std::uint32_t, std::size_t>> field_ids;
    field_ids.reserve(model_layout.fields.size());
    for (std::size_t ordinal = 0; ordinal < model_layout.fields.size();
         ++ordinal) {
      field_ids.emplace_back(model_layout.fields[ordinal]->fieldId.value,
                             ordinal);
    }
    std::sort(field_ids.begin(), field_ids.end());

    output << "inline constexpr ::std::array<schema_cpp::numeric_index_entry, "
           << model_layout.fields.size() << "u> field_id_index_"
           << model_ordinal << "{{\n";
    for (const auto &[field_id, ordinal] : field_ids) {
      output << "    {.key = " << field_id
             << "u, .ordinal = " << ordinal << "u},\n";
    }
    output << "}};\n";

    std::vector<std::pair<std::string_view, std::size_t>> field_names;
    for (std::size_t ordinal = 0; ordinal < model_layout.fields.size();
         ++ordinal)
      field_names.emplace_back(model_layout.fields[ordinal]->name, ordinal);
    std::sort(field_names.begin(), field_names.end());
    output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
           << field_names.size() << "u> field_name_index_" << model_ordinal
           << "{{\n";
    for (const auto &[name, ordinal] : field_names) {
      output << "    {.key = " << StringLiteral(name)
             << ", .ordinal = " << ordinal << "u},\n";
    }
    output << "}};\n";

    std::vector<std::pair<std::string_view, std::size_t>> relation_names;
    for (std::size_t ordinal = 0; ordinal < model_layout.relations.size();
         ++ordinal)
      relation_names.emplace_back(
          CatalogMemberName(*model_layout.relations[ordinal]), ordinal);
    std::sort(relation_names.begin(), relation_names.end());
    output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
           << relation_names.size() << "u> relation_name_index_"
           << model_ordinal << "{{\n";
    for (const auto &[name, ordinal] : relation_names) {
      output << "    {.key = " << StringLiteral(name)
             << ", .ordinal = " << ordinal << "u},\n";
    }
    output << "}};\n\n";
  }
}

void EmitModelDescriptorTable(
    std::ostringstream &output,
    const std::vector<DescriptorModelLayout> &layout) {
  output << "inline constexpr ::std::array<schema_cpp::model_descriptor, "
         << layout.size() << "u> all_models{{\n";
  for (std::size_t ordinal = 0; ordinal < layout.size(); ++ordinal) {
    const auto &model_layout = layout[ordinal];
    const auto &model = *model_layout.model;
    output
        << "    {\n"
        << "        .type_id = " << model.typeId.value << "u,\n"
        << "        .name = " << StringLiteral(model.name) << ",\n"
        << "        .storage_name = " << StringLiteral(model.storageName)
        << ",\n"
        << "        .qualified_name = " << StringLiteral(model.qualifiedName)
        << ",\n"
        << "        .owner_capsule = " << StringLiteral(model.ownerCapsule)
        << ",\n"
        << "        .domain = "
        << StringLiteral(model.catalogIdentity.has_value() &&
                                 !model.catalogIdentity->domain.empty()
                             ? model.catalogIdentity->domain
                             : model.domain)
        << ",\n"
        << "        .description = " << StringLiteral(model.description)
        << ",\n"
        << "        .mutation_authority = "
        << StringLiteral(model.mutationAuthority) << ",\n"
        << "        .kind = schema_cpp::model_kind::"
        << GeneratedModelKindName(model.kind) << ",\n"
        << "        .role = schema_cpp::model_role::"
        << GeneratedModelRoleName(model.role) << ",\n"
        << "        .retention = schema_cpp::retention_policy::"
        << GeneratedRetentionPolicyName(model.retention) << ",\n"
        << "        .fields = schema_cpp::descriptor_slice(all_fields, "
        << model_layout.fieldOffset << "u, " << model_layout.fields.size()
        << "u),\n"
        << "        .keys = schema_cpp::descriptor_span(model_keys_" << ordinal
        << "),\n"
        << "        .indexes = schema_cpp::descriptor_span(model_indexes_"
        << ordinal << "),\n"
        << "        .relations = schema_cpp::descriptor_slice(all_relations, "
        << model_layout.relationOffset << "u, " << model_layout.relations.size()
        << "u),\n"
        << "        .joins = schema_cpp::descriptor_span(model_joins_"
        << ordinal << "),\n"
        << "        .one_ofs = schema_cpp::descriptor_span(model_one_ofs_"
        << ordinal << "),\n"
        << "        .source_aliases = "
           "schema_cpp::descriptor_span(model_source_aliases_"
        << ordinal << "),\n"
        << "        .retired_names = "
           "schema_cpp::descriptor_span(model_retired_names_"
        << ordinal << "),\n"
        << "        .resolution_retired_names = schema_cpp::descriptor_span("
        << "model_resolution_retired_names_" << ordinal << "),\n"
        << "        .name_review = schema_cpp::name_review_status::"
        << GeneratedNameReviewStatusName(model.nameReview) << ",\n"
        << "        .name_review_rationale = "
        << StringLiteral(model.nameReviewRationale) << ",\n"
        << "        .field_id_index = "
           "schema_cpp::descriptor_span(field_id_index_"
        << ordinal << "),\n"
        << "        .field_name_index = "
           "schema_cpp::descriptor_span(field_name_index_"
        << ordinal << "),\n"
        << "        .relation_name_index = "
           "schema_cpp::descriptor_span(relation_name_index_"
        << ordinal << "),\n"
        << "        .catalog_fragment_id = "
        << StringLiteral(model.catalogIdentity.has_value()
                             ? model.catalogIdentity->fragmentId
                             : std::string{})
        << ",\n"
        << "        .catalog_registry_key = "
        << StringLiteral(model.catalogIdentity.has_value()
                             ? model.catalogIdentity->registryKey
                             : std::string{})
        << ",\n"
        << "        .catalog_model_id = "
        << StringLiteral(model.catalogIdentity.has_value()
                             ? model.catalogIdentity->modelId
                             : std::string{})
        << ",\n"
        << "        .catalog_logical_name = "
        << StringLiteral(model.catalogIdentity.has_value()
                             ? model.catalogIdentity->logicalName
                             : std::string{})
        << ",\n"
        << "        .catalog_qualified_name = "
        << StringLiteral(model.catalogIdentity.has_value()
                             ? model.catalogIdentity->qualifiedName
                             : std::string{})
        << ",\n"
        << "        .catalog_declaration_order = "
        << (model.catalogIdentity.has_value()
                ? model.catalogIdentity->declarationOrder
                : 0u)
        << "u,\n"
        << "    },\n";
  }
  output << "}};\n\n";

  output << "inline constexpr ::std::array<schema_cpp::numeric_index_entry, "
         << layout.size() << "u> model_id_index{{\n";
  for (std::size_t ordinal = 0; ordinal < layout.size(); ++ordinal) {
    output << "    {.key = " << layout[ordinal].model->typeId.value
           << "u, .ordinal = " << ordinal << "u},\n";
  }
  output << "}};\n";

  std::vector<std::pair<std::string_view, std::size_t>> model_names;
  for (std::size_t ordinal = 0; ordinal < layout.size(); ++ordinal)
    model_names.emplace_back(layout[ordinal].model->name, ordinal);
  std::sort(model_names.begin(), model_names.end());
  output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
         << model_names.size() << "u> model_name_index{{\n";
  for (const auto &[name, ordinal] : model_names) {
    output << "    {.key = " << StringLiteral(name)
           << ", .ordinal = " << ordinal << "u},\n";
  }
  output << "}};\n";
}

void EmitWireDescriptorTables(
    CppGenerationResult &result, std::ostringstream &output,
    const idl::SemanticIr &ir,
    const std::vector<DescriptorModelLayout> &layout) {
  output << "inline constexpr ::std::array<schema_cpp::wire_field_descriptor, "
         << FieldCount(layout) << "u> all_wire_fields{{\n";
  std::size_t field_ordinal = 0;
  for (const auto &model_layout : layout) {
    for (const auto *field : model_layout.fields) {
      const auto *type = FindType(ir, field->type.semanticType);
      const auto *wire =
          type == nullptr ? nullptr : FindWireType(ir, type->wireType);
      const auto *storage =
          type == nullptr ? nullptr : FindStorageCodec(ir, type->storageCodec);
      if (type == nullptr || wire == nullptr || storage == nullptr) {
        AddDiagnostic(result, "schema.cpp.wire_descriptor_unresolved",
                      "A generated wire field descriptor references an "
                      "unresolved semantic, wire, or storage type.",
                      field->provenance);
      }
      const auto kind =
          wire == nullptr ? idl::WirePrimitiveKind::Bytes : wire->kind;
      const std::uint32_t fixed_size =
          wire == nullptr
              ? 0u
              : (wire->fixedSize != 0u
                     ? wire->fixedSize
                     : (kind == idl::WirePrimitiveKind::FixedBytes &&
                                type != nullptr
                            ? type->fixedSize
                            : 0u));
      output
          << "    {\n"
          << "        .model_type_id = " << model_layout.model->typeId.value
          << "u,\n"
          << "        .field_tag = " << field->fieldId.value << "u,\n"
          << "        .semantic_type_id = " << field->type.semanticTypeId.value
          << "u,\n"
          << "        .wire_type_id = "
          << (wire == nullptr ? 0u : wire->wireTypeId.value) << "u,\n"
          << "        .fixed_size = " << fixed_size << "u,\n"
          << "        .scale = " << (type == nullptr ? 0u : type->scale)
          << "u,\n"
          << "        .name = " << StringLiteral(field->name) << ",\n"
          << "        .type_kind = schema_cpp::semantic_type_kind::"
          << GeneratedSemanticTypeKindName(field->type.kind) << ",\n"
          << "        .value_representation = "
             "schema_cpp::value_representation_kind::"
          << GeneratedValueRepresentationKindName(
                 field->type.kind, kind,
                 storage == nullptr ? idl::StorageCodecKind::SqliteInteger
                                    : storage->kind)
          << ",\n"
          << "        .wire_type = schema_cpp::wire_type_kind::"
          << GeneratedWireTypeKindName(kind) << ",\n"
          << "        .byte_order = schema_cpp::wire_byte_order::"
          << GeneratedWireByteOrderName(
                 wire == nullptr ? idl::WireByteOrder::None : wire->byteOrder)
          << ",\n"
          << "        .compatibility = schema_cpp::wire_compatibility_class::"
          << GeneratedWireCompatibilityClassName(
                 idl::WireCompatibilityClassOf(kind))
          << ",\n"
          << "        .cardinality = schema_cpp::field_cardinality::"
          << GeneratedFieldCardinalityName(field->cardinality) << ",\n"
          << "        .duplicate_policy = schema_cpp::wire_duplicate_policy::"
          << (field->cardinality == idl::FieldCardinality::Repeated ? "append"
                                                                    : "reject")
          << ",\n"
          << "        .serialization = schema_cpp::serialization_policy::"
          << GeneratedSerializationPolicyName(field->serialization) << ",\n"
          << "        .maximum_items = " << field->maximumItems.value_or(0u)
          << "u,\n"
          << "        .enum_values = "
             "schema_cpp::descriptor_span(field_enum_values_"
          << field_ordinal << "),\n"
          << "    },\n";
      ++field_ordinal;
    }
  }
  output << "}};\n\n";

  output
      << "inline constexpr ::std::array<schema_cpp::wire_message_descriptor, "
      << layout.size() << "u> all_wire_messages{{\n";
  for (std::size_t ordinal = 0; ordinal < layout.size(); ++ordinal) {
    const auto &model_layout = layout[ordinal];
    output << "    {\n"
           << "        .model_type_id = " << model_layout.model->typeId.value
           << "u,\n"
           << "        .name = " << StringLiteral(model_layout.model->name)
           << ",\n"
           << "        .qualified_name = "
           << StringLiteral(model_layout.model->qualifiedName) << ",\n"
           << "        .fields = schema_cpp::descriptor_slice(all_wire_fields, "
           << model_layout.fieldOffset << "u, " << model_layout.fields.size()
           << "u),\n"
           << "        .one_ofs = schema_cpp::descriptor_span(model_one_ofs_"
           << ordinal << "),\n"
           << "        .field_tag_index = "
              "schema_cpp::descriptor_span(field_id_index_"
           << ordinal << "),\n"
           << "    },\n";
  }
  output << "}};\n\n";
}

std::string ModelReferenceExpression(
    const std::optional<idl::SemanticModelReference> &reference) {
  if (!reference.has_value())
    return "{}";
  return "{.model_type_id = " + std::to_string(reference->typeId.value) +
         "u, .qualified_name = " + StringLiteral(reference->canonicalName) +
         "}";
}

std::string
ModelReferenceExpression(const idl::SemanticModelReference &reference) {
  return "{.model_type_id = " + std::to_string(reference.typeId.value) +
         "u, .qualified_name = " + StringLiteral(reference.canonicalName) + "}";
}

void EmitFabricDescriptor(
    std::ostringstream &output, std::string_view indent,
    const std::optional<idl::FabricOperationProfile> &fabric) {
  output << indent << ".fabric = {\n"
         << indent << "    .lane = schema_cpp::fabric_lane::"
         << GeneratedFabricLaneName(fabric.has_value()
                                        ? fabric->lane
                                        : idl::FabricLane::ControlReliable)
         << ",\n"
         << indent << "    .maximum_encoded_bytes = "
         << (fabric.has_value() ? fabric->maximumEncodedBytes : 0u) << "u,\n"
         << indent
         << "    .enabled = " << (fabric.has_value() ? "true" : "false")
         << ",\n"
         << indent << "    .direct_eligible = "
         << (fabric.has_value() && fabric->directEligible ? "true" : "false")
         << ",\n"
         << indent << "},\n";
}

void EmitOperationDescriptorTables(std::ostringstream &output,
                                   const idl::SemanticIr &ir,
                                   const idl::SemanticSource &source) {
  const auto commands = SortedDefinitions(ir.commands, source.handle,
                                          &idl::SemanticCommand::commandId);
  const auto events =
      SortedDefinitions(ir.events, source.handle, &idl::SemanticEvent::eventId);

  for (std::size_t ordinal = 0; ordinal < commands.size(); ++ordinal) {
    auto errors = commands[ordinal]->errors;
    std::sort(errors.begin(), errors.end(),
              [](const auto &lhs, const auto &rhs) {
                return std::tie(lhs.typeId.value, lhs.canonicalName) <
                       std::tie(rhs.typeId.value, rhs.canonicalName);
              });
    output << "inline constexpr "
              "::std::array<schema_cpp::model_reference_descriptor, "
           << errors.size() << "u> command_errors_" << ordinal << "{{\n";
    for (const auto &error : errors)
      output << "    " << ModelReferenceExpression(error) << ",\n";
    output << "}};\n";
  }
  if (!commands.empty())
    output << '\n';
  output << "inline constexpr ::std::array<schema_cpp::command_descriptor, "
         << commands.size() << "u> all_commands{{\n";
  for (std::size_t ordinal = 0; ordinal < commands.size(); ++ordinal) {
    const auto &command = *commands[ordinal];
    output << "    {\n"
           << "        .command_id = " << command.commandId.value << "u,\n"
           << "        .name = " << StringLiteral(command.name) << ",\n"
           << "        .qualified_name = "
           << StringLiteral(command.qualifiedName) << ",\n"
           << "        .owner_capsule = " << StringLiteral(command.ownerCapsule)
           << ",\n"
           << "        .request = " << ModelReferenceExpression(command.request)
           << ",\n"
           << "        .response = "
           << ModelReferenceExpression(command.response) << ",\n"
           << "        .errors = schema_cpp::descriptor_span(command_errors_"
           << ordinal << "),\n"
           << "        .targets = "
           << OperationTargetMaskExpression(command.targets) << ",\n";
    EmitFabricDescriptor(output, "        ", command.fabric);
    output << "        .idempotency = schema_cpp::command_idempotency::"
           << GeneratedCommandIdempotencyName(command.idempotency) << ",\n"
           << "        .streaming = schema_cpp::command_streaming::"
           << GeneratedCommandStreamingName(command.streaming) << ",\n"
           << "        .has_request = "
           << (command.request.has_value() ? "true" : "false") << ",\n"
           << "        .has_response = "
           << (command.response.has_value() ? "true" : "false") << ",\n"
           << "    },\n";
  }
  output << "}};\n\n";
  output << "inline constexpr ::std::array<schema_cpp::event_descriptor, "
         << events.size() << "u> all_events{{\n";
  for (const auto *event : events) {
    output << "    {\n"
           << "        .event_id = " << event->eventId.value << "u,\n"
           << "        .name = " << StringLiteral(event->name) << ",\n"
           << "        .qualified_name = "
           << StringLiteral(event->qualifiedName) << ",\n"
           << "        .owner_capsule = " << StringLiteral(event->ownerCapsule)
           << ",\n"
           << "        .payload = " << ModelReferenceExpression(event->payload)
           << ",\n"
           << "        .targets = "
           << OperationTargetMaskExpression(event->targets) << ",\n";
    EmitFabricDescriptor(output, "        ", event->fabric);
    output << "        .delivery = schema_cpp::event_delivery::"
           << GeneratedEventDeliveryName(event->delivery) << ",\n"
           << "        .ordering = schema_cpp::event_ordering::"
           << GeneratedEventOrderingName(event->ordering) << ",\n"
           << "        .replay = schema_cpp::event_replay::"
           << GeneratedEventReplayName(event->replay) << ",\n"
           << "    },\n";
  }
  output << "}};\n\n";

  output << "inline constexpr ::std::array<schema_cpp::numeric_index_entry, "
         << commands.size() << "u> command_id_index{{\n";
  for (std::size_t ordinal = 0; ordinal < commands.size(); ++ordinal) {
    output << "    {.key = " << commands[ordinal]->commandId.value
           << "u, .ordinal = " << ordinal << "u},\n";
  }
  output << "}};\n";
  std::vector<std::pair<std::string_view, std::size_t>> command_names;
  for (std::size_t ordinal = 0; ordinal < commands.size(); ++ordinal)
    command_names.emplace_back(commands[ordinal]->name, ordinal);
  std::sort(command_names.begin(), command_names.end());
  output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
         << command_names.size() << "u> command_name_index{{\n";
  for (const auto &[name, ordinal] : command_names)
    output << "    {.key = " << StringLiteral(name)
           << ", .ordinal = " << ordinal << "u},\n";
  output << "}};\n";

  output << "inline constexpr ::std::array<schema_cpp::numeric_index_entry, "
         << events.size() << "u> event_id_index{{\n";
  for (std::size_t ordinal = 0; ordinal < events.size(); ++ordinal) {
    output << "    {.key = " << events[ordinal]->eventId.value
           << "u, .ordinal = " << ordinal << "u},\n";
  }
  output << "}};\n";
  std::vector<std::pair<std::string_view, std::size_t>> event_names;
  for (std::size_t ordinal = 0; ordinal < events.size(); ++ordinal)
    event_names.emplace_back(events[ordinal]->name, ordinal);
  std::sort(event_names.begin(), event_names.end());
  output << "inline constexpr ::std::array<schema_cpp::name_index_entry, "
         << event_names.size() << "u> event_name_index{{\n";
  for (const auto &[name, ordinal] : event_names)
    output << "    {.key = " << StringLiteral(name)
           << ", .ordinal = " << ordinal << "u},\n";
  output << "}};\n\n";
}

void EmitSchemaCatalog(CppGenerationResult &result, std::ostringstream &output,
                       const idl::SemanticManifest &manifest,
                       const idl::SemanticSource &source,
                       std::string_view allocation_fingerprint) {
  const auto layout = BuildDescriptorLayout(manifest.ir, source);
  output << "namespace schema_descriptor_data\n{\n";
  const auto fragment_count = manifest.ir.catalog.has_value()
                                  ? manifest.ir.catalog->fragments.size()
                                  : 0u;
  output << "inline constexpr "
            "::std::array<schema_cpp::catalog_fragment_descriptor, "
         << fragment_count << "u> catalog_fragments{{\n";
  if (manifest.ir.catalog.has_value()) {
    for (const auto &fragment : manifest.ir.catalog->fragments) {
      output << "    {\n"
             << "        .fragment_id = " << StringLiteral(fragment.fragmentId)
             << ",\n"
             << "        .owner_capsule = "
             << StringLiteral(fragment.ownerCapsule) << ",\n"
             << "        .version = " << fragment.version << "u,\n"
             << "        .declaration_order = " << fragment.declarationOrder
             << "u,\n"
             << "    },\n";
    }
  }
  output << "}};\n\n";
  EmitFieldContractTables(output, manifest.ir, layout);
  EmitFieldDescriptorTable(result, output, manifest.ir, layout);
  EmitRelationDescriptorTable(output, layout);
  EmitModelContractTables(output, manifest.ir, layout);
  EmitDescriptorIndexes(output, layout);
  EmitModelDescriptorTable(output, layout);
  EmitWireDescriptorTables(result, output, manifest.ir, layout);
  EmitOperationDescriptorTables(output, manifest.ir, source);
  output << "}\n\n";
  output
      << "inline constexpr schema_cpp::schema_catalog_view schema_catalog{\n"
      << "    .idl_format_version = " << manifest.ir.formatVersion << "u,\n"
      << "    .edition = " << source.edition << "u,\n"
      << "    .schema_version = " << source.schemaVersion << "u,\n"
      << "    .source_path = " << StringLiteral(source.path) << ",\n"
      << "    .schema_name = " << StringLiteral(source.schemaName) << ",\n"
      << "    .owner_capsule = " << StringLiteral(source.ownerCapsule) << ",\n"
      << "    .semantic_fingerprint = "
      << StringLiteral(manifest.semanticFingerprint) << ",\n"
      << "    .allocation_fingerprint = "
      << StringLiteral(allocation_fingerprint) << ",\n"
      << "    .fields = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_fields),\n"
      << "    .relations = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_relations),\n"
      << "    .models = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_models),\n"
      << "    .wire_fields = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_wire_fields),"
         "\n"
      << "    .wire_messages = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_wire_"
         "messages),\n"
      << "    .commands = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_commands),\n"
      << "    .events = "
         "schema_cpp::descriptor_span(schema_descriptor_data::all_events),\n"
      << "    .model_id_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::model_id_index),"
         "\n"
      << "    .model_name_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::model_name_index)"
         ",\n"
      << "    .wire_message_id_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::model_id_index),"
         "\n"
      << "    .command_id_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::command_id_index)"
         ",\n"
      << "    .command_name_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::command_name_"
         "index),\n"
      << "    .event_id_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::event_id_index),"
         "\n"
      << "    .event_name_index = "
         "schema_cpp::descriptor_span(schema_descriptor_data::event_name_index)"
         ",\n"
      << "    .catalog_id = "
      << StringLiteral(manifest.ir.catalog.has_value()
                           ? manifest.ir.catalog->catalogId
                           : std::string{})
      << ",\n"
      << "    .catalog_version = "
      << (manifest.ir.catalog.has_value() ? manifest.ir.catalog->version : 0u)
      << "u,\n"
      << "    .catalog_fragments = "
         "schema_cpp::descriptor_span(schema_descriptor_data::catalog_"
         "fragments),\n"
      << "};\n\n";
}

void EmitOperationTokens(CppGenerationResult &result,
                         std::ostringstream &output, const idl::SemanticIr &ir,
                         const idl::SemanticSource &source,
                         const std::vector<SourceGenerationInfo> &sources) {
  output << "namespace commands\n{\n";
  for (const auto *command : SortedDefinitions(
           ir.commands, source.handle, &idl::SemanticCommand::commandId)) {
    const std::string request_type =
        command->request.has_value()
            ? QualifiedModelName(result, ir, sources, *command->request,
                                 command->provenance)
            : "schema_cpp::no_payload";
    const std::string response_type =
        command->response.has_value()
            ? QualifiedModelName(result, ir, sources, *command->response,
                                 command->provenance)
            : "schema_cpp::no_payload";
    output << "using " << command->name << "_token = schema_cpp::command_token<"
           << request_type << ", " << response_type << ", "
           << command->commandId.value << "u>;\n"
           << "inline constexpr " << command->name << "_token " << command->name
           << "{schema_catalog.contract(), schema_catalog.find_command("
           << command->commandId.value << "u)};\n";
  }
  output << "}\n\nnamespace events\n{\n";
  for (const auto *event : SortedDefinitions(ir.events, source.handle,
                                             &idl::SemanticEvent::eventId)) {
    const std::string payload_type = QualifiedModelName(
        result, ir, sources, event->payload, event->provenance);
    output << "using " << event->name << "_token = schema_cpp::event_token<"
           << payload_type << ", " << event->eventId.value << "u>;\n"
           << "inline constexpr " << event->name << "_token " << event->name
           << "{schema_catalog.contract(), schema_catalog.find_event("
           << event->eventId.value << "u)};\n";
  }
  output << "}\n\nnamespace operations\n{\n";
  for (const auto *command : SortedDefinitions(
           ir.commands, source.handle, &idl::SemanticCommand::commandId)) {
    const std::string request_type =
        command->request.has_value()
            ? QualifiedModelName(result, ir, sources, *command->request,
                                 command->provenance)
            : "schema_cpp::no_payload";
    const std::string response_type =
        command->response.has_value()
            ? QualifiedModelName(result, ir, sources, *command->response,
                                 command->provenance)
            : "schema_cpp::no_payload";
    output << "struct " << command->name << "\n{\n"
           << "    using token_type = commands::" << command->name
           << "_token;\n"
           << "    using request_type = " << request_type << ";\n"
           << "    using response_type = " << response_type << ";\n"
           << "    static inline constexpr const token_type& token = commands::"
           << command->name << ";\n"
           << "    static constexpr ::std::uint32_t command_id = "
           << command->commandId.value << "u;\n"
           << "    static constexpr ::std::uint32_t maximum_encoded_bytes = "
           << (command->fabric.has_value()
                   ? command->fabric->maximumEncodedBytes
                   : 0u)
           << "u;\n"
           << "    static constexpr bool fabric_enabled = "
           << (command->fabric.has_value() ? "true" : "false") << ";\n"
           << "    static constexpr bool direct_eligible = "
           << (command->fabric.has_value() && command->fabric->directEligible
                   ? "true"
                   : "false")
           << ";\n\n"
           << "    [[nodiscard]] static constexpr bool "
              "accepts_encoded_payload(::std::size_t size) noexcept\n"
           << "    {\n"
           << "        return fabric_enabled && maximum_encoded_bytes > 0u && "
              "size <= maximum_encoded_bytes;\n"
           << "    }\n";
    if (command->request.has_value()) {
      const std::string binding =
          QualifiedModelName(result, ir, sources, *command->request,
                             command->provenance) +
          "_binding";
      output << "\n    [[nodiscard]] static auto encode_request(\n"
             << "        const request_type& value,\n"
             << "        ::std::span<const schema_cpp::wire_unknown_field> "
                "unknown_fields = {},\n"
             << "        const schema_cpp::wire_encode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << binding
             << "::encode_wire(value, unknown_fields, options);\n"
             << "    }\n\n"
             << "    [[nodiscard]] static auto decode_request(\n"
             << "        ::std::span<const ::std::byte> bytes,\n"
             << "        const schema_cpp::wire_decode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << binding
             << "::decode_wire(bytes, options);\n"
             << "    }\n";
    }
    if (command->response.has_value()) {
      const std::string binding =
          QualifiedModelName(result, ir, sources, *command->response,
                             command->provenance) +
          "_binding";
      output << "\n    [[nodiscard]] static auto encode_response(\n"
             << "        const response_type& value,\n"
             << "        ::std::span<const schema_cpp::wire_unknown_field> "
                "unknown_fields = {},\n"
             << "        const schema_cpp::wire_encode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << binding
             << "::encode_wire(value, unknown_fields, options);\n"
             << "    }\n\n"
             << "    [[nodiscard]] static auto decode_response(\n"
             << "        ::std::span<const ::std::byte> bytes,\n"
             << "        const schema_cpp::wire_decode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << binding
             << "::decode_wire(bytes, options);\n"
             << "    }\n";
    }
    for (std::size_t error_index = 0; error_index < command->errors.size();
         ++error_index) {
      const auto &error = command->errors[error_index];
      const std::string error_type =
          QualifiedModelName(result, ir, sources, error, command->provenance);
      output << "\n    using error_" << error_index << "_type = " << error_type
             << ";\n"
             << "    [[nodiscard]] static auto encode_error_" << error_index
             << "(\n"
             << "        const error_" << error_index << "_type& value,\n"
             << "        ::std::span<const schema_cpp::wire_unknown_field> "
                "unknown_fields = {},\n"
             << "        const schema_cpp::wire_encode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << error_type
             << "_binding::encode_wire(value, unknown_fields, options);\n"
             << "    }\n\n"
             << "    [[nodiscard]] static auto decode_error_" << error_index
             << "(\n"
             << "        ::std::span<const ::std::byte> bytes,\n"
             << "        const schema_cpp::wire_decode_options& options = {}\n"
             << "    )\n"
             << "    {\n"
             << "        return " << error_type
             << "_binding::decode_wire(bytes, options);\n"
             << "    }\n";
    }
    output << "};\n\n";
  }
  output << "}\n\nnamespace event_operations\n{\n";
  for (const auto *event : SortedDefinitions(ir.events, source.handle,
                                             &idl::SemanticEvent::eventId)) {
    const std::string payload_type = QualifiedModelName(
        result, ir, sources, event->payload, event->provenance);
    output << "struct " << event->name << "\n{\n"
           << "    using token_type = events::" << event->name << "_token;\n"
           << "    using payload_type = " << payload_type << ";\n"
           << "    static inline constexpr const token_type& token = events::"
           << event->name << ";\n"
           << "    static constexpr ::std::uint32_t event_id = "
           << event->eventId.value << "u;\n"
           << "    static constexpr ::std::uint32_t maximum_encoded_bytes = "
           << (event->fabric.has_value() ? event->fabric->maximumEncodedBytes
                                         : 0u)
           << "u;\n"
           << "    static constexpr bool fabric_enabled = "
           << (event->fabric.has_value() ? "true" : "false") << ";\n"
           << "    static constexpr bool direct_eligible = "
           << (event->fabric.has_value() && event->fabric->directEligible
                   ? "true"
                   : "false")
           << ";\n\n"
           << "    [[nodiscard]] static constexpr bool "
              "accepts_encoded_payload(::std::size_t size) noexcept\n"
           << "    {\n"
           << "        return fabric_enabled && maximum_encoded_bytes > 0u && "
              "size <= maximum_encoded_bytes;\n"
           << "    }\n\n"
           << "    [[nodiscard]] static auto encode_payload(\n"
           << "        const payload_type& value,\n"
           << "        ::std::span<const schema_cpp::wire_unknown_field> "
              "unknown_fields = {},\n"
           << "        const schema_cpp::wire_encode_options& options = {}\n"
           << "    )\n"
           << "    {\n"
           << "        return " << payload_type
           << "_binding::encode_wire(value, unknown_fields, options);\n"
           << "    }\n\n"
           << "    [[nodiscard]] static auto decode_payload(\n"
           << "        ::std::span<const ::std::byte> bytes,\n"
           << "        const schema_cpp::wire_decode_options& options = {}\n"
           << "    )\n"
           << "    {\n"
           << "        return " << payload_type
           << "_binding::decode_wire(bytes, options);\n"
           << "    }\n"
           << "};\n\n";
  }
  output << "}\n\n";
}

std::vector<const idl::SemanticField *>
PrimaryKeyFields(CppGenerationResult &result, const idl::SemanticModel &model) {
  const auto key = std::find_if(
      model.keys.begin(), model.keys.end(), [](const auto &candidate) {
        return candidate.kind == idl::KeyKind::Primary;
      });
  if (key == model.keys.end())
    return {};

  std::vector<const idl::SemanticField *> fields;
  fields.reserve(key->fields.size());
  for (const auto &reference : key->fields) {
    const auto *field = FindField(model, reference);
    if (field == nullptr) {
      AddDiagnostic(
          result, "schema.cpp.primary_key_field_unresolved",
          "A generated primary-key binding references an unresolved field.",
          key->provenance);
      continue;
    }
    fields.push_back(field);
  }
  return fields;
}

std::string
QualifiedEnumValidator(CppGenerationResult &result, const idl::SemanticIr &ir,
                       const std::vector<SourceGenerationInfo> &sources,
                       idl::SourceHandle current_source,
                       const idl::SemanticField &field) {
  const auto *type = FindType(ir, field.type.semanticType);
  if (type == nullptr || !type->definingEnum.has_value()) {
    AddDiagnostic(
        result, "schema.cpp.enum_validator_unresolved",
        "A generated enum field does not resolve to its domain validator.",
        field.provenance);
    return {};
  }
  const auto *enumeration = FindEnum(ir, *type->definingEnum);
  if (enumeration == nullptr || !enumeration->source.valid() ||
      enumeration->source.value >= sources.size() ||
      sources[enumeration->source.value].source == nullptr) {
    AddDiagnostic(
        result, "schema.cpp.enum_validator_unresolved",
        "A generated enum field references an unresolved enum source.",
        field.provenance);
    return {};
  }
  if (enumeration->source == current_source)
    return "is_valid_" + enumeration->name;
  return "::" + sources[enumeration->source.value].namespaceName +
         "::is_valid_" + enumeration->name;
}

void EmitValidationIssue(std::ostringstream &output, std::string_view indent,
                         const idl::SemanticModel &model,
                         const idl::SemanticField &field,
                         std::string_view code_expression,
                         std::string_view message_expression) {
  output << indent << "result.issues.push_back({\n"
         << indent << "    .model_type_id = " << model.typeId.value << "u,\n"
         << indent << "    .field_id = " << field.fieldId.value << "u,\n"
         << indent << "    .code = " << code_expression << ",\n"
         << indent << "    .message = " << message_expression << ",\n"
         << indent << "});\n";
}

void EmitValueValidation(CppGenerationResult &result,
                         std::ostringstream &output, const idl::SemanticIr &ir,
                         const idl::SemanticSource &source,
                         const idl::SemanticModel &model,
                         const idl::SemanticField &field,
                         const std::vector<SourceGenerationInfo> &sources,
                         std::string_view value_expression,
                         std::string_view indent) {
  const std::string cpp_name(CppFieldIdentifier(field));
  const auto *type = FindType(ir, field.type.semanticType);
  if (type == nullptr) {
    AddDiagnostic(
        result, "schema.cpp.binding_type_unresolved",
        "A generated value validator references an unresolved semantic type.",
        field.provenance);
    return;
  }
  output << indent << "{\n"
         << indent
         << "    const auto encoded_value = schema_cpp::encode_storage_value("
         << value_expression << ");\n"
         << indent
         << "    const auto semantic_failure = "
            "schema_cpp::validate_semantic_storage_value(\n"
         << indent << "        encoded_value,\n"
         << indent << "        schema_cpp::semantic_type_kind::"
         << GeneratedSemanticTypeKindName(type->kind) << ",\n"
         << indent << "        schema_cpp::normalization_kind::"
         << GeneratedNormalizationKindName(field.representation.normalization)
         << ",\n"
         << indent << "        " << type->fixedSize << "u,\n"
         << indent << "        " << type->precision << "u,\n"
         << indent << "        " << type->scale << "u\n"
         << indent << "    );\n"
         << indent << "    if (!semantic_failure.ok())\n"
         << indent << "    {\n";
  EmitValidationIssue(output, std::string(indent) + "        ", model, field,
                      "semantic_failure.code", "semantic_failure.message");
  output << indent << "    }\n";

  for (const auto &check : field.checks) {
    if (check.kind == idl::CheckKind::EnumDomain) {
      const std::string validator =
          QualifiedEnumValidator(result, ir, sources, source.handle, field);
      if (validator.empty())
        continue;
      output << indent << "    if (!" << validator << '(' << value_expression
             << "))\n"
             << indent << "    {\n";
      const std::string message =
          check.message.empty() ? "Enum value is outside the generated domain."
                                : check.message;
      EmitValidationIssue(output, std::string(indent) + "        ", model,
                          field, StringLiteral("field." + check.name),
                          StringLiteral(message));
      output << indent << "    }\n";
      if (!field.allowedEnumValueIds.empty()) {
        output << indent << "    if (";
        for (std::size_t index = 0; index < field.allowedEnumValueIds.size();
             ++index) {
          if (index != 0)
            output << " && ";
          output << "static_cast<::std::uint32_t>(" << value_expression
                 << ") != " << field.allowedEnumValueIds[index].value << "u";
        }
        output << ")\n" << indent << "    {\n";
        EmitValidationIssue(
            output, std::string(indent) + "        ", model, field,
            StringLiteral("field.enum_narrowing"),
            StringLiteral(
                "Enum value is outside the field-specific governed domain."));
        output << indent << "    }\n";
      }
      continue;
    }
    if (check.generated)
      continue;

    output << indent << "    const auto " << cpp_name << '_' << check.name
           << "_failure = schema_cpp::validate_storage_check(\n"
           << indent << "        encoded_value,\n"
           << indent << "        schema_cpp::check_kind::"
           << GeneratedCheckKindName(check.kind) << ",\n"
           << indent << "        schema_cpp::schema_field_kind::"
           << GeneratedFieldKindName(field.representation.fieldKind) << ",\n"
           << indent << "        schema_cpp::check_unit::"
           << GeneratedCheckUnitName(check.unit) << ",\n"
           << indent << "        " << StringLiteral(check.value) << ",\n"
           << indent << "        " << StringLiteral(check.message) << "\n"
           << indent << "    );\n"
           << indent << "    if (!" << cpp_name << '_' << check.name
           << "_failure.ok())\n"
           << indent << "    {\n";
    EmitValidationIssue(output, std::string(indent) + "        ", model, field,
                        cpp_name + '_' + check.name + "_failure.code",
                        cpp_name + '_' + check.name + "_failure.message");
    output << indent << "    }\n";
  }
  output << indent << "}\n";
}

void EmitBindingField(std::ostringstream &output, std::string_view indent,
                      const idl::SemanticField &field,
                      std::string_view value_expression) {
  output << indent << "result.record.push_back({\n"
         << indent << "    .field_id = " << field.fieldId.value << "u,\n"
         << indent << "    .name = " << StringLiteral(field.name) << ",\n"
         << indent << "    .value = " << value_expression << ",\n"
         << indent << "});\n";
}

void EmitModelValidator(CppGenerationResult &result, std::ostringstream &output,
                        const idl::SemanticIr &ir,
                        const idl::SemanticSource &source,
                        const idl::SemanticModel &model,
                        const std::vector<SourceGenerationInfo> &sources,
                        bool standalone = true) {
  if (standalone) output << "struct " << model.name << "_validator\n{\n"
         << "    [[nodiscard]] static " << model.name
         << "_validation_result validate(const " << model.name << "& value)\n"
         << "    {\n";
  output << "        " << model.name << "_validation_result result;\n";
  for (const auto *field : SortedFields(model)) {
    const std::string cpp_name(CppFieldIdentifier(*field));
    if (field->cardinality == idl::FieldCardinality::Repeated) {
      if (field->minimum.has_value()) {
        output << "        if (value." << cpp_name << ".size() < "
               << *field->minimum << "u)\n        {\n";
        EmitValidationIssue(output, "            ", model, *field,
                            StringLiteral("field.minimum_items"),
                            StringLiteral("Repeated field has fewer than the "
                                          "governed minimum number of items."));
        output << "        }\n";
      }
      if (field->maximumItems.has_value()) {
        output << "        if (value." << cpp_name << ".size() > "
               << *field->maximumItems << "u)\n        {\n";
        EmitValidationIssue(output, "            ", model, *field,
                            StringLiteral("field.maximum_items"),
                            StringLiteral("Repeated field exceeds the governed "
                                          "maximum number of items."));
        output << "        }\n";
      }
      output << "        for (const auto& item : value." << cpp_name
             << ")\n        {\n";
      EmitValueValidation(result, output, ir, source, model, *field, sources,
                          "item", "            ");
      output << "        }\n";
    } else if (field->required) {
      EmitValueValidation(result, output, ir, source, model, *field, sources,
                          "value." + cpp_name, "        ");
    } else {
      output << "        if (value." << cpp_name
             << ".has_value())\n        {\n";
      EmitValueValidation(result, output, ir, source, model, *field, sources,
                          "*value." + cpp_name, "            ");
      output << "        }\n";
    }
  }
  output << "        return result;\n";
  if (standalone) output << "    }\n};\n\n";
}

void EmitModelBinding(CppGenerationResult &result, std::ostringstream &output,
                      const idl::SemanticIr &ir,
                      const idl::SemanticSource &source,
                      const idl::SemanticModel &model,
                      const std::vector<SourceGenerationInfo> &sources,
                      bool separate_validation) {
  const auto fields = SortedFields(model);
  const auto primary_fields = PrimaryKeyFields(result, model);
  std::vector<const idl::SemanticField *> mutable_fields;
  for (const auto *field : fields)
    if (!field->primaryKey && !field->immutable)
      mutable_fields.push_back(field);

  output << "using " << model.name
         << "_binding_result = schema_cpp::binding_result<"
         << model.typeId.value << "u>;\n"
         << "using " << model.name
         << "_decode_result = schema_cpp::decode_result<" << model.name << ", "
         << model.typeId.value << "u>;\n"
         << "using " << model.name
         << "_wire_decode_result = schema_cpp::wire_model_decode_result<"
         << model.name << ">;\n\n"
         << "struct " << model.name << "_binding\n{\n"
         << "    using model_type = " << model.name << ";\n"
         << "    using primary_key_type = " << model.name << "_primary_key;\n"
         << "    using changes_type = " << model.name << "_changes;\n"
         << "    using binding_result_type = " << model.name
         << "_binding_result;\n"
         << "    using decode_result_type = " << model.name
         << "_decode_result;\n\n"
         << "    static constexpr ::std::uint32_t model_type_id = "
         << model.typeId.value << "u;\n"
         << "    static constexpr ::std::string_view model_name = "
         << StringLiteral(model.name) << ";\n\n"
         << "    [[nodiscard]] static constexpr ::std::span<const "
            "schema_cpp::field_descriptor> layout() noexcept\n"
         << "    {\n"
         << "        const auto* descriptor = "
            "schema_catalog.find_model(model_type_id);\n"
         << "        return descriptor == nullptr ? ::std::span<const "
            "schema_cpp::field_descriptor>{} : descriptor->fields;\n"
         << "    }\n\n"
         << "    [[nodiscard]] static constexpr const "
            "schema_cpp::wire_message_descriptor* wire_layout() noexcept\n"
         << "    {\n"
         << "        return schema_catalog.find_wire_message(model_type_id);\n"
         << "    }\n\n"
         << "    [[nodiscard]] static " << model.name
         << "_validation_result validate(const model_type& value)\n"
         << "    {\n";
  if (separate_validation)
    output << "        return " << model.name << "_validator::validate(value);\n";
  else EmitModelValidator(result, output, ir, source, model, sources, false);
  output << "    }\n\n"
         << "    [[nodiscard]] static binding_result_type bind_row(const "
            "model_type& value)\n"
         << "    {\n"
         << "        binding_result_type result;\n"
         << "        auto validation = validate(value);\n"
         << "        result.issues = ::std::move(validation.issues);\n"
         << "        if (!result.ok()) return result;\n"
         << "        result.record.reserve(" << fields.size() << "u);\n";
  for (const auto *field : fields) {
    const std::string cpp_name(CppFieldIdentifier(*field));
    if (field->cardinality == idl::FieldCardinality::Repeated) {
      output << "        for (const auto& item : value." << cpp_name
             << ")\n        {\n";
      EmitBindingField(output, "            ", *field,
                       "schema_cpp::encode_storage_value(item)");
      output << "        }\n";
    } else if (field->required) {
      EmitBindingField(output, "        ", *field,
                       "schema_cpp::encode_storage_value(value." + cpp_name +
                           ')');
    } else {
      output << "        if (value." << cpp_name
             << ".has_value())\n        {\n";
      EmitBindingField(output, "            ", *field,
                       "schema_cpp::encode_storage_value(*value." + cpp_name +
                           ')');
      output << "        }\n        else\n        {\n";
      EmitBindingField(output, "            ", *field, "::std::monostate{}");
      output << "        }\n";
    }
  }
  output
      << "        return result;\n"
      << "    }\n\n"
      << "    [[nodiscard]] static schema_cpp::wire_encode_result "
         "encode_wire(\n"
      << "        const model_type& value,\n"
      << "        ::std::span<const schema_cpp::wire_unknown_field> "
         "unknown_fields = {},\n"
      << "        const schema_cpp::wire_encode_options& options = {}\n"
      << "    )\n"
      << "    {\n"
      << "        const auto* descriptor = wire_layout();\n"
      << "        if (descriptor == nullptr)\n"
      << "        {\n"
      << "            schema_cpp::wire_encode_result result;\n"
      << "            result.issues.push_back({\n"
      << "                .model_type_id = model_type_id,\n"
      << "                .code = \"wire.message_descriptor_missing\",\n"
      << "                .message = \"Generated wire message descriptor is "
         "missing.\",\n"
      << "            });\n"
      << "            return result;\n"
      << "        }\n"
      << "        const auto bound = bind_row(value);\n"
      << "        if (!bound.ok())\n"
      << "        {\n"
      << "            schema_cpp::wire_encode_result result;\n"
      << "            schema_cpp::append_wire_validation_issues(result.issues, "
         "bound.issues);\n"
      << "            return result;\n"
      << "        }\n"
      << "        return schema_cpp::encode_wire_record(*descriptor, "
         "bound.record, unknown_fields, options);\n"
      << "    }\n\n"
      << "    [[nodiscard]] static " << model.name
      << "_wire_decode_result decode_wire(\n"
      << "        ::std::span<const ::std::byte> bytes,\n"
      << "        const schema_cpp::wire_decode_options& options = {}\n"
      << "    )\n"
      << "    {\n"
      << "        " << model.name << "_wire_decode_result result;\n"
      << "        const auto* descriptor = wire_layout();\n"
      << "        if (descriptor == nullptr)\n"
      << "        {\n"
      << "            result.issues.push_back({\n"
      << "                .model_type_id = model_type_id,\n"
      << "                .code = \"wire.message_descriptor_missing\",\n"
      << "                .message = \"Generated wire message descriptor is "
         "missing.\",\n"
      << "            });\n"
      << "            return result;\n"
      << "        }\n"
      << "        auto decoded_record = "
         "schema_cpp::decode_wire_record(*descriptor, bytes, options);\n"
      << "        result.unknown_fields = "
         "::std::move(decoded_record.unknown_fields);\n"
      << "        result.issues = ::std::move(decoded_record.issues);\n"
      << "        if (!result.issues.empty()) return result;\n"
      << "        auto decoded = decode(decoded_record.record);\n"
      << "        schema_cpp::append_wire_validation_issues(result.issues, "
         "decoded.issues);\n"
      << "        if (decoded.ok()) result.value = "
         "::std::move(decoded.value);\n"
      << "        return result;\n"
      << "    }\n\n"
      << "    [[nodiscard]] static binding_result_type bind_primary_key(const "
         "primary_key_type& value)\n"
      << "    {\n"
      << "        binding_result_type result;\n"
      << "        result.record.reserve(" << primary_fields.size() << "u);\n";
  for (const auto *field : primary_fields) {
    const std::string cpp_name(CppFieldIdentifier(*field));
    EmitValueValidation(result, output, ir, source, model, *field, sources,
                        "value." + cpp_name, "        ");
    EmitBindingField(output, "        ", *field,
                     "schema_cpp::encode_storage_value(value." + cpp_name +
                         ')');
  }
  output << "        return result;\n"
         << "    }\n\n"
         << "    [[nodiscard]] static binding_result_type bind_changes(const "
            "changes_type& changes)\n"
         << "    {\n"
         << "        binding_result_type result;\n"
         << "        result.record.reserve(" << mutable_fields.size()
         << "u);\n";
  if (mutable_fields.empty())
    output << "        (void)changes;\n";
  for (const auto *field : mutable_fields) {
    const std::string cpp_name(CppFieldIdentifier(*field));
    output << "        if (changes." << cpp_name << ".changed())\n        {\n"
           << "            if (changes." << cpp_name
           << ".state == schema_cpp::field_update_state::null_value)\n"
           << "            {\n";
    if (field->required ||
        field->cardinality == idl::FieldCardinality::Repeated) {
      EmitValidationIssue(
          output, "                ", model, *field,
          StringLiteral("storage.required_field_null"),
          StringLiteral("Required field changes cannot assign null."));
    } else {
      EmitBindingField(output, "                ", *field,
                       "::std::monostate{}");
    }
    output << "            }\n            else\n            {\n";
    if (field->cardinality == idl::FieldCardinality::Repeated) {
      EmitValidationIssue(
          output, "                ", model, *field,
          StringLiteral("storage.repeated_change_requires_adapter"),
          StringLiteral("Repeated field changes require a collection-owned "
                        "storage adapter; use the generated wire model for "
                        "message replacement."));
    } else {
      EmitValueValidation(result, output, ir, source, model, *field, sources,
                          "changes." + cpp_name + ".payload",
                          "                ");
      EmitBindingField(output, "                ", *field,
                       "schema_cpp::encode_storage_value(changes." + cpp_name +
                           ".payload)");
    }
    output << "            }\n        }\n";
  }
  output << "        return result;\n"
         << "    }\n\n"
         << "    [[nodiscard]] static decode_result_type "
            "decode(::std::span<const schema_cpp::binding_field> record)\n"
         << "    {\n"
         << "        decode_result_type result;\n"
         << "        model_type decoded{};\n"
         << "        ::std::array<bool, " << fields.size() << "u> seen{};\n"
         << "        for (const auto& bound : record)\n"
         << "        {\n"
         << "            switch (bound.field_id)\n"
         << "            {\n";
  for (std::size_t index = 0; index < fields.size(); ++index) {
    const auto *field = fields[index];
    const std::string cpp_name(CppFieldIdentifier(*field));
    const std::string type = QualifiedTypeName(
        result, ir, sources, source.handle, field->type, field->provenance);
    output << "                case " << field->fieldId.value << "u:\n"
           << "                {\n";
    if (field->cardinality != idl::FieldCardinality::Repeated) {
      output << "                    if (seen[" << index << "u])\n"
             << "                    {\n";
      EmitValidationIssue(
          output, "                        ", model, *field,
          StringLiteral("storage.field_duplicate"),
          StringLiteral("Stored record contains a duplicate field."));
      output << "                        break;\n"
             << "                    }\n";
    }
    output << "                    seen[" << index << "u] = true;\n";
    if (field->cardinality == idl::FieldCardinality::Repeated) {
      output << "                    if "
                "(::std::holds_alternative<::std::monostate>(bound.value))\n"
             << "                    {\n";
      EmitValidationIssue(
          output, "                        ", model, *field,
          StringLiteral("storage.repeated_field_null"),
          StringLiteral("Repeated stored field items cannot be null."));
      output << "                        break;\n"
             << "                    }\n";
    } else if (!field->required) {
      output << "                    if "
                "(::std::holds_alternative<::std::monostate>(bound.value))\n"
             << "                    {\n"
             << "                        decoded." << cpp_name << ".reset();\n"
             << "                        break;\n"
             << "                    }\n";
    } else {
      output << "                    if "
                "(::std::holds_alternative<::std::monostate>(bound.value))\n"
             << "                    {\n";
      EmitValidationIssue(
          output, "                        ", model, *field,
          StringLiteral("storage.required_field_null"),
          StringLiteral("Required stored field cannot be null."));
      output << "                        break;\n"
             << "                    }\n";
    }
    output << "                    auto field_value = "
              "schema_cpp::decode_storage_value<"
           << type << ">(bound.value);\n"
           << "                    if (!field_value.ok())\n"
           << "                    {\n";
    EmitValidationIssue(output, "                        ", model, *field,
                        "field_value.code", "field_value.message");
    output << "                        break;\n"
           << "                    }\n";
    if (field->cardinality == idl::FieldCardinality::Repeated)
      output << "                    decoded." << cpp_name
             << ".push_back(::std::move(*field_value.value));\n";
    else
      output << "                    decoded." << cpp_name
             << " = ::std::move(*field_value.value);\n";
    output << "                    break;\n"
           << "                }\n";
  }
  output << "                default:\n"
         << "                    result.issues.push_back({\n"
         << "                        .model_type_id = " << model.typeId.value
         << "u,\n"
         << "                        .field_id = bound.field_id,\n"
         << "                        .code = \"storage.field_unknown\",\n"
         << "                        .message = \"Stored record contains an "
            "unknown field.\",\n"
         << "                    });\n"
         << "                    break;\n"
         << "            }\n"
         << "        }\n";
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (!fields[index]->required)
      continue;
    output << "        if (!seen[" << index << "u])\n"
           << "        {\n";
    EmitValidationIssue(
        output, "            ", model, *fields[index],
        StringLiteral("storage.required_field_missing"),
        StringLiteral("Stored record is missing a required field."));
    output << "        }\n";
  }
  output << "        if (result.issues.empty())\n"
         << "        {\n"
         << "            auto validation = validate(decoded);\n"
         << "            result.issues = ::std::move(validation.issues);\n"
         << "        }\n"
         << "        if (result.issues.empty()) result.value = "
            "::std::move(decoded);\n"
         << "        return result;\n"
         << "    }\n"
         << "};\n\n";
}

void EmitModelBindings(CppGenerationResult &result, std::ostringstream &output,
                       const idl::SemanticIr &ir,
                       const idl::SemanticSource &source,
                       const std::vector<SourceGenerationInfo> &sources,
                       bool separate_validation) {
  for (const auto *model : GeneratedModels(ir, source.handle))
    EmitModelBinding(result, output, ir, source, *model, sources, separate_validation);
}

std::string ArtifactPreamble(const idl::SemanticManifest &manifest,
                            std::string_view allocation_fingerprint,
                            const idl::SemanticSource &source) {
  std::ostringstream output;
  output << "// Generated by arken_schema_cpp_v1. Do not edit.\n"
         << "// source: " << CommentText(source.path) << "\n"
         << "// semantic_fingerprint: " << manifest.semanticFingerprint << "\n\n"
         << "// allocation_fingerprint: " << allocation_fingerprint << "\n\n"
         << "#pragma once\n\n";
  return output.str();
}

bool HasEmittedRawLiteral(std::string_view content) {
  char quote = 0;
  bool line_comment = false, block_comment = false;
  for (std::size_t i = 0; i < content.size(); ++i) {
    const auto c = content[i];
    const auto next = i + 1u < content.size() ? content[i + 1u] : '\0';
    if (line_comment) { if (c == '\n') line_comment = false; continue; }
    if (block_comment) {
      if (c == '*' && next == '/') { block_comment = false; ++i; }
      continue;
    }
    if (quote) {
      if (c == '\\' && i + 1u < content.size()) ++i;
      else if (c == quote) quote = 0;
      continue;
    }
    if (c == '/' && next == '/') { line_comment = true; ++i; }
    else if (c == '/' && next == '*') { block_comment = true; ++i; }
    else if (c == 'R' && next == '"') return true;
    else if (c == '"' || c == '\'') quote = c;
  }
  return false;
}

std::string CompactEmittedSource(CppGenerationResult &result,
                                std::string content,
                                const idl::SemanticSource &source) {
  // This is a layout projection of our controlled emitter, not a C++ minifier.
  // StringLiteral emits escaped physical-line literals. A future raw emitter
  // must explicitly extend this contract before indentation can be removed.
  if (HasEmittedRawLiteral(content)) {
    AddDiagnostic(result, "schema.cpp.compact_source_raw_literal",
                  "Compact source does not admit raw multiline literal shapes.",
                  {.sourcePath = source.path});
    return content;
  }
  std::string compact;
  compact.reserve(content.size());
  bool continued = false;
  for (std::size_t begin = 0; begin < content.size();) {
    const auto newline = content.find('\n', begin);
    const auto end = newline == std::string::npos ? content.size() : newline;
    auto first = begin;
    if (!continued)
      while (first < end && (content[first] == ' ' || content[first] == '\t'))
        ++first;
    compact.append(content, first, end - first);
    // Preserve separators after backslash-newline preprocessing continuation.
    continued = end > begin && content[end - 1u] == '\\';
    if (newline != std::string::npos) compact.push_back('\n');
    begin = newline == std::string::npos ? content.size() : newline + 1u;
  }
  return compact;
}

std::vector<GeneratedCppArtifact> GenerateArtifacts(
    CppGenerationResult &result, const idl::SemanticManifest &manifest,
    std::string_view allocation_fingerprint, const idl::SemanticSource &source,
    const SourceGenerationInfo &info,
    const std::vector<SourceGenerationInfo> &sources,
    const std::vector<generator_detail::CppRegistrySourceInfo>
        &registry_sources, bool compact_source, bool separate_validation) {
  const auto stem = info.relativePath.substr(0, info.relativePath.size() - 4u);
  const auto models_path = stem + "_models.hpp";
  const auto validation_path = stem + "_validation.hpp";
  const auto basename = [](const std::string &path) {
    const auto slash = path.find_last_of('/');
    return path.substr(slash == std::string::npos ? 0u : slash + 1u);
  };
  std::ostringstream output;
  const auto preamble = ArtifactPreamble(manifest, allocation_fingerprint, source);
  output << preamble;
  output << "#include <array>\n";
  output << "#include <cstddef>\n";
  output << "#include <cstdint>\n";
  output << "#include <optional>\n";
  output << "#include <string>\n";
  output << "#include <string_view>\n";
  output << "#include <variant>\n";
  output << "#include <vector>\n\n";
  output << "#include \"arken_schema_cpp_v1/wire.hpp\"\n";

  std::set<std::string> imported_paths;
  for (const auto &import : source.imports) {
    if (!import.source.valid() || import.source.value >= sources.size() ||
        sources[import.source.value].source == nullptr) {
      AddDiagnostic(
          result, "schema.cpp.import_source_unresolved",
          "A generated C++ fragment references an unresolved imported source.",
          import.provenance);
      continue;
    }
    imported_paths.insert(sources[import.source.value].relativePath);
  }
  for (const auto &path : imported_paths)
    output << "#include \"" << (separate_validation ? path.substr(0, path.size() - 4u) + "_models.hpp" : path)
           << "\"\n";

  output << "\nnamespace " << info.namespaceName << "\n{\n";
  output << "namespace schema_cpp = ::arken::schema::cpp::v1;\n\n";

  EmitEnums(result, output, manifest.ir, source);
  EmitSemanticTypes(result, output, manifest.ir, source, sources);
  for (const auto *model : GeneratedModels(manifest.ir, source.handle)) {
    EmitModel(result, output, manifest.ir, source, *model, sources);
  }
  const auto models = output.str() + "}\n";
  if (separate_validation) {
    output.str({});
    output.clear();
    output << preamble << "#include " << StringLiteral(basename(validation_path)) << "\n";
    for (const auto &path : imported_paths)
      output << "#include " << StringLiteral(path) << "\n";
    output << "\nnamespace " << info.namespaceName << "\n{\n";
  }
  EmitSchemaCatalog(result, output, manifest, source, allocation_fingerprint);
  if (source.handle == manifest.ir.entrySource) {
    auto registry = generator_detail::GenerateCppRegistrySection(
        manifest, allocation_fingerprint, source, registry_sources);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(registry.diagnostics.begin()),
        std::make_move_iterator(registry.diagnostics.end()));
    if (registry.ok())
      output << registry.content;
  }
  const auto declarations = output.str();
  std::ostringstream bindings, operations, validators;
  if (separate_validation)
    for (const auto *model : GeneratedModels(manifest.ir, source.handle))
      EmitModelValidator(result, validators, manifest.ir, source, *model, sources);
  EmitModelBindings(result, bindings, manifest.ir, source, sources, separate_validation);
  EmitOperationTokens(result, operations, manifest.ir, source, sources);
  const auto make_artifact = [&](std::string path, std::string content) {
    if (compact_source)
      content = CompactEmittedSource(result, std::move(content), source);
    return GeneratedCppArtifact{
        .sourcePath = source.path,
        .relativePath = std::move(path),
        .namespaceName = info.namespaceName,
        .content = std::move(content),
    };
  };
  const auto namespace_open = "\nnamespace " + info.namespaceName + "\n{\n";
  const auto validation = preamble + "#include " + StringLiteral(basename(models_path))
      + "\n" + namespace_open + validators.str() + "}\n";
  auto complete = declarations + bindings.str() + operations.str() + "}\n";
  // Physical layout only: do not omit bindings, reflection, or constexpr data.
  // Small fragments need no companions; large fragments keep one full entrypoint.
  constexpr std::size_t decomposition_threshold = 4u * 1024u * 1024u;
  std::vector<GeneratedCppArtifact> artifacts;
  if (complete.size() <= decomposition_threshold)
    artifacts.push_back(make_artifact(info.relativePath, std::move(complete)));
  else {

  const auto types_path = stem + "_types.hpp", bindings_path = stem + "_bindings.hpp";
  // Bindings include types, never the primary: that would cycle before tokens exist.
  // Primary remains FIRST for semantic-source lookups by generators/consumers.
  artifacts = {
      make_artifact(info.relativePath, preamble + "#include " +
          StringLiteral(basename(bindings_path)) + "\n" + namespace_open + operations.str() + "}\n"),
      make_artifact(types_path, declarations + "}\n"),
      make_artifact(bindings_path, preamble + "#include " +
          StringLiteral(basename(types_path)) + "\n" + namespace_open + bindings.str() + "}\n"),
  };
  }
  if (separate_validation) {
    artifacts.push_back(make_artifact(models_path, models));
    artifacts.push_back(make_artifact(validation_path, validation));
  }
  return artifacts;
}
} // namespace

std::string ProjectCppFieldIdentifier(std::string_view canonicalName) {
  if (IsCppKeyword(canonicalName))
    return std::string(canonicalName) + "_field";
  return std::string(canonicalName);
}

bool CppGenerationResult::ok() const { return !HasErrors(diagnostics); }

CppGenerationResult GenerateCppArtifacts(const idl::SemanticManifest &manifest,
                                         const CppGenerationOptions &options) {
  CppGenerationResult result;
  const auto target_support =
      idl::ValidateTargetSupport(manifest.ir, idl::TargetProfile::Cpp);
  result.diagnostics = target_support.diagnostics;
  if (HasErrors(result.diagnostics) || !ValidateOptions(result, options))
    return result;

  const auto allocation = idl::BuildAllocationLedger(manifest.ir);
  result.diagnostics.insert(result.diagnostics.end(),
                            allocation.diagnostics.begin(),
                            allocation.diagnostics.end());
  if (HasErrors(result.diagnostics) || !allocation.ledger.has_value())
    return result;

  std::vector<SourceGenerationInfo> sources(manifest.ir.sources.size());
  std::map<std::string, idl::SourceHandle> source_paths;
  std::map<std::string, idl::SourceHandle> artifact_paths;
  for (const auto &source : manifest.ir.sources) {
    if (!source.handle.valid() || source.handle.value >= sources.size()) {
      AddDiagnostic(result, "schema.cpp.source_handle_invalid",
                    "A semantic source has an invalid canonical handle.",
                    {.sourcePath = source.path});
      continue;
    }
    ValidateSourceIdentifiers(result, manifest.ir, source);
    ValidateGeneratedNameCollisions(result, manifest.ir, source,
                                    source.handle == manifest.ir.entrySource, options.separateModelValidation);
    const std::string relative_path = ArtifactPath(source, options);
    const std::string namespace_name = NamespaceName(source, options);
    if (!source_paths.emplace(source.path, source.handle).second) {
      AddDiagnostic(result, "schema.cpp.source_path_duplicate",
                    "Multiple semantic sources have the same canonical path.",
                    {.sourcePath = source.path});
    }
    if (!artifact_paths.emplace(relative_path, source.handle).second) {
      AddDiagnostic(
          result, "schema.cpp.artifact_path_collision",
          "Multiple semantic sources generate the same C++ artifact path.",
          {.sourcePath = source.path});
    }
    sources[source.handle.value] = {
        .source = &source,
        .relativePath = relative_path,
        .namespaceName = namespace_name,
    };
  }
  if (HasErrors(result.diagnostics))
    return result;

  std::vector<generator_detail::CppRegistrySourceInfo> registry_sources(
      sources.size());
  for (std::size_t ordinal = 0; ordinal < sources.size(); ++ordinal) {
    registry_sources[ordinal] = {
        .source = sources[ordinal].source,
        .namespaceName = sources[ordinal].namespaceName,
    };
  }

  std::vector<const SourceGenerationInfo *> ordered_sources;
  for (const auto &source : sources) {
    if (source.source != nullptr)
      ordered_sources.push_back(&source);
  }
  std::sort(ordered_sources.begin(), ordered_sources.end(),
            [](const auto *lhs, const auto *rhs) {
              if (lhs->source->path != rhs->source->path)
                return lhs->source->path < rhs->source->path;
              return lhs->relativePath < rhs->relativePath;
            });
  for (const auto *source : ordered_sources) {
    auto artifacts = GenerateArtifacts(
        result, manifest, allocation.ledger->allocationFingerprint,
        *source->source, *source, sources, registry_sources, options.compactSource, options.separateModelValidation);
    result.artifacts.insert(result.artifacts.end(),
                            std::make_move_iterator(artifacts.begin()),
                            std::make_move_iterator(artifacts.end()));
  }
  std::set<std::string> emitted_paths;
  for (const auto &artifact : result.artifacts) {
    if (!emitted_paths.insert(artifact.relativePath).second)
      AddDiagnostic(result, "schema.cpp.artifact_path_collision",
                    "Generated entrypoint and companion artifact paths must be unique.",
                    {.sourcePath = artifact.sourcePath});
  }
  if (HasErrors(result.diagnostics))
    result.artifacts.clear();
  return result;
}
} // namespace arken::schema::cpp::v1
