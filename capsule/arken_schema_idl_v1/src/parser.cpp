#include "arken_schema_idl_v1/idl.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "lexer.hpp"

namespace arken::schema::idl::v1 {
namespace {
using detail::Token;
using detail::TokenKind;

struct ParsedAttribute {
  std::string name;
  std::uint32_t value = 0;
  bool hasValue = false;
  SourceSpan span;
};

enum class PropertyKind {
  Identifier,
  Integer,
  Boolean,
  String,
  IdentifierList,
};

struct ParsedProperty {
  std::string name;
  std::string text;
  std::uint32_t number = 0;
  bool boolean = false;
  std::vector<std::string> list;
  SourceSpan span;
};

void SortDiagnostics(std::vector<Diagnostic> &diagnostics) {
  std::sort(diagnostics.begin(), diagnostics.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.sourceName != rhs.sourceName)
                return lhs.sourceName < rhs.sourceName;
              if (lhs.span.begin.offset != rhs.span.begin.offset)
                return lhs.span.begin.offset < rhs.span.begin.offset;
              if (lhs.code != rhs.code)
                return lhs.code < rhs.code;
              return lhs.message < rhs.message;
            });
}

std::uint32_t StableRegistryId(std::string_view registry_namespace,
                               std::string_view canonical_name) {
  std::uint32_t hash = 0x811c9dc5u;
  const auto append = [&hash](std::string_view value) {
    for (const unsigned char byte : value) {
      hash ^= byte;
      hash *= 0x01000193u;
    }
  };
  append(registry_namespace);
  hash ^= 0xffu;
  hash *= 0x01000193u;
  append(canonical_name);
  return hash == 0u ? 1u : hash;
}

std::vector<std::string> DefaultSemanticTargets() {
  return {"cpp", "csharp", "fabric", "http_json", "javascript", "sqlite"};
}

class Parser {
public:
  Parser(std::string_view source_name, std::vector<Token> tokens,
         std::vector<Comment> comments, std::vector<Diagnostic> diagnostics,
         const ParseLimits &limits)
      : sourceName_(source_name), tokens_(std::move(tokens)),
        comments_(std::move(comments)), diagnostics_(std::move(diagnostics)),
        limits_(limits) {}

  ParseResult Run() {
    Document document;
    document.comments = std::move(comments_);
    if (Check(TokenKind::Edition)) {
      ParseEdition(document);
      ParseSchema(document);
    } else {
      ParseSchemaLanguage(document);
      ParseModule(document);
    }
    currentEdition_ = document.edition;
    ParseOwner(document);
    if (CheckNamedDeclaration("catalog"))
      ParseCatalog(document);
    ParseImports(document);
    while (!Check(TokenKind::End)) {
      if (Check(TokenKind::Reserved)) {
        ParseReservation(document.reservedIds, document.reservedNames,
                         std::nullopt);
      } else if (Check(TokenKind::Model)) {
        if (document.models.size() >= limits_.maximumModels) {
          AddDiagnostic("idl.model_limit_exceeded",
                        "IDL model count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.models.push_back(ParseModel(document));
      } else if (CheckNamedDeclaration("enum")) {
        if (document.enums.size() >= limits_.maximumEnums) {
          AddDiagnostic("idl.enum_limit_exceeded",
                        "IDL enum count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.enums.push_back(ParseEnum());
      } else if (CheckNamedDeclaration("wire_type")) {
        if (document.wireTypes.size() >= limits_.maximumWireTypes) {
          AddDiagnostic("idl.wire_type_limit_exceeded",
                        "IDL wire type count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.wireTypes.push_back(ParseWireType());
      } else if (CheckNamedDeclaration("storage_codec")) {
        if (document.storageCodecs.size() >= limits_.maximumStorageCodecs) {
          AddDiagnostic("idl.storage_codec_limit_exceeded",
                        "IDL storage codec count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.storageCodecs.push_back(ParseStorageCodec());
      } else if ((document.edition == 1 &&
                  CheckNamedDeclaration("semantic_type")) ||
                 (document.edition == 2 && CheckNamedDeclaration("type"))) {
        if (document.semanticTypes.size() >= limits_.maximumSemanticTypes) {
          AddDiagnostic("idl.semantic_type_limit_exceeded",
                        "IDL semantic type count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.semanticTypes.push_back(ParseSemanticType(document.edition));
      } else if (CheckNamedDeclaration("command")) {
        if (document.commands.size() >= limits_.maximumCommands) {
          AddDiagnostic("idl.command_limit_exceeded",
                        "IDL command count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.commands.push_back(ParseCommand());
      } else if (CheckNamedDeclaration("event")) {
        if (document.events.size() >= limits_.maximumEvents) {
          AddDiagnostic("idl.event_limit_exceeded",
                        "IDL event count exceeds the configured limit.",
                        Current().span);
          break;
        }
        document.events.push_back(ParseEvent());
      } else {
        AddDiagnostic("idl.unexpected_top_level_declaration",
                      "Expected a type, reservation, enum, model, command, or "
                      "event declaration.",
                      Current().span);
        SynchronizeTopLevel();
      }
    }

    Validate(document);
    ParseResult result{.diagnostics = std::move(diagnostics_)};
    SortDiagnostics(result.diagnostics);
    const bool has_error =
        std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                    [](const Diagnostic &diagnostic) {
                      return diagnostic.severity == DiagnosticSeverity::Error;
                    });
    if (!has_error) {
      result.normalizedText = Format(document);
      result.document = std::move(document);
    }
    return result;
  }

private:
  const Token &Current() const {
    return tokens_[std::min(index_, tokens_.size() - 1)];
  }

  const Token &Previous() const {
    return tokens_[index_ == 0 ? 0 : index_ - 1];
  }

  bool Check(TokenKind kind) const { return Current().kind == kind; }

  bool CheckNamedDeclaration(std::string_view name) const {
    return Check(TokenKind::Identifier) && Current().text == name;
  }

  const Token &Advance() {
    if (!Check(TokenKind::End))
      ++index_;
    return Previous();
  }

  bool Match(TokenKind kind) {
    if (!Check(kind))
      return false;
    Advance();
    return true;
  }

  const Token *Consume(TokenKind kind, std::string_view context) {
    if (Check(kind))
      return &Advance();
    AddDiagnostic("idl.expected_token",
                  "Expected " + std::string(detail::TokenKindName(kind)) + " " +
                      std::string(context) + ".",
                  Current().span);
    return nullptr;
  }

  void AddDiagnostic(std::string code, std::string message, SourceSpan span,
                     std::vector<RelatedDiagnosticSpan> related_spans = {}) {
    if (diagnostics_.size() >= limits_.maximumDiagnostics)
      return;
    diagnostics_.push_back({
        .code = std::move(code),
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .sourceName = sourceName_,
        .span = span,
        .relatedSpans = std::move(related_spans),
    });
  }

  std::uint32_t ParseU32(const Token *token, std::string_view identity) {
    if (token == nullptr)
      return 0;
    std::uint64_t value = 0;
    std::string_view digits = token->text;
    int base = 10;
    if (digits.size() >= 2 && digits[0] == '0' &&
        (digits[1] == 'x' || digits[1] == 'X')) {
      digits.remove_prefix(2);
      base = 16;
    }
    const char *begin = digits.data();
    const char *end = begin + digits.size();
    const auto parsed = std::from_chars(begin, end, value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        value > std::numeric_limits<std::uint32_t>::max()) {
      AddDiagnostic("idl.integer_out_of_range",
                    std::string(identity) + " must fit in u32.", token->span);
      return 0;
    }
    return static_cast<std::uint32_t>(value);
  }

  void ParseEdition(Document &document) {
    Consume(TokenKind::Edition, "at the beginning of the document");
    document.edition =
        ParseU32(Consume(TokenKind::Integer, "after edition"), "Edition");
    Consume(TokenKind::Semicolon, "after edition number");
  }

  void ParseSchema(Document &document) {
    Consume(TokenKind::Schema, "after edition declaration");
    if (const auto *name = Consume(TokenKind::Identifier, "as the schema name"))
      document.schemaName = name->text;
    if (const auto *version =
            Consume(TokenKind::Identifier, "as a v-prefixed schema version")) {
      if (version->text.size() < 2 || version->text.front() != 'v' ||
          !std::all_of(
              version->text.begin() + 1, version->text.end(),
              [](char value) { return value >= '0' && value <= '9'; })) {
        AddDiagnostic("idl.invalid_schema_version",
                      "Schema version must use v followed by a positive u32.",
                      version->span);
      } else {
        Token digits = *version;
        digits.text.erase(digits.text.begin());
        document.schemaVersion = ParseU32(&digits, "Schema version");
      }
    }
    Consume(TokenKind::Semicolon, "after schema declaration");
  }

  void ParseSchemaLanguage(Document &document) {
    Consume(TokenKind::Schema, "at the beginning of the document");
    if (const auto *version = Consume(TokenKind::Identifier,
                                      "as a v-prefixed language version")) {
      if (version->text.size() < 2 || version->text.front() != 'v' ||
          !std::all_of(
              version->text.begin() + 1, version->text.end(),
              [](char value) { return value >= '0' && value <= '9'; })) {
        AddDiagnostic(
            "idl.invalid_language_version",
            "Schema language version must use v followed by a positive u32.",
            version->span);
      } else {
        Token digits = *version;
        digits.text.erase(digits.text.begin());
        document.edition = ParseU32(&digits, "Schema language version");
      }
    }
    Consume(TokenKind::Semicolon, "after schema language version");
  }

  void ParseModule(Document &document) {
    if (!CheckNamedDeclaration("module")) {
      AddDiagnostic(
          "idl.module_declaration_missing",
          "Schema V2 requires a module declaration after the language version.",
          Current().span);
      return;
    }
    Advance();
    if (const auto *name = Consume(TokenKind::Identifier, "as the module name"))
      document.schemaName = name->text;
    if (const auto *version =
            Consume(TokenKind::Identifier, "as a v-prefixed module version")) {
      if (version->text.size() < 2 || version->text.front() != 'v' ||
          !std::all_of(
              version->text.begin() + 1, version->text.end(),
              [](char value) { return value >= '0' && value <= '9'; })) {
        AddDiagnostic("idl.invalid_schema_version",
                      "Module version must use v followed by a positive u32.",
                      version->span);
      } else {
        Token digits = *version;
        digits.text.erase(digits.text.begin());
        document.schemaVersion = ParseU32(&digits, "Module version");
      }
    }
    Consume(TokenKind::Semicolon, "after module declaration");
  }

  void ParseOwner(Document &document) {
    Consume(TokenKind::Owner, "after schema declaration");
    if (const auto *owner =
            Consume(TokenKind::Identifier, "as the owner capsule"))
      document.ownerCapsule = owner->text;
    Consume(TokenKind::Semicolon, "after owner declaration");
  }

  CatalogFragmentDeclaration ParseCatalogFragment() {
    const Token begin = Advance();
    CatalogFragmentDeclaration declaration;
    if (const auto *fragment_id =
            Consume(TokenKind::String, "as the catalog fragment ID"))
      declaration.fragmentId = fragment_id->decodedText;
    const auto properties = ParseProperties(
        {
            {"declaration_order", PropertyKind::Integer},
            {"owner_capsule", PropertyKind::Identifier},
            {"version", PropertyKind::Integer},
        },
        "catalog fragment");
    const auto *owner =
        RequiredProperty(properties, "owner_capsule", begin.span);
    const auto *version = RequiredProperty(properties, "version", begin.span);
    const auto *order =
        RequiredProperty(properties, "declaration_order", begin.span);
    if (owner != nullptr)
      declaration.ownerCapsule = owner->text;
    if (version != nullptr)
      declaration.version = version->number;
    if (order != nullptr)
      declaration.declarationOrder = order->number;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  void ParseCatalog(Document &document) {
    const Token begin = Advance();
    CatalogDeclaration declaration;
    if (const auto *catalog_id =
            Consume(TokenKind::String, "as the catalog ID"))
      declaration.catalogId = catalog_id->decodedText;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"catalog_version"});
    declaration.version =
        RequiredAttribute(attributes, "catalog_version", begin.span);
    Consume(TokenKind::LeftBrace, "before catalog fragments");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
      if (CheckNamedDeclaration("fragment")) {
        if (declaration.fragments.size() >= limits_.maximumCatalogFragments) {
          AddDiagnostic("idl.catalog_fragment_limit_exceeded",
                        "Catalog fragment count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseCatalogFragment());
        } else
          declaration.fragments.push_back(ParseCatalogFragment());
      } else {
        AddDiagnostic("idl.unexpected_catalog_member",
                      "Expected a catalog fragment declaration or the end of "
                      "the catalog.",
                      Current().span);
        while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End) &&
               !CheckNamedDeclaration("fragment"))
          Advance();
      }
    }
    const Token *close =
        Consume(TokenKind::RightBrace, "after catalog fragments");
    declaration.span = {
        begin.span.begin,
        close == nullptr ? Current().span.begin : close->span.end,
    };
    document.catalog = std::move(declaration);
  }

  CatalogModelIdentityDeclaration ParseModelCatalogIdentity() {
    const Token begin = Advance();
    CatalogModelIdentityDeclaration declaration;
    const auto properties = ParseProperties(
        {
            {"declaration_order", PropertyKind::Integer},
            {"domain", PropertyKind::String},
            {"fragment_id", PropertyKind::String},
            {"logical_name", PropertyKind::String},
            {"model_id", PropertyKind::String},
            {"qualified_name", PropertyKind::String},
            {"registry_key", PropertyKind::String},
        },
        "model catalog_identity");
    const auto *fragment_id =
        RequiredProperty(properties, "fragment_id", begin.span);
    const auto *registry_key =
        RequiredProperty(properties, "registry_key", begin.span);
    const auto *model_id = RequiredProperty(properties, "model_id", begin.span);
    const auto *logical_name =
        RequiredProperty(properties, "logical_name", begin.span);
    const auto *qualified_name =
        RequiredProperty(properties, "qualified_name", begin.span);
    const auto *order =
        RequiredProperty(properties, "declaration_order", begin.span);
    if (fragment_id != nullptr)
      declaration.fragmentId = fragment_id->text;
    if (registry_key != nullptr)
      declaration.registryKey = registry_key->text;
    if (model_id != nullptr)
      declaration.modelId = model_id->text;
    if (logical_name != nullptr)
      declaration.logicalName = logical_name->text;
    if (qualified_name != nullptr)
      declaration.qualifiedName = qualified_name->text;
    if (const auto *domain = OptionalProperty(properties, "domain"))
      declaration.domain = domain->text;
    if (order != nullptr)
      declaration.declarationOrder = order->number;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  CatalogFieldIdentityDeclaration ParseFieldCatalogIdentity() {
    const Token begin = Advance();
    CatalogFieldIdentityDeclaration declaration;
    const auto properties = ParseProperties(
        {
            {"declaration_order", PropertyKind::Integer},
            {"field_name", PropertyKind::String},
            {"registry_key", PropertyKind::String},
        },
        "field catalog_identity");
    const auto *registry_key =
        RequiredProperty(properties, "registry_key", begin.span);
    const auto *field_name =
        RequiredProperty(properties, "field_name", begin.span);
    const auto *order =
        RequiredProperty(properties, "declaration_order", begin.span);
    if (registry_key != nullptr)
      declaration.registryKey = registry_key->text;
    if (field_name != nullptr)
      declaration.fieldName = field_name->text;
    if (order != nullptr)
      declaration.declarationOrder = order->number;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  FieldRepresentationDeclaration ParseFieldRepresentation() {
    const Token begin = Advance();
    FieldRepresentationDeclaration declaration;
    if (currentEdition_ == 2) {
      const auto properties = ParseProperties(
          {
              {"field_kind", PropertyKind::Identifier},
              {"normalization", PropertyKind::Identifier},
          },
          "field representation");
      const auto *field_kind = OptionalProperty(properties, "field_kind");
      const auto *normalization = OptionalProperty(properties, "normalization");
      if (field_kind == nullptr && normalization == nullptr)
        AddDiagnostic(
            "idl.empty_field_representation",
            "Field representation must refine field_kind or normalization.",
            begin.span);
      if (field_kind != nullptr)
        declaration.fieldKind = field_kind->text;
      if (normalization != nullptr)
        declaration.normalization = normalization->text;
      declaration.span = {begin.span.begin, Previous().span.end};
      return declaration;
    }
    const auto properties = ParseProperties(
        {
            {"field_kind", PropertyKind::Identifier},
            {"normalization", PropertyKind::Identifier},
            {"precision", PropertyKind::Integer},
            {"scale", PropertyKind::Integer},
            {"storage", PropertyKind::Identifier},
            {"storage_size", PropertyKind::Integer},
            {"wire", PropertyKind::Identifier},
            {"wire_size", PropertyKind::Integer},
        },
        "field representation");
    const auto *field_kind =
        RequiredProperty(properties, "field_kind", begin.span);
    const auto *wire = RequiredProperty(properties, "wire", begin.span);
    const auto *wire_size =
        RequiredProperty(properties, "wire_size", begin.span);
    const auto *storage = RequiredProperty(properties, "storage", begin.span);
    const auto *storage_size =
        RequiredProperty(properties, "storage_size", begin.span);
    const auto *precision =
        RequiredProperty(properties, "precision", begin.span);
    const auto *scale = RequiredProperty(properties, "scale", begin.span);
    const auto *normalization =
        RequiredProperty(properties, "normalization", begin.span);
    if (field_kind != nullptr)
      declaration.fieldKind = field_kind->text;
    if (wire != nullptr)
      declaration.wireTypeName = wire->text;
    if (wire_size != nullptr)
      declaration.wireSize = wire_size->number;
    if (storage != nullptr)
      declaration.storageCodecName = storage->text;
    if (storage_size != nullptr)
      declaration.storageSize = storage_size->number;
    if (precision != nullptr)
      declaration.precision = precision->number;
    if (scale != nullptr)
      declaration.scale = scale->number;
    if (normalization != nullptr)
      declaration.normalization = normalization->text;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  void ParseImports(Document &document) {
    while (Check(TokenKind::Import)) {
      const Token begin = Advance();
      const Token *path = Consume(TokenKind::String, "as the import path");
      const Token *semicolon =
          Consume(TokenKind::Semicolon, "after import declaration");
      if (document.imports.size() >= limits_.maximumImportsPerSource) {
        AddDiagnostic(
            "idl.import_limit_exceeded",
            "IDL import count exceeds the configured per-source limit.",
            begin.span);
        continue;
      }
      document.imports.push_back({
          .path = path == nullptr ? std::string{} : path->decodedText,
          .pathSpan = path == nullptr ? begin.span : path->span,
          .span =
              {
                  begin.span.begin,
                  semicolon == nullptr ? Current().span.begin
                                       : semicolon->span.end,
              },
      });
    }
  }

  std::optional<RegistryKind> ParseRegistryKind() {
    if (Match(TokenKind::Model))
      return RegistryKind::Model;
    if (Match(TokenKind::Field))
      return RegistryKind::Field;
    if (!Check(TokenKind::Identifier)) {
      AddDiagnostic("idl.reservation_registry_missing",
                    "Reserved declaration requires a registry kind.",
                    Current().span);
      return std::nullopt;
    }
    const Token token = Advance();
    if (token.text == "semantic_type")
      return RegistryKind::SemanticType;
    if (token.text == "wire_type")
      return RegistryKind::WireType;
    if (token.text == "storage_codec")
      return RegistryKind::StorageCodec;
    if (token.text == "enum")
      return RegistryKind::Enum;
    if (token.text == "enum_value")
      return RegistryKind::EnumValue;
    if (token.text == "command")
      return RegistryKind::Command;
    if (token.text == "event")
      return RegistryKind::Event;
    AddDiagnostic("idl.reservation_registry_invalid",
                  "Reserved declaration uses an unknown registry kind.",
                  token.span);
    return std::nullopt;
  }

  void ParseReservation(std::vector<ReservedIdDeclaration> &reserved_ids,
                        std::vector<ReservedNameDeclaration> &reserved_names,
                        std::optional<RegistryKind> member_registry) {
    const Token begin = Advance();
    const auto registry = ParseRegistryKind();
    const Token *reservation_kind =
        Consume(TokenKind::Identifier, "as id or name");
    if (reserved_ids.size() + reserved_names.size() >=
        limits_.maximumReservations) {
      AddDiagnostic("idl.reservation_limit_exceeded",
                    "IDL reservation count exceeds the configured limit.",
                    begin.span);
    }

    if (registry.has_value() &&
        ((member_registry.has_value() && *registry != *member_registry) ||
         (!member_registry.has_value() &&
          (*registry == RegistryKind::Field ||
           *registry == RegistryKind::EnumValue)))) {
      AddDiagnostic("idl.reservation_scope_invalid",
                    member_registry == RegistryKind::Field
                        ? "Model members may reserve only field identities."
                    : member_registry == RegistryKind::EnumValue
                        ? "Enum members may reserve only enum_value identities."
                        : "Field and enum_value identities must be reserved "
                          "inside their owners.",
                    begin.span);
    }

    if (reservation_kind != nullptr && reservation_kind->text == "id") {
      const Token *value =
          Consume(TokenKind::Integer, "as the reserved numeric identity");
      const Token *semicolon =
          Consume(TokenKind::Semicolon, "after reserved ID declaration");
      if (registry.has_value() && reserved_ids.size() + reserved_names.size() <
                                      limits_.maximumReservations) {
        reserved_ids.push_back({
            .registry = *registry,
            .value = ParseU32(value, "Reserved identity"),
            .span =
                {
                    begin.span.begin,
                    semicolon == nullptr ? Current().span.begin
                                         : semicolon->span.end,
                },
        });
      }
      return;
    }
    if (reservation_kind != nullptr && reservation_kind->text == "name") {
      const Token *value =
          Consume(TokenKind::Identifier, "as the reserved canonical name");
      const Token *semicolon =
          Consume(TokenKind::Semicolon, "after reserved name declaration");
      if (registry.has_value() && reserved_ids.size() + reserved_names.size() <
                                      limits_.maximumReservations) {
        reserved_names.push_back({
            .registry = *registry,
            .value = value == nullptr ? std::string{} : value->text,
            .span =
                {
                    begin.span.begin,
                    semicolon == nullptr ? Current().span.begin
                                         : semicolon->span.end,
                },
        });
      }
      return;
    }
    if (reservation_kind != nullptr) {
      AddDiagnostic("idl.reservation_kind_invalid",
                    "Reserved declaration kind must be id or name.",
                    reservation_kind->span);
    }
    while (!Check(TokenKind::Semicolon) && !Check(TokenKind::End))
      Advance();
    Match(TokenKind::Semicolon);
  }

  std::vector<ParsedAttribute> ParseAttributes() {
    std::vector<ParsedAttribute> attributes;
    while (Match(TokenKind::At)) {
      const SourcePosition begin = Previous().span.begin;
      const Token *name = Consume(TokenKind::Identifier, "after @");
      const Token *value = nullptr;
      const Token *close = nullptr;
      const bool has_value = Match(TokenKind::LeftParen);
      if (has_value) {
        value = Consume(TokenKind::Integer, "as the attribute value");
        close = Consume(TokenKind::RightParen, "after attribute value");
      }
      attributes.push_back({
          .name = name == nullptr ? std::string{} : name->text,
          .value = ParseU32(value, "Attribute value"),
          .hasValue = has_value,
          .span =
              {
                  begin,
                  close != nullptr ? close->span.end
                                   : (name != nullptr ? name->span.end
                                                      : Current().span.begin),
              },
      });
    }
    return attributes;
  }

  std::uint32_t
  RequiredAttribute(const std::vector<ParsedAttribute> &attributes,
                    std::string_view name, SourceSpan declaration_span) {
    std::uint32_t value = 0;
    std::size_t matches = 0;
    const ParsedAttribute *first_match = nullptr;
    for (const auto &attribute : attributes) {
      if (attribute.name == name) {
        if (first_match == nullptr)
          first_match = &attribute;
        if (attribute.hasValue) {
          value = attribute.value;
        } else {
          AddDiagnostic("idl.attribute_value_required",
                        "Attribute @" + std::string(name) +
                            " requires an integer value.",
                        attribute.span);
        }
        ++matches;
      }
    }
    if (matches == 0) {
      AddDiagnostic("idl.required_attribute_missing",
                    "Declaration requires @" + std::string(name) + "(...).",
                    declaration_span);
    } else if (matches > 1) {
      AddDiagnostic("idl.duplicate_attribute",
                    "Declaration repeats @" + std::string(name) + ".",
                    declaration_span,
                    {{
                        .message = "The first attribute is here.",
                        .sourceName = sourceName_,
                        .span = first_match->span,
                    }});
    }
    return value;
  }

  std::optional<std::uint32_t>
  OptionalAttribute(const std::vector<ParsedAttribute> &attributes,
                    std::string_view name, SourceSpan declaration_span) {
    std::optional<std::uint32_t> value;
    const ParsedAttribute *first_match = nullptr;
    for (const auto &attribute : attributes) {
      if (attribute.name != name)
        continue;
      if (first_match == nullptr)
        first_match = &attribute;
      if (!attribute.hasValue)
        AddDiagnostic("idl.attribute_value_required",
                      "Attribute @" + std::string(name) +
                          " requires an integer value.",
                      attribute.span);
      else
        value = attribute.value;
    }
    if (first_match != nullptr) {
      const auto count = std::count_if(
          attributes.begin(), attributes.end(),
          [name](const auto &attribute) { return attribute.name == name; });
      if (count > 1)
        AddDiagnostic("idl.duplicate_attribute",
                      "Declaration repeats @" + std::string(name) + ".",
                      declaration_span,
                      {{
                          .message = "The first attribute is here.",
                          .sourceName = sourceName_,
                          .span = first_match->span,
                      }});
    }
    return value;
  }

  bool MarkerAttribute(const std::vector<ParsedAttribute> &attributes,
                       std::string_view name, SourceSpan declaration_span) {
    std::size_t matches = 0;
    for (const auto &attribute : attributes) {
      if (attribute.name != name)
        continue;
      ++matches;
      if (attribute.hasValue)
        AddDiagnostic("idl.attribute_value_forbidden",
                      "Marker attribute @" + std::string(name) +
                          " does not accept a value.",
                      attribute.span);
    }
    if (matches > 1)
      AddDiagnostic("idl.duplicate_attribute",
                    "Declaration repeats @" + std::string(name) + ".",
                    declaration_span);
    return matches != 0;
  }

  void RejectUnknownAttributes(const std::vector<ParsedAttribute> &attributes,
                               const std::set<std::string> &allowed) {
    for (const auto &attribute : attributes) {
      if (!allowed.contains(attribute.name)) {
        AddDiagnostic("idl.unknown_attribute",
                      "Attribute @" + attribute.name +
                          " is not supported in this declaration.",
                      attribute.span);
      }
    }
  }

  std::vector<ParsedProperty>
  ParseProperties(const std::map<std::string, PropertyKind> &allowed,
                  std::string_view declaration_kind) {
    std::vector<ParsedProperty> properties;
    Consume(TokenKind::LeftBrace,
            "before " + std::string(declaration_kind) + " properties");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
      const Token *name = Consume(TokenKind::Identifier, "as a property name");
      if (name == nullptr) {
        while (!Check(TokenKind::Semicolon) && !Check(TokenKind::RightBrace) &&
               !Check(TokenKind::End))
          Advance();
        Match(TokenKind::Semicolon);
        continue;
      }
      const auto expected = allowed.find(name->text);
      if (expected == allowed.end()) {
        AddDiagnostic("idl.unknown_type_property",
                      std::string(declaration_kind) +
                          " does not support property " + name->text + ".",
                      name->span);
        while (!Check(TokenKind::Semicolon) && !Check(TokenKind::RightBrace) &&
               !Check(TokenKind::End))
          Advance();
        Match(TokenKind::Semicolon);
        continue;
      }

      ParsedProperty property{.name = name->text};
      const auto begin = name->span.begin;
      if (expected->second == PropertyKind::Integer) {
        property.number =
            ParseU32(Consume(TokenKind::Integer, "as the property value"),
                     "Property value");
      } else if (expected->second == PropertyKind::Identifier ||
                 expected->second == PropertyKind::Boolean) {
        if (const auto *value =
                Consume(TokenKind::Identifier, "as the property value")) {
          property.text = value->text;
          if (expected->second == PropertyKind::Boolean) {
            if (value->text == "true")
              property.boolean = true;
            else if (value->text == "false")
              property.boolean = false;
            else
              AddDiagnostic("idl.boolean_invalid",
                            "Boolean property must be true or false.",
                            value->span);
          }
        }
      } else if (expected->second == PropertyKind::String) {
        if (const auto *value =
                Consume(TokenKind::String, "as the property value"))
          property.text = value->decodedText;
      } else {
        if (const auto *value =
                Consume(TokenKind::Identifier, "as a list value"))
          property.list.push_back(value->text);
        while (Match(TokenKind::Comma)) {
          if (const auto *value =
                  Consume(TokenKind::Identifier, "as a list value"))
            property.list.push_back(value->text);
        }
      }
      const Token *semicolon =
          Consume(TokenKind::Semicolon, "after property value");
      property.span = {begin, semicolon == nullptr ? Current().span.begin
                                                   : semicolon->span.end};
      properties.push_back(std::move(property));
    }
    Consume(TokenKind::RightBrace,
            "after " + std::string(declaration_kind) + " properties");
    return properties;
  }

  const ParsedProperty *
  RequiredProperty(const std::vector<ParsedProperty> &properties,
                   std::string_view name, SourceSpan declaration_span) {
    const ParsedProperty *result = nullptr;
    for (const auto &property : properties) {
      if (property.name != name)
        continue;
      if (result != nullptr) {
        AddDiagnostic("idl.duplicate_type_property",
                      "Declaration repeats property " + std::string(name) + ".",
                      property.span,
                      {{.message = "The first property is here.",
                        .sourceName = sourceName_,
                        .span = result->span}});
      } else {
        result = &property;
      }
    }
    if (result == nullptr) {
      AddDiagnostic("idl.required_type_property_missing",
                    "Declaration requires property " + std::string(name) + ".",
                    declaration_span);
    }
    return result;
  }

  const ParsedProperty *
  OptionalProperty(const std::vector<ParsedProperty> &properties,
                   std::string_view name) {
    const ParsedProperty *result = nullptr;
    for (const auto &property : properties) {
      if (property.name != name)
        continue;
      if (result != nullptr) {
        AddDiagnostic("idl.duplicate_type_property",
                      "Declaration repeats property " + std::string(name) + ".",
                      property.span,
                      {{.message = "The first property is here.",
                        .sourceName = sourceName_,
                        .span = result->span}});
      } else {
        result = &property;
      }
    }
    return result;
  }

  std::optional<CatalogMemberIdentityDeclaration>
  ParseCatalogMemberIdentity(const std::vector<ParsedProperty> &properties,
                             SourceSpan declaration_span) {
    const auto *name = OptionalProperty(properties, "catalog_name");
    const auto *order = OptionalProperty(properties, "catalog_order");
    if (name == nullptr && order == nullptr)
      return std::nullopt;
    if (name == nullptr || order == nullptr) {
      AddDiagnostic("idl.catalog_member_identity_incomplete",
                    "Catalog member identity requires both catalog_name and "
                    "catalog_order.",
                    declaration_span);
      return std::nullopt;
    }
    return CatalogMemberIdentityDeclaration{
        .name = name->text,
        .declarationOrder = order->number,
        .span = {name->span.begin, order->span.end},
    };
  }

  WireTypeDeclaration ParseWireType() {
    const Token begin = Advance();
    WireTypeDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the wire type name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"wire_type_id"});
    declaration.wireTypeId =
        RequiredAttribute(attributes, "wire_type_id", begin.span);
    const auto properties = ParseProperties(
        {
            {"bit_width", PropertyKind::Integer},
            {"byte_order", PropertyKind::Identifier},
            {"fixed_size", PropertyKind::Integer},
        },
        "wire_type");
    const auto *bit_width =
        RequiredProperty(properties, "bit_width", begin.span);
    const auto *fixed_size =
        RequiredProperty(properties, "fixed_size", begin.span);
    const auto *byte_order =
        RequiredProperty(properties, "byte_order", begin.span);
    if (bit_width != nullptr)
      declaration.bitWidth = bit_width->number;
    if (fixed_size != nullptr)
      declaration.fixedSize = fixed_size->number;
    if (byte_order != nullptr)
      declaration.byteOrder = byte_order->text;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  StorageCodecDeclaration ParseStorageCodec() {
    const Token begin = Advance();
    StorageCodecDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the storage codec name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"storage_codec_id"});
    declaration.storageCodecId =
        RequiredAttribute(attributes, "storage_codec_id", begin.span);
    const auto properties = ParseProperties(
        {
            {"exact", PropertyKind::Boolean},
            {"fixed_size", PropertyKind::Integer},
            {"order_preserving", PropertyKind::Boolean},
            {"storage_class", PropertyKind::Identifier},
        },
        "storage_codec");
    const auto *storage_class =
        RequiredProperty(properties, "storage_class", begin.span);
    const auto *fixed_size =
        RequiredProperty(properties, "fixed_size", begin.span);
    const auto *exact = RequiredProperty(properties, "exact", begin.span);
    const auto *order =
        RequiredProperty(properties, "order_preserving", begin.span);
    if (storage_class != nullptr)
      declaration.storageClass = storage_class->text;
    if (fixed_size != nullptr)
      declaration.fixedSize = fixed_size->number;
    if (exact != nullptr)
      declaration.exact = exact->boolean;
    if (order != nullptr)
      declaration.orderPreserving = order->boolean;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  SemanticTypeDeclaration ParseSemanticType(std::uint32_t edition) {
    const Token begin = Advance();
    SemanticTypeDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the semantic type name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    if (edition == 2) {
      RejectUnknownAttributes(attributes, {"id"});
      if (const auto id = OptionalAttribute(attributes, "id", begin.span);
          id.has_value()) {
        declaration.semanticTypeId = *id;
        declaration.semanticTypeIdExplicit = true;
      }
      declaration.targets = DefaultSemanticTargets();
      const auto properties = ParseProperties(
          {
              {"boundary_mapping", PropertyKind::Identifier},
              {"default", PropertyKind::Identifier},
              {"enum", PropertyKind::Identifier},
              {"fixed_size", PropertyKind::Integer},
              {"json_profile", PropertyKind::Identifier},
              {"kind", PropertyKind::Identifier},
              {"narrowing", PropertyKind::Identifier},
              {"normalization", PropertyKind::Identifier},
              {"precision", PropertyKind::Integer},
              {"presence", PropertyKind::Identifier},
              {"registry_key", PropertyKind::String},
              {"representation", PropertyKind::Identifier},
              {"scale", PropertyKind::Integer},
              {"semantic_name", PropertyKind::String},
              {"targets", PropertyKind::IdentifierList},
              {"temporal_role", PropertyKind::Identifier},
              {"wire", PropertyKind::Identifier},
          },
          "type");
      const auto *kind = RequiredProperty(properties, "kind", begin.span);
      if (kind != nullptr)
        declaration.kind = kind->text;
      if (const auto *wire = OptionalProperty(properties, "wire"))
        declaration.wireTypeName = wire->text;
      if (const auto *fixed_size = OptionalProperty(properties, "fixed_size"))
        declaration.fixedSize = fixed_size->number;
      if (const auto *precision = OptionalProperty(properties, "precision"))
        declaration.precision = precision->number;
      if (const auto *scale = OptionalProperty(properties, "scale"))
        declaration.scale = scale->number;
      if (const auto *semantic_name =
              OptionalProperty(properties, "semantic_name"))
        declaration.semanticName = semantic_name->text;
      if (const auto *registry_key =
              OptionalProperty(properties, "registry_key"))
        declaration.registryKey = registry_key->text;
      if (const auto *normalization =
              OptionalProperty(properties, "normalization"))
        declaration.normalization = normalization->text;
      if (const auto *json_profile =
              OptionalProperty(properties, "json_profile"))
        declaration.jsonProfile = json_profile->text;
      if (const auto *boundary_mapping =
              OptionalProperty(properties, "boundary_mapping"))
        declaration.boundaryMapping = boundary_mapping->text;
      if (const auto *temporal_role =
              OptionalProperty(properties, "temporal_role"))
        declaration.temporalRole = temporal_role->text;
      if (const auto *presence = OptionalProperty(properties, "presence"))
        declaration.presence = presence->text;
      if (const auto *default_policy = OptionalProperty(properties, "default"))
        declaration.defaultPolicy = default_policy->text;
      if (const auto *narrowing = OptionalProperty(properties, "narrowing"))
        declaration.narrowing = narrowing->text;
      if (const auto *enum_name = OptionalProperty(properties, "enum"))
        declaration.enumName = enum_name->text;
      if (const auto *representation =
              OptionalProperty(properties, "representation"))
        declaration.representation = representation->text;
      if (const auto *targets = OptionalProperty(properties, "targets"))
        declaration.targets = targets->list;
      declaration.span = {begin.span.begin, Previous().span.end};
      return declaration;
    }

    RejectUnknownAttributes(attributes, {"semantic_type_id"});
    declaration.semanticTypeId =
        RequiredAttribute(attributes, "semantic_type_id", begin.span);
    declaration.semanticTypeIdExplicit = true;
    const auto properties = ParseProperties(
        {
            {"default", PropertyKind::Identifier},
            {"enum", PropertyKind::Identifier},
            {"fixed_size", PropertyKind::Integer},
            {"boundary_mapping", PropertyKind::Identifier},
            {"json_profile", PropertyKind::Identifier},
            {"kind", PropertyKind::Identifier},
            {"narrowing", PropertyKind::Identifier},
            {"normalization", PropertyKind::Identifier},
            {"precision", PropertyKind::Integer},
            {"presence", PropertyKind::Identifier},
            {"registry_key", PropertyKind::String},
            {"representation", PropertyKind::Identifier},
            {"scale", PropertyKind::Integer},
            {"semantic_name", PropertyKind::String},
            {"storage", PropertyKind::Identifier},
            {"targets", PropertyKind::IdentifierList},
            {"temporal_role", PropertyKind::Identifier},
            {"wire", PropertyKind::Identifier},
        },
        "semantic_type");
    const auto *kind = RequiredProperty(properties, "kind", begin.span);
    const auto *wire = RequiredProperty(properties, "wire", begin.span);
    const auto *storage = RequiredProperty(properties, "storage", begin.span);
    const auto *fixed_size =
        RequiredProperty(properties, "fixed_size", begin.span);
    const auto *precision =
        RequiredProperty(properties, "precision", begin.span);
    const auto *scale = RequiredProperty(properties, "scale", begin.span);
    const auto *normalization =
        RequiredProperty(properties, "normalization", begin.span);
    const auto *presence = RequiredProperty(properties, "presence", begin.span);
    const auto *default_policy =
        RequiredProperty(properties, "default", begin.span);
    const auto *narrowing =
        RequiredProperty(properties, "narrowing", begin.span);
    const auto *targets = RequiredProperty(properties, "targets", begin.span);
    const auto *enum_name = OptionalProperty(properties, "enum");
    if (kind != nullptr)
      declaration.kind = kind->text;
    if (wire != nullptr)
      declaration.wireTypeName = wire->text;
    if (storage != nullptr)
      declaration.storageCodecName = storage->text;
    if (fixed_size != nullptr)
      declaration.fixedSize = fixed_size->number;
    if (precision != nullptr)
      declaration.precision = precision->number;
    if (scale != nullptr)
      declaration.scale = scale->number;
    if (const auto *semantic_name =
            OptionalProperty(properties, "semantic_name"))
      declaration.semanticName = semantic_name->text;
    if (const auto *registry_key = OptionalProperty(properties, "registry_key"))
      declaration.registryKey = registry_key->text;
    if (normalization != nullptr)
      declaration.normalization = normalization->text;
    if (const auto *json_profile = OptionalProperty(properties, "json_profile"))
      declaration.jsonProfile = json_profile->text;
    if (const auto *boundary_mapping =
            OptionalProperty(properties, "boundary_mapping"))
      declaration.boundaryMapping = boundary_mapping->text;
    if (const auto *temporal_role =
            OptionalProperty(properties, "temporal_role"))
      declaration.temporalRole = temporal_role->text;
    if (presence != nullptr)
      declaration.presence = presence->text;
    if (default_policy != nullptr)
      declaration.defaultPolicy = default_policy->text;
    if (narrowing != nullptr)
      declaration.narrowing = narrowing->text;
    if (enum_name != nullptr)
      declaration.enumName = enum_name->text;
    if (const auto *representation =
            OptionalProperty(properties, "representation"))
      declaration.representation = representation->text;
    if (targets != nullptr)
      declaration.targets = targets->list;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  bool ParseModelProperty(ModelDeclaration &model,
                          std::set<std::string> &singular_properties) {
    if (!Check(TokenKind::Identifier))
      return false;
    const std::string name = Current().text;
    const bool repeated = name == "source_alias" || name == "retired_name" ||
                          name == "catalog_retired_name";
    const bool string_value =
        name == "description" || name == "name_review_rationale" ||
        name == "source_alias" || name == "catalog_retired_name";
    const bool known = string_value || name == "storage_name" ||
                       name == "domain" || name == "kind" ||
                       name == "retention" || name == "mutation_authority" ||
                       name == "role" || name == "retired_name" ||
                       name == "catalog_retired_name" || name == "name_review";
    if (!known)
      return false;

    const Token property = Advance();
    if (!repeated && !singular_properties.insert(name).second) {
      AddDiagnostic("idl.duplicate_model_property",
                    "Model repeats property " + name + ".", property.span);
    }
    std::string value;
    if (string_value) {
      if (const auto *token =
              Consume(TokenKind::String, "as the model property value"))
        value = token->decodedText;
    } else if (const auto *token = Consume(TokenKind::Identifier,
                                           "as the model property value")) {
      value = token->text;
    }
    Consume(TokenKind::Semicolon, "after model property");

    if (name == "storage_name")
      model.storageName = std::move(value);
    else if (name == "domain")
      model.domain = std::move(value);
    else if (name == "description")
      model.description = std::move(value);
    else if (name == "kind")
      model.kind = std::move(value);
    else if (name == "retention")
      model.retention = std::move(value);
    else if (name == "mutation_authority")
      model.mutationAuthority = std::move(value);
    else if (name == "role")
      model.role = std::move(value);
    else if (name == "source_alias")
      model.sourceAliases.push_back(std::move(value));
    else if (name == "retired_name")
      model.retiredNames.push_back(std::move(value));
    else if (name == "catalog_retired_name")
      model.catalogRetiredNames.push_back(std::move(value));
    else if (name == "name_review")
      model.nameReview = std::move(value);
    else if (name == "name_review_rationale")
      model.nameReviewRationale = std::move(value);
    return true;
  }

  void ValidateFabricProperties(const std::vector<std::string> &targets,
                                const ParsedProperty *fabric_lane,
                                const ParsedProperty *maximum_encoded_bytes,
                                const ParsedProperty *direct_eligible,
                                SourceSpan declaration_span) {
    const bool has_fabric =
        std::find(targets.begin(), targets.end(), "fabric") != targets.end();
    if (has_fabric) {
      for (const auto [property, name] : {
               std::pair{fabric_lane, std::string_view("fabric_lane")},
               std::pair{maximum_encoded_bytes,
                         std::string_view("maximum_encoded_bytes")},
               std::pair{direct_eligible, std::string_view("direct_eligible")},
           }) {
        if (property == nullptr)
          AddDiagnostic("idl.fabric_profile_property_missing",
                        "Fabric operation target requires property " +
                            std::string(name) + ".",
                        declaration_span);
      }
      return;
    }
    if (fabric_lane != nullptr || maximum_encoded_bytes != nullptr ||
        direct_eligible != nullptr)
      AddDiagnostic(
          "idl.fabric_profile_without_target",
          "Fabric operation properties require the fabric target profile.",
          declaration_span);
  }

  CommandDeclaration ParseCommand() {
    const Token begin = Advance();
    CommandDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the command name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"command_id"});
    declaration.commandId =
        RequiredAttribute(attributes, "command_id", begin.span);
    const auto properties = ParseProperties(
        {
            {"errors", PropertyKind::IdentifierList},
            {"direct_eligible", PropertyKind::Boolean},
            {"fabric_lane", PropertyKind::Identifier},
            {"idempotency", PropertyKind::Identifier},
            {"maximum_encoded_bytes", PropertyKind::Integer},
            {"request", PropertyKind::Identifier},
            {"response", PropertyKind::Identifier},
            {"streaming", PropertyKind::Identifier},
            {"targets", PropertyKind::IdentifierList},
        },
        "command");
    const auto *request = RequiredProperty(properties, "request", begin.span);
    const auto *response = RequiredProperty(properties, "response", begin.span);
    const auto *errors = RequiredProperty(properties, "errors", begin.span);
    const auto *idempotency =
        RequiredProperty(properties, "idempotency", begin.span);
    const auto *streaming =
        RequiredProperty(properties, "streaming", begin.span);
    const auto *targets = RequiredProperty(properties, "targets", begin.span);
    const auto *fabric_lane = OptionalProperty(properties, "fabric_lane");
    const auto *maximum_encoded_bytes =
        OptionalProperty(properties, "maximum_encoded_bytes");
    const auto *direct_eligible =
        OptionalProperty(properties, "direct_eligible");
    if (request != nullptr)
      declaration.requestModelName = request->text;
    if (response != nullptr)
      declaration.responseModelName = response->text;
    if (errors != nullptr)
      declaration.errorModelNames = errors->list;
    if (idempotency != nullptr)
      declaration.idempotency = idempotency->text;
    if (streaming != nullptr)
      declaration.streaming = streaming->text;
    if (targets != nullptr)
      declaration.targets = targets->list;
    if (fabric_lane != nullptr)
      declaration.fabricLane = fabric_lane->text;
    if (maximum_encoded_bytes != nullptr)
      declaration.maximumEncodedBytes = maximum_encoded_bytes->number;
    if (direct_eligible != nullptr)
      declaration.directEligible = direct_eligible->boolean;
    ValidateFabricProperties(declaration.targets, fabric_lane,
                             maximum_encoded_bytes, direct_eligible,
                             begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  EventDeclaration ParseEvent() {
    const Token begin = Advance();
    EventDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the event name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"event_id"});
    declaration.eventId = RequiredAttribute(attributes, "event_id", begin.span);
    const auto properties = ParseProperties(
        {
            {"delivery", PropertyKind::Identifier},
            {"direct_eligible", PropertyKind::Boolean},
            {"fabric_lane", PropertyKind::Identifier},
            {"maximum_encoded_bytes", PropertyKind::Integer},
            {"ordering", PropertyKind::Identifier},
            {"payload", PropertyKind::Identifier},
            {"replay", PropertyKind::Identifier},
            {"targets", PropertyKind::IdentifierList},
        },
        "event");
    const auto *payload = RequiredProperty(properties, "payload", begin.span);
    const auto *delivery = RequiredProperty(properties, "delivery", begin.span);
    const auto *ordering = RequiredProperty(properties, "ordering", begin.span);
    const auto *replay = RequiredProperty(properties, "replay", begin.span);
    const auto *targets = RequiredProperty(properties, "targets", begin.span);
    const auto *fabric_lane = OptionalProperty(properties, "fabric_lane");
    const auto *maximum_encoded_bytes =
        OptionalProperty(properties, "maximum_encoded_bytes");
    const auto *direct_eligible =
        OptionalProperty(properties, "direct_eligible");
    if (payload != nullptr)
      declaration.payloadModelName = payload->text;
    if (delivery != nullptr)
      declaration.delivery = delivery->text;
    if (ordering != nullptr)
      declaration.ordering = ordering->text;
    if (replay != nullptr)
      declaration.replay = replay->text;
    if (targets != nullptr)
      declaration.targets = targets->list;
    if (fabric_lane != nullptr)
      declaration.fabricLane = fabric_lane->text;
    if (maximum_encoded_bytes != nullptr)
      declaration.maximumEncodedBytes = maximum_encoded_bytes->number;
    if (direct_eligible != nullptr)
      declaration.directEligible = direct_eligible->boolean;
    ValidateFabricProperties(declaration.targets, fabric_lane,
                             maximum_encoded_bytes, direct_eligible,
                             begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  ModelDeclaration ParseModel(const Document &document) {
    const Token model_token = Advance();
    ModelDeclaration model;
    if (const auto *name = Consume(TokenKind::Identifier, "as the model name"))
      model.name = name->text;
    model.storageName = model.name;
    model.domain = document.schemaName;
    model.mutationAuthority = document.ownerCapsule;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"external", "type_id"});
    model.typeId = RequiredAttribute(attributes, "type_id", model_token.span);
    model.external = MarkerAttribute(attributes, "external", model_token.span);
    Consume(TokenKind::LeftBrace, "before model members");

    std::set<std::string> singular_properties;
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
      if (CheckNamedDeclaration("catalog_identity")) {
        if (model.catalogIdentity.has_value())
          AddDiagnostic("idl.duplicate_model_catalog_identity",
                        "Model repeats catalog_identity.", Current().span);
        model.catalogIdentity = ParseModelCatalogIdentity();
      } else if (Check(TokenKind::PrimaryId)) {
        if (model.fields.size() >= limits_.maximumFieldsPerModel) {
          AddDiagnostic(
              "idl.field_limit_exceeded",
              "IDL field count exceeds the configured per-model limit.",
              Current().span);
          SynchronizeModelEnd();
          break;
        }
        model.fields.push_back(ParsePrimaryId(model.name));
      } else if (Check(TokenKind::Field)) {
        if (model.fields.size() >= limits_.maximumFieldsPerModel) {
          AddDiagnostic(
              "idl.field_limit_exceeded",
              "IDL field count exceeds the configured per-model limit.",
              Current().span);
          SynchronizeModelEnd();
          break;
        }
        model.fields.push_back(ParseField());
      } else if (CheckNamedDeclaration("key")) {
        if (model.keys.size() >= limits_.maximumKeysPerModel) {
          AddDiagnostic("idl.key_limit_exceeded",
                        "Model key count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseKey());
        } else
          model.keys.push_back(ParseKey());
      } else if (CheckNamedDeclaration("index")) {
        if (model.indexes.size() >= limits_.maximumIndexesPerModel) {
          AddDiagnostic("idl.index_limit_exceeded",
                        "Model index count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseIndex());
        } else
          model.indexes.push_back(ParseIndex());
      } else if (CheckNamedDeclaration("relation")) {
        if (model.relations.size() >= limits_.maximumRelationsPerModel) {
          AddDiagnostic("idl.relation_limit_exceeded",
                        "Model relation count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseRelation());
        } else
          model.relations.push_back(ParseRelation());
      } else if (CheckNamedDeclaration("join")) {
        if (model.joins.size() >= limits_.maximumJoinsPerModel) {
          AddDiagnostic("idl.join_limit_exceeded",
                        "Model join count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseJoin());
        } else
          model.joins.push_back(ParseJoin());
      } else if (CheckNamedDeclaration("one_of")) {
        if (model.oneOfs.size() >= limits_.maximumOneOfsPerModel) {
          AddDiagnostic("idl.one_of_limit_exceeded",
                        "Model one_of count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseOneOf());
        } else
          model.oneOfs.push_back(ParseOneOf());
      } else if (CheckNamedDeclaration("temporal_contract")) {
        if (model.temporalContracts.size() >=
            limits_.maximumTemporalContractsPerModel) {
          AddDiagnostic(
              "idl.temporal_contract_limit_exceeded",
              "Model temporal contract count exceeds the configured limit.",
              Current().span);
          static_cast<void>(ParseTemporalContract());
        } else
          model.temporalContracts.push_back(ParseTemporalContract());
      } else if (Check(TokenKind::Reserved)) {
        ParseReservation(model.reservedIds, model.reservedNames,
                         RegistryKind::Field);
      } else if (ParseModelProperty(model, singular_properties)) {
      } else {
        AddDiagnostic("idl.unexpected_model_member",
                      "Expected a field, structural declaration, reservation, "
                      "or the end of the model.",
                      Current().span);
        SynchronizeModelMember();
      }
    }
    const Token *close = Consume(TokenKind::RightBrace, "after model members");
    model.span = {
        model_token.span.begin,
        close == nullptr ? Current().span.begin : close->span.end,
    };
    return model;
  }

  EnumValueDeclaration ParseEnumValue() {
    const Token begin = Advance();
    EnumValueDeclaration value;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the enum value name"))
      value.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"value_id"});
    value.valueId = RequiredAttribute(attributes, "value_id", begin.span);
    if (Match(TokenKind::Semicolon)) {
      value.span = {begin.span.begin, Previous().span.end};
      return value;
    }
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
        },
        "enum value");
    value.catalogIdentity = ParseCatalogMemberIdentity(properties, begin.span);
    value.span = {begin.span.begin, Previous().span.end};
    return value;
  }

  EnumDeclaration ParseEnum() {
    const Token begin = Advance();
    EnumDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the enum name"))
      declaration.name = name->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"enum_id"});
    declaration.enumId = RequiredAttribute(attributes, "enum_id", begin.span);
    Consume(TokenKind::LeftBrace, "before enum members");
    bool has_semantic_type = false;
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
      if ((currentEdition_ == 1 && CheckNamedDeclaration("semantic_type")) ||
          (currentEdition_ == 2 && CheckNamedDeclaration("type"))) {
        const Token property = Advance();
        if (has_semantic_type)
          AddDiagnostic("idl.duplicate_enum_property",
                        "Enum repeats its governed type.", property.span);
        has_semantic_type = true;
        if (const auto *type =
                Consume(TokenKind::Identifier, "as the enum semantic type"))
          declaration.semanticTypeName = type->text;
        Consume(TokenKind::Semicolon, "after enum type");
      } else if (CheckNamedDeclaration("value")) {
        if (declaration.values.size() >= limits_.maximumEnumValuesPerEnum) {
          AddDiagnostic("idl.enum_value_limit_exceeded",
                        "Enum value count exceeds the configured limit.",
                        Current().span);
          SynchronizeEnumEnd();
          break;
        }
        declaration.values.push_back(ParseEnumValue());
      } else if (Check(TokenKind::Reserved)) {
        ParseReservation(declaration.reservedIds, declaration.reservedNames,
                         RegistryKind::EnumValue);
      } else {
        AddDiagnostic("idl.unexpected_enum_member",
                      "Expected type, value, reserved, or the end of the enum.",
                      Current().span);
        SynchronizeEnumMember();
      }
    }
    const Token *close = Consume(TokenKind::RightBrace, "after enum members");
    declaration.span = {begin.span.begin, close == nullptr
                                              ? Current().span.begin
                                              : close->span.end};
    return declaration;
  }

  KeyDeclaration ParseKey() {
    const Token begin = Advance();
    KeyDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the key name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"fields", PropertyKind::IdentifierList},
            {"kind", PropertyKind::Identifier},
        },
        "key");
    const auto *kind = RequiredProperty(properties, "kind", begin.span);
    const auto *fields = RequiredProperty(properties, "fields", begin.span);
    if (kind != nullptr)
      declaration.kind = kind->text;
    if (fields != nullptr)
      declaration.fields = fields->list;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  IndexDeclaration ParseIndex() {
    const Token begin = Advance();
    IndexDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the index name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"fields", PropertyKind::IdentifierList},
            {"unique", PropertyKind::Boolean},
        },
        "index");
    const auto *fields = RequiredProperty(properties, "fields", begin.span);
    const auto *unique = RequiredProperty(properties, "unique", begin.span);
    if (fields != nullptr)
      declaration.fields = fields->list;
    if (unique != nullptr)
      declaration.unique = unique->boolean;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  RelationDeclaration ParseRelation() {
    const Token begin = Advance();
    RelationDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the relation name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"cardinality", PropertyKind::Identifier},
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"on_delete", PropertyKind::Identifier},
            {"on_update", PropertyKind::Identifier},
            {"source_fields", PropertyKind::IdentifierList},
            {"target", PropertyKind::String},
            {"target_fields", PropertyKind::IdentifierList},
        },
        "relation");
    const auto *target = RequiredProperty(properties, "target", begin.span);
    const auto *source_fields =
        RequiredProperty(properties, "source_fields", begin.span);
    const auto *target_fields =
        RequiredProperty(properties, "target_fields", begin.span);
    const auto *cardinality =
        RequiredProperty(properties, "cardinality", begin.span);
    const auto *on_delete =
        RequiredProperty(properties, "on_delete", begin.span);
    const auto *on_update =
        RequiredProperty(properties, "on_update", begin.span);
    if (target != nullptr)
      declaration.targetModel = target->text;
    if (source_fields != nullptr)
      declaration.sourceFields = source_fields->list;
    if (target_fields != nullptr)
      declaration.targetFields = target_fields->list;
    if (cardinality != nullptr)
      declaration.cardinality = cardinality->text;
    if (on_delete != nullptr)
      declaration.onDelete = on_delete->text;
    if (on_update != nullptr)
      declaration.onUpdate = on_update->text;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  JoinDeclaration ParseJoin() {
    const Token begin = Advance();
    JoinDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the join name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"source_relation", PropertyKind::Identifier},
            {"target_relation", PropertyKind::Identifier},
            {"through", PropertyKind::String},
        },
        "join");
    const auto *through = RequiredProperty(properties, "through", begin.span);
    const auto *source =
        RequiredProperty(properties, "source_relation", begin.span);
    const auto *target =
        RequiredProperty(properties, "target_relation", begin.span);
    if (through != nullptr)
      declaration.throughModel = through->text;
    if (source != nullptr)
      declaration.sourceRelation = source->text;
    if (target != nullptr)
      declaration.targetRelation = target->text;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  OneOfVariantDeclaration ParseOneOfVariant() {
    const Token begin = Advance();
    OneOfVariantDeclaration declaration;
    if (const auto *tag =
            Consume(TokenKind::Identifier, "as the one_of variant tag"))
      declaration.tag = tag->text;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"fields", PropertyKind::IdentifierList},
            {"optional_fields", PropertyKind::IdentifierList},
        },
        "one_of variant");
    if (const auto *fields = OptionalProperty(properties, "fields"))
      declaration.fields = fields->list;
    if (const auto *fields = OptionalProperty(properties, "optional_fields"))
      declaration.optionalFields = fields->list;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  OneOfDeclaration ParseOneOf() {
    const Token begin = Advance();
    OneOfDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the one_of name"))
      declaration.name = name->text;
    Consume(TokenKind::LeftBrace, "before one_of members");
    bool has_discriminator = false;
    const Token *catalog_name = nullptr;
    const Token *catalog_order = nullptr;
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
      if (CheckNamedDeclaration("discriminator")) {
        const Token property = Advance();
        if (has_discriminator)
          AddDiagnostic("idl.duplicate_one_of_property",
                        "One_of repeats discriminator.", property.span);
        has_discriminator = true;
        if (const auto *field =
                Consume(TokenKind::Identifier, "as the discriminator field"))
          declaration.discriminatorField = field->text;
        Consume(TokenKind::Semicolon, "after one_of discriminator");
      } else if (CheckNamedDeclaration("catalog_name")) {
        const Token property = Advance();
        if (catalog_name != nullptr)
          AddDiagnostic("idl.duplicate_type_property",
                        "One_of repeats catalog_name.", property.span);
        catalog_name = Consume(TokenKind::String, "as the one_of catalog name");
        Consume(TokenKind::Semicolon, "after one_of catalog name");
      } else if (CheckNamedDeclaration("catalog_order")) {
        const Token property = Advance();
        if (catalog_order != nullptr)
          AddDiagnostic("idl.duplicate_type_property",
                        "One_of repeats catalog_order.", property.span);
        catalog_order =
            Consume(TokenKind::Integer, "as the one_of catalog order");
        Consume(TokenKind::Semicolon, "after one_of catalog order");
      } else if (CheckNamedDeclaration("variant")) {
        if (declaration.variants.size() >= limits_.maximumOneOfVariants) {
          AddDiagnostic("idl.one_of_variant_limit_exceeded",
                        "One_of variant count exceeds the configured limit.",
                        Current().span);
          static_cast<void>(ParseOneOfVariant());
        } else
          declaration.variants.push_back(ParseOneOfVariant());
      } else {
        AddDiagnostic("idl.unexpected_one_of_member",
                      "Expected catalog identity, discriminator, variant, or "
                      "the end of the one_of.",
                      Current().span);
        while (!Check(TokenKind::End) && !Check(TokenKind::RightBrace) &&
               !CheckNamedDeclaration("catalog_name") &&
               !CheckNamedDeclaration("catalog_order") &&
               !CheckNamedDeclaration("discriminator") &&
               !CheckNamedDeclaration("variant"))
          Advance();
      }
    }
    const Token *close = Consume(TokenKind::RightBrace, "after one_of members");
    if (!has_discriminator)
      AddDiagnostic("idl.required_one_of_property_missing",
                    "One_of requires discriminator.", begin.span);
    if ((catalog_name == nullptr) != (catalog_order == nullptr))
      AddDiagnostic("idl.catalog_member_identity_incomplete",
                    "Catalog member identity requires both catalog_name and "
                    "catalog_order.",
                    begin.span);
    else if (catalog_name != nullptr)
      declaration.catalogIdentity = CatalogMemberIdentityDeclaration{
          .name = catalog_name->decodedText,
          .declarationOrder = ParseU32(catalog_order, "Catalog order"),
          .span = {catalog_name->span.begin, catalog_order->span.end},
      };
    declaration.span = {begin.span.begin, close == nullptr
                                              ? Current().span.begin
                                              : close->span.end};
    return declaration;
  }

  TemporalContractDeclaration ParseTemporalContract() {
    const Token begin = Advance();
    TemporalContractDeclaration declaration;
    if (const auto *name =
            Consume(TokenKind::Identifier, "as the temporal contract name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"end_field", PropertyKind::Identifier},
            {"end_inclusive", PropertyKind::Boolean},
            {"open_end", PropertyKind::Identifier},
            {"ordering", PropertyKind::Identifier},
            {"start_field", PropertyKind::Identifier},
            {"start_inclusive", PropertyKind::Boolean},
        },
        "temporal_contract");
    const auto *start = RequiredProperty(properties, "start_field", begin.span);
    const auto *end = RequiredProperty(properties, "end_field", begin.span);
    const auto *ordering = RequiredProperty(properties, "ordering", begin.span);
    const auto *open_end = RequiredProperty(properties, "open_end", begin.span);
    const auto *start_inclusive =
        RequiredProperty(properties, "start_inclusive", begin.span);
    const auto *end_inclusive =
        RequiredProperty(properties, "end_inclusive", begin.span);
    if (start != nullptr)
      declaration.startField = start->text;
    if (end != nullptr)
      declaration.endField = end->text;
    if (ordering != nullptr)
      declaration.ordering = ordering->text;
    if (open_end != nullptr)
      declaration.openEnd = open_end->text;
    if (start_inclusive != nullptr)
      declaration.startInclusive = start_inclusive->boolean;
    if (end_inclusive != nullptr)
      declaration.endInclusive = end_inclusive->boolean;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  FieldDeclaration ParsePrimaryId(std::string_view model_name) {
    const Token begin_token = Advance();
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes, {"field_id"});
    const std::string name = std::string(model_name) + "_id";
    FieldDeclaration field{
        .name = name,
        .storageName = name,
        .typeName = name,
        .fieldId = RequiredAttribute(attributes, "field_id", begin_token.span),
        .minimum = std::nullopt,
        .maximumItems = std::nullopt,
        .optional = false,
        .sourceProvenance = "boundary_generated",
        .required = true,
        .redacted = false,
        .immutable = true,
        .writeOnce = true,
        .primary = true,
    };
    if (Match(TokenKind::Semicolon)) {
    } else if (Check(TokenKind::LeftBrace)) {
      Consume(TokenKind::LeftBrace, "before primary_id members");
      while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        if (CheckNamedDeclaration("catalog_identity")) {
          if (field.catalogIdentity.has_value())
            AddDiagnostic("idl.duplicate_field_catalog_identity",
                          "Primary ID repeats catalog_identity.",
                          Current().span);
          field.catalogIdentity = ParseFieldCatalogIdentity();
        } else {
          AddDiagnostic("idl.unexpected_primary_id_member",
                        "Expected catalog_identity or the end of primary_id.",
                        Current().span);
          while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End) &&
                 !CheckNamedDeclaration("catalog_identity"))
            Advance();
        }
      }
      Consume(TokenKind::RightBrace, "after primary_id members");
    } else {
      Consume(TokenKind::Semicolon,
              "or a property block after primary_id declaration");
    }
    field.span = {begin_token.span.begin, Previous().span.end};
    return field;
  }

  FieldDefaultDeclaration ParseFieldDefault() {
    const Token begin = Advance();
    FieldDefaultDeclaration declaration;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"catalog_value", PropertyKind::String},
            {"kind", PropertyKind::Identifier},
            {"source", PropertyKind::Identifier},
            {"value", PropertyKind::String},
        },
        "field default");
    const auto *kind = RequiredProperty(properties, "kind", begin.span);
    if (kind != nullptr)
      declaration.kind = kind->text;
    const auto *value = OptionalProperty(properties, "value");
    const auto *source = OptionalProperty(properties, "source");
    const auto *catalog_value = OptionalProperty(properties, "catalog_value");
    if (declaration.kind == "literal") {
      if (value == nullptr)
        AddDiagnostic("idl.required_type_property_missing",
                      "Literal default requires value.", begin.span);
      else
        declaration.literal = value->text;
      if (source != nullptr)
        AddDiagnostic("idl.field_default_property_invalid",
                      "Literal default cannot declare source.", source->span);
      if (catalog_value != nullptr)
        AddDiagnostic("idl.field_default_property_invalid",
                      "Literal default cannot declare catalog_value.",
                      catalog_value->span);
    } else if (declaration.kind == "boundary") {
      if (source == nullptr)
        AddDiagnostic("idl.required_type_property_missing",
                      "Boundary default requires source.", begin.span);
      else
        declaration.boundarySource = source->text;
      if (catalog_value != nullptr)
        declaration.catalogValue = catalog_value->text;
      if (value != nullptr)
        AddDiagnostic("idl.field_default_property_invalid",
                      "Boundary default cannot declare value.", value->span);
    } else if (kind != nullptr) {
      AddDiagnostic("idl.field_default_kind_invalid",
                    "Field default kind must be literal or boundary.",
                    kind->span);
    }
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  CheckDeclaration ParseFieldCheck() {
    const Token begin = Advance();
    CheckDeclaration declaration;
    if (const auto *name = Consume(TokenKind::Identifier, "as the check name"))
      declaration.name = name->text;
    const auto properties = ParseProperties(
        {
            {"catalog_name", PropertyKind::String},
            {"catalog_order", PropertyKind::Integer},
            {"kind", PropertyKind::Identifier},
            {"message", PropertyKind::String},
            {"unit", PropertyKind::Identifier},
            {"value", PropertyKind::String},
        },
        "field check");
    const auto *kind = RequiredProperty(properties, "kind", begin.span);
    const auto *value = RequiredProperty(properties, "value", begin.span);
    if (kind != nullptr)
      declaration.kind = kind->text;
    if (const auto *unit = OptionalProperty(properties, "unit"))
      declaration.unit = unit->text;
    if (value != nullptr)
      declaration.value = value->text;
    if (const auto *message = OptionalProperty(properties, "message"))
      declaration.message = message->text;
    declaration.catalogIdentity =
        ParseCatalogMemberIdentity(properties, begin.span);
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  QueryPolicyDeclaration ParseFieldQuery() {
    const Token begin = Advance();
    QueryPolicyDeclaration declaration;
    const auto properties = ParseProperties(
        {
            {"cursor", PropertyKind::Boolean},
            {"operators", PropertyKind::IdentifierList},
            {"sortable", PropertyKind::Boolean},
        },
        "field query");
    const auto *sortable =
        currentEdition_ == 1
            ? RequiredProperty(properties, "sortable", begin.span)
            : OptionalProperty(properties, "sortable");
    const auto *cursor =
        currentEdition_ == 1
            ? RequiredProperty(properties, "cursor", begin.span)
            : OptionalProperty(properties, "cursor");
    if (const auto *operators = OptionalProperty(properties, "operators"))
      declaration.operators = operators->list;
    if (sortable != nullptr)
      declaration.sortable = sortable->boolean;
    if (cursor != nullptr)
      declaration.cursor = cursor->boolean;
    declaration.span = {begin.span.begin, Previous().span.end};
    return declaration;
  }

  bool ParseFieldProperty(FieldDeclaration &field,
                          std::set<std::string> &seen) {
    if (!Check(TokenKind::Identifier))
      return false;
    const std::string name = Current().text;
    const bool known = name == "allowed_enum_values" || name == "immutable" ||
                       name == "cpp_name" || name == "csharp_name" ||
                       name == "import_transform" || name == "presence" ||
                       name == "redacted" || name == "serialization" ||
                       name == "source_field" || name == "source_provenance" ||
                       name == "storage_name" || name == "temporal_role" ||
                       name == "write_once";
    if (!known)
      return false;
    const Token property = Advance();
    if (!seen.insert(name).second)
      AddDiagnostic("idl.duplicate_type_property",
                    "Field repeats property " + name + ".", property.span);

    std::string value;
    std::vector<std::string> values;
    bool boolean = false;
    if (name == "allowed_enum_values") {
      const auto append_value = [this, &values] {
        if (const auto *token =
                Consume(TokenKind::Identifier, "as an allowed enum value")) {
          if (values.size() >= limits_.maximumEnumValuesPerEnum)
            AddDiagnostic(
                "idl.enum_value_limit_exceeded",
                "Field allowed enum value count exceeds the configured limit.",
                token->span);
          else
            values.push_back(token->text);
        }
      };
      append_value();
      while (Match(TokenKind::Comma))
        append_value();
    } else if (name == "source_field") {
      if (const auto *token =
              Consume(TokenKind::String, "as the field property value"))
        value = token->decodedText;
    } else if (const auto *token = Consume(TokenKind::Identifier,
                                           "as the field property value")) {
      value = token->text;
      if (name == "immutable" || name == "redacted" || name == "write_once") {
        if (value == "true")
          boolean = true;
        else if (value != "false")
          AddDiagnostic("idl.boolean_invalid",
                        "Boolean property must be true or false.", token->span);
      }
    }
    Consume(TokenKind::Semicolon, "after field property");

    if (name == "allowed_enum_values")
      field.allowedEnumValues = std::move(values);
    else if (name == "cpp_name")
      field.cppName = std::move(value);
    else if (name == "csharp_name")
      field.csharpName = std::move(value);
    else if (name == "storage_name")
      field.storageName = std::move(value);
    else if (name == "presence") {
      if (value == "required") {
        field.required = true;
        field.optional = false;
      } else if (value == "optional") {
        field.required = false;
        field.optional = true;
      } else
        AddDiagnostic("idl.field_presence_invalid",
                      "Field presence must be required or optional.",
                      property.span);
    } else if (name == "redacted")
      field.redacted = boolean;
    else if (name == "serialization")
      field.serialization = std::move(value);
    else if (name == "immutable")
      field.immutable = boolean;
    else if (name == "write_once")
      field.writeOnce = boolean;
    else if (name == "source_field")
      field.sourceField = std::move(value);
    else if (name == "source_provenance")
      field.sourceProvenance = std::move(value);
    else if (name == "import_transform")
      field.importTransform = std::move(value);
    else if (name == "temporal_role")
      field.temporalRole = std::move(value);
    return true;
  }

  FieldDeclaration ParseField() {
    const Token begin_token = Advance();
    FieldDeclaration field;
    if (const auto *name = Consume(TokenKind::Identifier, "as the field name"))
      field.name = name->text;
    field.storageName = field.name;
    Consume(TokenKind::Colon, "between field name and type");
    if (const auto *type = Consume(TokenKind::Identifier, "as the field type"))
      field.typeName = type->text;
    const auto attributes = ParseAttributes();
    RejectUnknownAttributes(attributes,
                            {"field_id", "max_items", "min", "optional"});
    field.fieldId = RequiredAttribute(attributes, "field_id", begin_token.span);
    std::size_t minimum_count = 0;
    std::size_t maximum_items_count = 0;
    std::size_t optional_count = 0;
    for (const auto &attribute : attributes) {
      if (attribute.name == "min") {
        if (attribute.hasValue)
          field.minimum = attribute.value;
        else
          AddDiagnostic("idl.attribute_value_required",
                        "Attribute @min requires an integer value.",
                        attribute.span);
        ++minimum_count;
      } else if (attribute.name == "max_items") {
        if (attribute.hasValue)
          field.maximumItems = attribute.value;
        else
          AddDiagnostic("idl.attribute_value_required",
                        "Attribute @max_items requires an integer value.",
                        attribute.span);
        ++maximum_items_count;
      } else if (attribute.name == "optional") {
        if (attribute.hasValue)
          AddDiagnostic("idl.flag_attribute_value_forbidden",
                        "Flag attribute @optional does not accept a value.",
                        attribute.span);
        field.optional = true;
        field.required = false;
        ++optional_count;
      }
    }
    if (minimum_count > 1)
      AddDiagnostic("idl.duplicate_attribute", "Declaration repeats @min.",
                    begin_token.span);
    if (maximum_items_count > 1)
      AddDiagnostic("idl.duplicate_attribute",
                    "Declaration repeats @max_items.", begin_token.span);
    if (optional_count > 1)
      AddDiagnostic("idl.duplicate_attribute", "Declaration repeats @optional.",
                    begin_token.span);
    if (field.optional && field.maximumItems.has_value())
      AddDiagnostic("idl.field_cardinality_conflict",
                    "Field cannot be both optional and repeated.",
                    begin_token.span);
    if (field.maximumItems == 0)
      AddDiagnostic("idl.maximum_items_invalid",
                    "Repeated fields require @max_items greater than zero.",
                    begin_token.span);

    if (Match(TokenKind::Semicolon)) {
    } else if (Check(TokenKind::LeftBrace)) {
      Consume(TokenKind::LeftBrace, "before field members");
      std::set<std::string> singular;
      while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        if (CheckNamedDeclaration("default")) {
          if (!singular.insert("default").second)
            AddDiagnostic("idl.duplicate_field_default",
                          "Field repeats default.", Current().span);
          field.defaultValue = ParseFieldDefault();
        } else if (CheckNamedDeclaration("check")) {
          if (field.checks.size() >= limits_.maximumChecksPerField) {
            AddDiagnostic("idl.field_check_limit_exceeded",
                          "Field check count exceeds the configured limit.",
                          Current().span);
            static_cast<void>(ParseFieldCheck());
          } else
            field.checks.push_back(ParseFieldCheck());
        } else if (CheckNamedDeclaration("query")) {
          if (!singular.insert("query").second)
            AddDiagnostic("idl.duplicate_field_query", "Field repeats query.",
                          Current().span);
          field.query = ParseFieldQuery();
        } else if (CheckNamedDeclaration("catalog_identity")) {
          if (!singular.insert("catalog_identity").second)
            AddDiagnostic("idl.duplicate_field_catalog_identity",
                          "Field repeats catalog_identity.", Current().span);
          field.catalogIdentity = ParseFieldCatalogIdentity();
        } else if (CheckNamedDeclaration("representation")) {
          if (!singular.insert("representation").second)
            AddDiagnostic("idl.duplicate_field_representation",
                          "Field repeats representation.", Current().span);
          field.representation = ParseFieldRepresentation();
        } else if (!ParseFieldProperty(field, singular)) {
          AddDiagnostic("idl.unexpected_field_member",
                        "Expected a field property or policy declaration.",
                        Current().span);
          while (!Check(TokenKind::Semicolon) &&
                 !Check(TokenKind::RightBrace) && !Check(TokenKind::End))
            Advance();
          Match(TokenKind::Semicolon);
        }
      }
      Consume(TokenKind::RightBrace, "after field members");
    } else {
      Consume(TokenKind::Semicolon,
              "or a property block after field declaration");
    }
    field.span = {
        begin_token.span.begin,
        Previous().span.end,
    };
    return field;
  }

  void SynchronizeTopLevel() {
    while (!Check(TokenKind::End) && !Check(TokenKind::Model) &&
           !Check(TokenKind::Reserved) && !CheckNamedDeclaration("wire_type") &&
           !CheckNamedDeclaration("storage_codec") &&
           !CheckNamedDeclaration("semantic_type") &&
           !CheckNamedDeclaration("enum") &&
           !CheckNamedDeclaration("command") && !CheckNamedDeclaration("event"))
      Advance();
  }

  void SynchronizeModelMember() {
    while (!Check(TokenKind::End) && !Check(TokenKind::RightBrace) &&
           !Check(TokenKind::PrimaryId) && !Check(TokenKind::Field) &&
           !Check(TokenKind::Reserved) && !CheckNamedDeclaration("key") &&
           !CheckNamedDeclaration("index") &&
           !CheckNamedDeclaration("relation") &&
           !CheckNamedDeclaration("join") && !CheckNamedDeclaration("one_of") &&
           !CheckNamedDeclaration("temporal_contract") &&
           !CheckNamedDeclaration("catalog_identity")) {
      Advance();
    }
  }

  void SynchronizeEnumMember() {
    while (!Check(TokenKind::End) && !Check(TokenKind::RightBrace) &&
           !Check(TokenKind::Reserved) &&
           !CheckNamedDeclaration("semantic_type") &&
           !CheckNamedDeclaration("type") && !CheckNamedDeclaration("value"))
      Advance();
  }

  void SynchronizeEnumEnd() {
    while (!Check(TokenKind::End) && !Check(TokenKind::RightBrace))
      Advance();
  }

  void SynchronizeModelEnd() {
    while (!Check(TokenKind::End) && !Check(TokenKind::RightBrace))
      Advance();
  }

  void ValidateName(std::string_view name, std::string_view kind,
                    SourceSpan span) {
    if (!IsCanonicalSnakeCase(name)) {
      AddDiagnostic("idl.noncanonical_name",
                    std::string(kind) + " must use lowercase ASCII snake_case.",
                    span);
    }
  }

  void Validate(Document &document) {
    if (document.edition == 2) {
      if (!document.wireTypes.empty() || !document.storageCodecs.empty())
        AddDiagnostic("idl.v2_backend_declaration_forbidden",
                      "Schema V2 synthesizes wire primitives and keeps backend "
                      "storage profiles outside authored schema.",
                      tokens_.front().span);
      for (auto &declaration : document.semanticTypes) {
        if (declaration.semanticTypeId != 0u)
          continue;
        const std::string canonical_identity =
            declaration.registryKey.empty()
                ? document.schemaName + ".v" +
                      std::to_string(document.schemaVersion) + "." +
                      declaration.name
                : declaration.registryKey;
        declaration.semanticTypeId = StableRegistryId(
            declaration.registryKey.empty() ? "arken.schema.semantic-type.v2"
                                            : "arken.schema.semantic-type.v1",
            canonical_identity);
      }
    }
    if (document.edition != 1 && document.edition != 2) {
      AddDiagnostic(
          "idl.unsupported_edition",
          "Only Arken Schema language versions 1 and 2 are supported.",
          tokens_.front().span);
    }
    if (document.schemaVersion == 0) {
      AddDiagnostic("idl.zero_schema_version",
                    "Schema version must be nonzero.", tokens_.front().span);
    }
    ValidateName(document.schemaName, "Schema name", tokens_.front().span);
    ValidateName(document.ownerCapsule, "Owner capsule", tokens_.front().span);

    if (document.catalog.has_value()) {
      auto &catalog = *document.catalog;
      if (catalog.catalogId.empty())
        AddDiagnostic("idl.catalog_id_empty", "Catalog ID must be nonempty.",
                      catalog.span);
      if (catalog.version == 0)
        AddDiagnostic("idl.catalog_version_zero",
                      "Catalog version must be nonzero.", catalog.span);
      std::map<std::string, SourceSpan> fragment_ids;
      std::map<std::uint32_t, SourceSpan> fragment_orders;
      for (const auto &fragment : catalog.fragments) {
        if (fragment.fragmentId.empty())
          AddDiagnostic("idl.catalog_fragment_id_empty",
                        "Catalog fragment ID must be nonempty.", fragment.span);
        else if (const auto [existing, inserted] =
                     fragment_ids.emplace(fragment.fragmentId, fragment.span);
                 !inserted)
          AddDiagnostic(
              "idl.duplicate_catalog_fragment_id",
              "Catalog fragment IDs must be unique.", fragment.span,
              {{
                  .message = "The first fragment using this ID is here.",
                  .sourceName = sourceName_,
                  .span = existing->second,
              }});
        ValidateName(fragment.ownerCapsule, "Catalog fragment owner capsule",
                     fragment.span);
        if (fragment.version == 0)
          AddDiagnostic("idl.catalog_fragment_version_zero",
                        "Catalog fragment version must be nonzero.",
                        fragment.span);
        if (const auto [existing, inserted] = fragment_orders.emplace(
                fragment.declarationOrder, fragment.span);
            !inserted)
          AddDiagnostic("idl.duplicate_catalog_fragment_order",
                        "Catalog fragment declaration orders must be unique.",
                        fragment.span,
                        {{
                            .message = "The first fragment using this "
                                       "declaration order is here.",
                            .sourceName = sourceName_,
                            .span = existing->second,
                        }});
      }
      std::uint32_t expected_order = 0;
      for (const auto &[order, span] : fragment_orders) {
        if (order != expected_order) {
          AddDiagnostic("idl.catalog_fragment_order_not_contiguous",
                        "Catalog fragment declaration orders must be "
                        "contiguous from zero.",
                        span);
          break;
        }
        ++expected_order;
      }
      std::sort(catalog.fragments.begin(), catalog.fragments.end(),
                [](const auto &lhs, const auto &rhs) {
                  if (lhs.declarationOrder != rhs.declarationOrder)
                    return lhs.declarationOrder < rhs.declarationOrder;
                  return lhs.fragmentId < rhs.fragmentId;
                });
    }

    std::map<std::string, SourceSpan> import_paths;
    for (const auto &import : document.imports) {
      const auto [existing, inserted] =
          import_paths.emplace(import.path, import.pathSpan);
      if (!inserted) {
        AddDiagnostic("idl.duplicate_import",
                      "Import path must be unique within its source file.",
                      import.pathSpan,
                      {{
                          .message = "The first import is here.",
                          .sourceName = sourceName_,
                          .span = existing->second,
                      }});
      }
    }

    std::map<std::string, SourceSpan> model_names;
    std::map<std::uint32_t, SourceSpan> model_ids;
    std::set<std::pair<RegistryKind, std::uint32_t>> reserved_ids;
    std::set<std::pair<RegistryKind, std::string>> reserved_names;
    for (const auto &reserved : document.reservedIds) {
      if (reserved.registry == RegistryKind::Field ||
          reserved.registry == RegistryKind::EnumValue) {
        AddDiagnostic("idl.reservation_scope_invalid",
                      "Field and enum_value identities must be reserved inside "
                      "their owners.",
                      reserved.span);
      }
      if (reserved.value == 0) {
        AddDiagnostic("idl.zero_reserved_id",
                      "Reserved numeric identity must be nonzero.",
                      reserved.span);
      } else if (!reserved_ids.emplace(reserved.registry, reserved.value)
                      .second) {
        AddDiagnostic("idl.duplicate_reserved_id",
                      "Reserved numeric identity must be unique within its "
                      "registry scope.",
                      reserved.span);
      }
    }
    for (const auto &reserved : document.reservedNames) {
      if (reserved.registry == RegistryKind::Field ||
          reserved.registry == RegistryKind::EnumValue) {
        AddDiagnostic("idl.reservation_scope_invalid",
                      "Field and enum_value identities must be reserved inside "
                      "their owners.",
                      reserved.span);
      }
      ValidateName(reserved.value, "Reserved name", reserved.span);
      if (!reserved_names.emplace(reserved.registry, reserved.value).second) {
        AddDiagnostic("idl.duplicate_reserved_name",
                      "Reserved name must be unique within its registry scope.",
                      reserved.span);
      }
    }
    std::sort(document.reservedIds.begin(), document.reservedIds.end(),
              [](const auto &lhs, const auto &rhs) {
                return std::tie(lhs.registry, lhs.value) <
                       std::tie(rhs.registry, rhs.value);
              });
    std::sort(document.reservedNames.begin(), document.reservedNames.end(),
              [](const auto &lhs, const auto &rhs) {
                return std::tie(lhs.registry, lhs.value) <
                       std::tie(rhs.registry, rhs.value);
              });

    const auto validate_type_declarations =
        [this, &reserved_ids,
         &reserved_names](auto &declarations, RegistryKind registry,
                          std::string_view label, auto id_projection) {
          std::map<std::uint32_t, SourceSpan> ids;
          std::map<std::string, SourceSpan> names;
          for (const auto &declaration : declarations) {
            ValidateName(declaration.name, label, declaration.span);
            const auto id = id_projection(declaration);
            if (id == 0) {
              AddDiagnostic("idl.zero_type_id",
                            std::string(label) + " numeric ID must be nonzero.",
                            declaration.span);
            } else if (const auto [existing, inserted] =
                           ids.emplace(id, declaration.span);
                       !inserted) {
              AddDiagnostic("idl.duplicate_type_id",
                            std::string(label) +
                                " numeric ID must be unique within its source.",
                            declaration.span,
                            {{.message = "The first declaration is here.",
                              .sourceName = sourceName_,
                              .span = existing->second}});
            }
            if (const auto [existing, inserted] =
                    names.emplace(declaration.name, declaration.span);
                !inserted) {
              AddDiagnostic("idl.duplicate_type_name",
                            std::string(label) +
                                " name must be unique within its source.",
                            declaration.span,
                            {{.message = "The first declaration is here.",
                              .sourceName = sourceName_,
                              .span = existing->second}});
            }
            if (reserved_ids.contains({registry, id}))
              AddDiagnostic("idl.reserved_type_id_active",
                            "Active type ID cannot also be reserved.",
                            declaration.span);
            if (reserved_names.contains({registry, declaration.name}))
              AddDiagnostic("idl.reserved_type_name_active",
                            "Active type name cannot also be reserved.",
                            declaration.span);
          }
          std::sort(declarations.begin(), declarations.end(),
                    [&id_projection](const auto &lhs, const auto &rhs) {
                      const auto lhs_id = id_projection(lhs);
                      const auto rhs_id = id_projection(rhs);
                      return lhs_id == rhs_id ? lhs.name < rhs.name
                                              : lhs_id < rhs_id;
                    });
        };
    validate_type_declarations(
        document.wireTypes, RegistryKind::WireType, "Wire type",
        [](const auto &declaration) { return declaration.wireTypeId; });
    validate_type_declarations(
        document.storageCodecs, RegistryKind::StorageCodec, "Storage codec",
        [](const auto &declaration) { return declaration.storageCodecId; });
    validate_type_declarations(
        document.semanticTypes, RegistryKind::SemanticType, "Semantic type",
        [](const auto &declaration) { return declaration.semanticTypeId; });
    std::map<std::uint32_t, SourceSpan> enum_ids;
    std::map<std::string, SourceSpan> enum_names;
    for (auto &declaration : document.enums) {
      ValidateName(declaration.name, "Enum name", declaration.span);
      if (!declaration.semanticTypeName.empty())
        ValidateName(declaration.semanticTypeName, "Enum semantic type",
                     declaration.span);
      if (declaration.enumId == 0)
        AddDiagnostic("idl.zero_enum_id", "Enum ID must be nonzero.",
                      declaration.span);
      else if (!enum_ids.emplace(declaration.enumId, declaration.span).second)
        AddDiagnostic("idl.duplicate_enum_id", "Enum ID must be unique.",
                      declaration.span);
      if (!enum_names.emplace(declaration.name, declaration.span).second)
        AddDiagnostic("idl.duplicate_enum_name", "Enum name must be unique.",
                      declaration.span);
      if (reserved_ids.contains({RegistryKind::Enum, declaration.enumId}))
        AddDiagnostic("idl.reserved_enum_id_active",
                      "Active enum ID cannot also be reserved.",
                      declaration.span);
      if (reserved_names.contains({RegistryKind::Enum, declaration.name}))
        AddDiagnostic("idl.reserved_enum_name_active",
                      "Active enum name cannot also be reserved.",
                      declaration.span);

      std::set<std::uint32_t> value_ids;
      std::set<std::string> value_names;
      std::set<std::uint32_t> catalog_orders;
      std::set<std::uint32_t> retired_ids;
      std::set<std::string> retired_names;
      for (const auto &retired : declaration.reservedIds) {
        if (retired.registry != RegistryKind::EnumValue || retired.value == 0 ||
            !retired_ids.insert(retired.value).second)
          AddDiagnostic("idl.invalid_reserved_enum_value_id",
                        "Reserved enum value IDs must be nonzero, unique, and "
                        "enum_value scoped.",
                        retired.span);
      }
      for (const auto &retired : declaration.reservedNames) {
        ValidateName(retired.value, "Reserved enum value name", retired.span);
        if (retired.registry != RegistryKind::EnumValue ||
            !retired_names.insert(retired.value).second)
          AddDiagnostic(
              "idl.invalid_reserved_enum_value_name",
              "Reserved enum value names must be unique and enum_value scoped.",
              retired.span);
      }
      for (const auto &value : declaration.values) {
        ValidateName(value.name, "Enum value name", value.span);
        if (value.valueId == 0 || !value_ids.insert(value.valueId).second)
          AddDiagnostic("idl.invalid_enum_value_id",
                        "Enum value IDs must be nonzero and unique.",
                        value.span);
        if (!value_names.insert(value.name).second)
          AddDiagnostic("idl.duplicate_enum_value_name",
                        "Enum value names must be unique.", value.span);
        if (retired_ids.contains(value.valueId) ||
            retired_names.contains(value.name))
          AddDiagnostic("idl.retired_enum_value_active",
                        "Retired enum value identity cannot be active.",
                        value.span);
        if (value.catalogIdentity.has_value() &&
            !catalog_orders.insert(value.catalogIdentity->declarationOrder)
                 .second)
          AddDiagnostic(
              "idl.duplicate_enum_value_catalog_order",
              "Enum value catalog orders must be unique within an enum.",
              value.catalogIdentity->span);
      }
      if (declaration.values.empty())
        AddDiagnostic("idl.enum_values_empty",
                      "Enum requires at least one value.", declaration.span);
      std::sort(declaration.values.begin(), declaration.values.end(),
                [](const auto &lhs, const auto &rhs) {
                  if (lhs.catalogIdentity.has_value() !=
                      rhs.catalogIdentity.has_value())
                    return lhs.catalogIdentity.has_value();
                  if (lhs.catalogIdentity.has_value() &&
                      lhs.catalogIdentity->declarationOrder !=
                          rhs.catalogIdentity->declarationOrder)
                    return lhs.catalogIdentity->declarationOrder <
                           rhs.catalogIdentity->declarationOrder;
                  return lhs.valueId == rhs.valueId ? lhs.name < rhs.name
                                                    : lhs.valueId < rhs.valueId;
                });
      std::sort(declaration.reservedIds.begin(), declaration.reservedIds.end(),
                [](const auto &lhs, const auto &rhs) {
                  return lhs.value < rhs.value;
                });
      std::sort(declaration.reservedNames.begin(),
                declaration.reservedNames.end(),
                [](const auto &lhs, const auto &rhs) {
                  return lhs.value < rhs.value;
                });
    }
    std::sort(document.enums.begin(), document.enums.end(),
              [](const auto &lhs, const auto &rhs) {
                return lhs.enumId == rhs.enumId ? lhs.name < rhs.name
                                                : lhs.enumId < rhs.enumId;
              });

    std::map<std::string, SourceSpan> model_storage_names;
    std::map<std::string, SourceSpan> model_resolution_names;
    validate_type_declarations(
        document.commands, RegistryKind::Command, "Command",
        [](const auto &declaration) { return declaration.commandId; });
    validate_type_declarations(
        document.events, RegistryKind::Event, "Event",
        [](const auto &declaration) { return declaration.eventId; });
    const auto validate_model_reference =
        [this](std::string_view name, std::string_view label, SourceSpan span,
               bool allow_none) {
          if (allow_none && name == "none")
            return;
          ValidateName(name, label, span);
        };
    for (const auto &command : document.commands) {
      validate_model_reference(command.requestModelName,
                               "Command request model", command.span, true);
      validate_model_reference(command.responseModelName,
                               "Command response model", command.span, true);
      if (command.errorModelNames.empty()) {
        AddDiagnostic(
            "idl.command_errors_empty",
            "Command errors must be none or one or more error models.",
            command.span);
      }
      std::set<std::string> error_names;
      for (const auto &error : command.errorModelNames) {
        validate_model_reference(error, "Command error model", command.span,
                                 true);
        if (!error_names.insert(error).second) {
          AddDiagnostic("idl.command_error_duplicate",
                        "Command error model names must be unique.",
                        command.span);
        }
      }
      if (command.errorModelNames.size() > 1 && error_names.contains("none")) {
        AddDiagnostic("idl.command_errors_none_mixed",
                      "Command errors cannot combine none with error models.",
                      command.span);
      }
    }
    for (const auto &event : document.events)
      validate_model_reference(event.payloadModelName, "Event payload model",
                               event.span, false);
    for (const auto &type : document.semanticTypes)
      if (!type.enumName.empty())
        ValidateName(type.enumName, "Semantic enum", type.span);
    for (auto &model : document.models) {
      ValidateName(model.name, "Model name", model.span);
      ValidateName(model.storageName, "Model storage name", model.span);
      ValidateName(model.domain, "Model domain", model.span);
      ValidateName(model.mutationAuthority, "Model mutation authority",
                   model.span);
      ValidateName(model.kind, "Model kind", model.span);
      ValidateName(model.retention, "Model retention", model.span);
      ValidateName(model.role, "Model role", model.span);
      ValidateName(model.nameReview, "Model name review status", model.span);
      if (model.catalogIdentity.has_value()) {
        const auto &identity = *model.catalogIdentity;
        if (identity.fragmentId.empty() || identity.registryKey.empty() ||
            identity.modelId.empty() || identity.logicalName.empty() ||
            identity.qualifiedName.empty())
          AddDiagnostic(
              "idl.model_catalog_identity_empty",
              "Model catalog identity string properties must be nonempty.",
              identity.span);
      }
      if (!model.nameReviewRationale.empty() && model.nameReview == "canonical")
        AddDiagnostic(
            "idl.name_review_rationale_invalid",
            "Canonical model names cannot carry an exception rationale.",
            model.span);
      if (model.nameReview != "canonical" && model.nameReviewRationale.empty())
        AddDiagnostic("idl.name_review_rationale_missing",
                      "Non-canonical name review states require a rationale.",
                      model.span);
      if (!model_storage_names.emplace(model.storageName, model.span).second)
        AddDiagnostic("idl.duplicate_model_storage_name",
                      "Model storage names must be unique.", model.span);
      if (!model_resolution_names.emplace(model.name, model.span).second)
        AddDiagnostic("idl.model_resolution_name_collision",
                      "Active and retired model names must not collide.",
                      model.span);
      std::set<std::string> aliases;
      for (const auto &alias : model.sourceAliases) {
        if (alias.empty() || !aliases.insert(alias).second)
          AddDiagnostic("idl.invalid_source_alias",
                        "Model source aliases must be nonempty and unique.",
                        model.span);
      }
      std::set<std::string> retired_names_for_model;
      for (const auto &retired_name : model.retiredNames) {
        ValidateName(retired_name, "Retired model name", model.span);
        if (retired_name == model.name ||
            !retired_names_for_model.insert(retired_name).second)
          AddDiagnostic("idl.invalid_retired_model_name",
                        "Retired model names must be unique and not active.",
                        model.span);
        if (!model_resolution_names.emplace(retired_name, model.span).second)
          AddDiagnostic("idl.model_resolution_name_collision",
                        "Active and retired model names must not collide.",
                        model.span);
      }
      std::set<std::string> catalog_retired_names_for_model;
      for (const auto &retired_name : model.catalogRetiredNames) {
        if (retired_name.empty() ||
            !catalog_retired_names_for_model.insert(retired_name).second)
          AddDiagnostic(
              "idl.invalid_catalog_retired_model_name",
              "Catalog retired model names must be nonempty and unique.",
              model.span);
      }
      std::sort(model.sourceAliases.begin(), model.sourceAliases.end());
      std::sort(model.retiredNames.begin(), model.retiredNames.end());
      std::sort(model.catalogRetiredNames.begin(),
                model.catalogRetiredNames.end());
      if (model.typeId == 0) {
        AddDiagnostic("idl.zero_model_id", "Model type ID must be nonzero.",
                      model.span);
      } else if (const auto [existing, inserted] =
                     model_ids.emplace(model.typeId, model.span);
                 !inserted) {
        AddDiagnostic(
            "idl.duplicate_model_id", "Model type ID must be unique.",
            model.span,
            {{
                .message = "The first model using this type ID is here.",
                .sourceName = sourceName_,
                .span = existing->second,
            }});
      }
      if (const auto [existing, inserted] =
              model_names.emplace(model.name, model.span);
          !inserted) {
        AddDiagnostic("idl.duplicate_model_name", "Model name must be unique.",
                      model.span,
                      {{
                          .message = "The first model using this name is here.",
                          .sourceName = sourceName_,
                          .span = existing->second,
                      }});
      }
      if (reserved_ids.contains({RegistryKind::Model, model.typeId})) {
        AddDiagnostic("idl.reserved_model_id_active",
                      "Active model type ID cannot also be reserved.",
                      model.span);
      }
      if (reserved_names.contains({RegistryKind::Model, model.name})) {
        AddDiagnostic("idl.reserved_model_name_active",
                      "Active model name cannot also be reserved.", model.span);
      }

      std::map<std::string, SourceSpan> field_names;
      std::map<std::string, SourceSpan> field_storage_names;
      std::map<std::uint32_t, SourceSpan> field_ids;
      std::set<std::uint32_t> reserved_field_ids;
      std::set<std::string> reserved_field_names;
      std::map<std::string, SourceSpan> catalog_field_registry_keys;
      std::map<std::uint32_t, SourceSpan> catalog_field_orders;
      std::size_t primary_count = 0;
      for (const auto &reserved : model.reservedIds) {
        if (reserved.registry != RegistryKind::Field) {
          AddDiagnostic("idl.reservation_scope_invalid",
                        "Model members may reserve only field identities.",
                        reserved.span);
        }
        if (reserved.value == 0) {
          AddDiagnostic("idl.zero_reserved_field_id",
                        "Reserved field ID must be nonzero.", reserved.span);
        } else if (!reserved_field_ids.insert(reserved.value).second) {
          AddDiagnostic("idl.duplicate_reserved_field_id",
                        "Reserved field ID must be unique.", reserved.span);
        }
      }
      for (const auto &reserved : model.reservedNames) {
        if (reserved.registry != RegistryKind::Field) {
          AddDiagnostic("idl.reservation_scope_invalid",
                        "Model members may reserve only field identities.",
                        reserved.span);
        }
        ValidateName(reserved.value, "Reserved field name", reserved.span);
        if (!reserved_field_names.insert(reserved.value).second) {
          AddDiagnostic("idl.duplicate_reserved_field_name",
                        "Reserved field name must be unique.", reserved.span);
        }
      }
      std::sort(model.reservedIds.begin(), model.reservedIds.end(),
                [](const auto &lhs, const auto &rhs) {
                  return lhs.value < rhs.value;
                });
      std::sort(model.reservedNames.begin(), model.reservedNames.end(),
                [](const auto &lhs, const auto &rhs) {
                  return lhs.value < rhs.value;
                });
      for (const auto &field : model.fields) {
        ValidateName(field.name, "Field name", field.span);
        ValidateName(field.storageName, "Field storage name", field.span);
        ValidateName(field.typeName, "Field type", field.span);
        if (!field.importTransform.empty())
          ValidateName(field.importTransform, "Field import transform",
                       field.span);
        if (field.serialization != "include" && field.serialization != "omit")
          AddDiagnostic("idl.field_serialization_invalid",
                        "Field serialization must be include or omit.",
                        field.span);
        if (field.defaultValue.kind != "none") {
          ValidateName(field.defaultValue.kind, "Field default kind",
                       field.defaultValue.span);
          if (!field.defaultValue.boundarySource.empty())
            ValidateName(field.defaultValue.boundarySource,
                         "Field boundary default source",
                         field.defaultValue.span);
        }
        std::set<std::string> check_names;
        for (const auto &check : field.checks) {
          ValidateName(check.name, "Field check name", check.span);
          ValidateName(check.kind, "Field check kind", check.span);
          ValidateName(check.unit, "Field check unit", check.span);
          if (!check_names.insert(check.name).second)
            AddDiagnostic("idl.duplicate_field_check_name",
                          "Field check names must be unique within a field.",
                          check.span);
        }
        std::set<std::string> query_operators;
        for (const auto &operation : field.query.operators) {
          ValidateName(operation, "Field query operator", field.query.span);
          if (!query_operators.insert(operation).second)
            AddDiagnostic("idl.duplicate_query_operator",
                          "Field query operators must be unique.",
                          field.query.span);
        }
        if (field.sourceProvenance != "canonical" &&
            field.sourceProvenance != "imported" &&
            field.sourceProvenance != "derived" &&
            field.sourceProvenance != "boundary_generated")
          AddDiagnostic("idl.field_source_provenance_invalid",
                        "Field source_provenance must be canonical, imported, "
                        "derived, or boundary_generated.",
                        field.span);
        if ((field.sourceProvenance == "imported") !=
            field.sourceField.has_value())
          AddDiagnostic("idl.field_source_field_invalid",
                        "Imported fields require source_field and non-imported "
                        "fields prohibit it.",
                        field.span);
        if (field.sourceField.has_value() && field.sourceField->empty())
          AddDiagnostic("idl.field_source_field_invalid",
                        "Field source_field cannot be empty.", field.span);
        if (!field.importTransform.empty() &&
            field.sourceProvenance != "imported")
          AddDiagnostic(
              "idl.field_import_transform_invalid",
              "Field import_transform is valid only for imported provenance.",
              field.span);
        if (field.primary && (!field.required || field.redacted ||
                              !field.immutable || !field.writeOnce ||
                              field.sourceProvenance != "boundary_generated"))
          AddDiagnostic("idl.primary_id_traits_invalid",
                        "Primary IDs must be required, non-redacted, "
                        "immutable, write-once, and boundary-generated.",
                        field.span);
        if (field.catalogIdentity.has_value()) {
          const auto &identity = *field.catalogIdentity;
          if (!model.catalogIdentity.has_value())
            AddDiagnostic(
                "idl.field_catalog_identity_without_model",
                "Field catalog identity requires model catalog identity.",
                identity.span);
          if (identity.registryKey.empty())
            AddDiagnostic("idl.field_catalog_registry_key_empty",
                          "Field catalog registry key must be nonempty.",
                          identity.span);
          else if (const auto [existing, inserted] =
                       catalog_field_registry_keys.emplace(identity.registryKey,
                                                           identity.span);
                   !inserted)
            AddDiagnostic(
                "idl.duplicate_field_catalog_registry_key",
                "Field catalog registry keys must be unique within a model.",
                identity.span,
                {{
                    .message =
                        "The first field using this registry key is here.",
                    .sourceName = sourceName_,
                    .span = existing->second,
                }});
          if (const auto [existing, inserted] = catalog_field_orders.emplace(
                  identity.declarationOrder, identity.span);
              !inserted)
            AddDiagnostic(
                "idl.duplicate_field_catalog_order",
                "Field catalog declaration orders must be unique within a "
                "model.",
                identity.span,
                {{
                    .message =
                        "The first field using this declaration order is here.",
                    .sourceName = sourceName_,
                    .span = existing->second,
                }});
        } else if (model.catalogIdentity.has_value()) {
          AddDiagnostic("idl.field_catalog_identity_missing",
                        "Every field in a catalog-backed model requires "
                        "catalog_identity.",
                        field.span);
        }
        if (!field_storage_names.emplace(field.storageName, field.span).second)
          AddDiagnostic("idl.duplicate_field_storage_name",
                        "Field storage names must be unique within a model.",
                        field.span);
        if (field.primary)
          ++primary_count;
        if (field.fieldId == 0) {
          AddDiagnostic("idl.zero_field_id", "Field ID must be nonzero.",
                        field.span);
        } else if (const auto [existing, inserted] =
                       field_ids.emplace(field.fieldId, field.span);
                   !inserted) {
          AddDiagnostic("idl.duplicate_field_id",
                        "Field ID must be unique within its model.", field.span,
                        {{
                            .message = "The first field using this ID is here.",
                            .sourceName = sourceName_,
                            .span = existing->second,
                        }});
        }
        if (const auto [existing, inserted] =
                field_names.emplace(field.name, field.span);
            !inserted) {
          AddDiagnostic(
              "idl.duplicate_field_name",
              "Field name must be unique within its model.", field.span,
              {{
                  .message = "The first field using this name is here.",
                  .sourceName = sourceName_,
                  .span = existing->second,
              }});
        }
        if (reserved_field_ids.contains(field.fieldId)) {
          AddDiagnostic("idl.reserved_field_id_active",
                        "Active field ID cannot also be reserved.", field.span);
        }
        if (reserved_field_names.contains(field.name)) {
          AddDiagnostic("idl.reserved_field_name_active",
                        "Active field name cannot also be reserved.",
                        field.span);
        }
      }
      if (primary_count > 1) {
        AddDiagnostic("idl.primary_id_count_invalid",
                      "A model may declare at most one primary_id shorthand.",
                      model.span);
      }
      if (model.catalogIdentity.has_value()) {
        std::uint32_t expected_order = 0;
        for (const auto &[order, span] : catalog_field_orders) {
          if (order != expected_order) {
            AddDiagnostic("idl.field_catalog_order_not_contiguous",
                          "Field catalog declaration orders must be contiguous "
                          "from zero within a model.",
                          span);
            break;
          }
          ++expected_order;
        }
      }

      const auto validate_field_list =
          [this](const std::vector<std::string> &fields, std::string_view label,
                 SourceSpan span, bool allow_empty) {
            if ((!allow_empty && fields.empty()) ||
                fields.size() > limits_.maximumStructuralFields)
              AddDiagnostic(
                  "idl.structural_field_count_invalid",
                  std::string(label) +
                      " field count is empty or exceeds the configured limit.",
                  span);
            std::set<std::string> names;
            for (const auto &field_name : fields) {
              ValidateName(field_name, label, span);
              if (!names.insert(field_name).second)
                AddDiagnostic("idl.duplicate_structural_field",
                              std::string(label) + " fields must be unique.",
                              span);
            }
          };

      std::set<std::string> key_names;
      std::size_t declared_primary_count = 0;
      for (const auto &key : model.keys) {
        ValidateName(key.name, "Key name", key.span);
        ValidateName(key.kind, "Key kind", key.span);
        validate_field_list(key.fields, "Key", key.span, false);
        if (!key_names.insert(key.name).second)
          AddDiagnostic("idl.duplicate_key_name",
                        "Key names must be unique within a model.", key.span);
        if (key.kind == "primary")
          ++declared_primary_count;
      }
      if (declared_primary_count > 1)
        AddDiagnostic("idl.primary_key_count_invalid",
                      "A model may declare at most one primary key.",
                      model.span);
      if (primary_count == 0 && declared_primary_count == 0)
        AddDiagnostic("idl.primary_key_missing",
                      "Each model requires a primary_id shorthand or a "
                      "declared primary key.",
                      model.span);
      std::set<std::string> index_names;
      for (const auto &index : model.indexes) {
        ValidateName(index.name, "Index name", index.span);
        validate_field_list(index.fields, "Index", index.span, false);
        if (!index_names.insert(index.name).second)
          AddDiagnostic("idl.duplicate_index_name",
                        "Index names must be unique within a model.",
                        index.span);
      }
      std::set<std::string> include_names;
      for (const auto &relation : model.relations) {
        ValidateName(relation.name, "Relation name", relation.span);
        ValidateName(relation.cardinality, "Relation cardinality",
                     relation.span);
        ValidateName(relation.onDelete, "Relation on_delete action",
                     relation.span);
        ValidateName(relation.onUpdate, "Relation on_update action",
                     relation.span);
        validate_field_list(relation.sourceFields, "Relation source",
                            relation.span, false);
        validate_field_list(relation.targetFields, "Relation target",
                            relation.span, false);
        if (!IsCanonicalQualifiedName(relation.targetModel))
          AddDiagnostic(
              "idl.noncanonical_qualified_name",
              "Relation target must be a canonical schema.vN.model name.",
              relation.span);
        if (!include_names.insert(relation.name).second)
          AddDiagnostic(
              "idl.duplicate_include_name",
              "Relation and join names share one namespace within a model.",
              relation.span);
      }
      for (const auto &join : model.joins) {
        ValidateName(join.name, "Join name", join.span);
        ValidateName(join.sourceRelation, "Join source relation", join.span);
        ValidateName(join.targetRelation, "Join target relation", join.span);
        if (!IsCanonicalQualifiedName(join.throughModel))
          AddDiagnostic(
              "idl.noncanonical_qualified_name",
              "Join through must be a canonical schema.vN.model name.",
              join.span);
        if (!include_names.insert(join.name).second)
          AddDiagnostic(
              "idl.duplicate_include_name",
              "Relation and join names share one namespace within a model.",
              join.span);
      }
      std::set<std::string> one_of_names;
      for (auto &one_of : model.oneOfs) {
        ValidateName(one_of.name, "One_of name", one_of.span);
        ValidateName(one_of.discriminatorField, "One_of discriminator field",
                     one_of.span);
        if (!one_of_names.insert(one_of.name).second)
          AddDiagnostic("idl.duplicate_one_of_name",
                        "One_of names must be unique within a model.",
                        one_of.span);
        std::set<std::string> tags;
        for (auto &variant : one_of.variants) {
          ValidateName(variant.tag, "One_of variant tag", variant.span);
          validate_field_list(variant.fields, "One_of variant", variant.span,
                              true);
          validate_field_list(variant.optionalFields, "One_of optional variant",
                              variant.span, true);
          if (!tags.insert(variant.tag).second)
            AddDiagnostic("idl.duplicate_one_of_variant",
                          "One_of variant tags must be unique.", variant.span);
          std::sort(variant.fields.begin(), variant.fields.end());
          std::sort(variant.optionalFields.begin(),
                    variant.optionalFields.end());
        }
        std::sort(
            one_of.variants.begin(), one_of.variants.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.tag < rhs.tag; });
      }
      std::set<std::string> temporal_contract_names;
      for (const auto &contract : model.temporalContracts) {
        ValidateName(contract.name, "Temporal contract name", contract.span);
        ValidateName(contract.startField, "Temporal start field",
                     contract.span);
        ValidateName(contract.endField, "Temporal end field", contract.span);
        ValidateName(contract.ordering, "Temporal ordering", contract.span);
        ValidateName(contract.openEnd, "Temporal open-end policy",
                     contract.span);
        if (!temporal_contract_names.insert(contract.name).second)
          AddDiagnostic(
              "idl.duplicate_temporal_contract_name",
              "Temporal contract names must be unique within a model.",
              contract.span);
      }
      std::sort(
          model.keys.begin(), model.keys.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
      std::sort(
          model.indexes.begin(), model.indexes.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
      std::sort(
          model.relations.begin(), model.relations.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
      std::sort(
          model.joins.begin(), model.joins.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
      std::sort(
          model.oneOfs.begin(), model.oneOfs.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
      std::sort(
          model.temporalContracts.begin(), model.temporalContracts.end(),
          [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
    }
  }

  std::string sourceName_;
  std::vector<Token> tokens_;
  std::vector<Comment> comments_;
  std::vector<Diagnostic> diagnostics_;
  ParseLimits limits_;
  std::uint32_t currentEdition_ = 1;
  std::size_t index_ = 0;
};
} // namespace

bool ParseResult::ok() const {
  return document.has_value() &&
         std::none_of(diagnostics.begin(), diagnostics.end(),
                      [](const Diagnostic &diagnostic) {
                        return diagnostic.severity == DiagnosticSeverity::Error;
                      });
}

bool IsCanonicalSnakeCase(std::string_view value) {
  if (value.empty() || value.front() < 'a' || value.front() > 'z' ||
      value.back() == '_')
    return false;
  bool previous_underscore = false;
  for (const char character : value) {
    const bool lowercase = character >= 'a' && character <= 'z';
    const bool digit = character >= '0' && character <= '9';
    const bool underscore = character == '_';
    if (!lowercase && !digit && !underscore)
      return false;
    if (underscore && previous_underscore)
      return false;
    previous_underscore = underscore;
  }
  return true;
}

bool IsCanonicalQualifiedName(std::string_view value) {
  const auto first_dot = value.find('.');
  if (first_dot == std::string_view::npos)
    return false;
  const auto second_dot = value.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos ||
      value.find('.', second_dot + 1) != std::string_view::npos)
    return false;
  const auto schema = value.substr(0, first_dot);
  const auto version = value.substr(first_dot + 1, second_dot - first_dot - 1);
  const auto name = value.substr(second_dot + 1);
  if (!IsCanonicalSnakeCase(schema) || !IsCanonicalSnakeCase(name) ||
      version.size() < 2 || version.front() != 'v' || version[1] == '0')
    return false;
  return std::all_of(version.begin() + 1, version.end(), [](char character) {
    return character >= '0' && character <= '9';
  });
}

ParseResult Parse(std::string_view source_name, std::string_view source,
                  const ParseLimits &limits) {
  if (limits.maximumDiagnostics == 0) {
    const SourcePosition position;
    return {
        .document = std::nullopt,
        .diagnostics = {{
            .code = "idl.invalid_parse_limits",
            .severity = DiagnosticSeverity::Error,
            .message = "The diagnostic limit must be greater than zero.",
            .sourceName = std::string(source_name),
            .span = {position, position},
        }},
        .normalizedText = {},
    };
  }

  auto lexed = detail::Lex(source_name, source, limits);
  if (std::any_of(lexed.diagnostics.begin(), lexed.diagnostics.end(),
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.severity == DiagnosticSeverity::Error;
                  })) {
    SortDiagnostics(lexed.diagnostics);
    return {
        .document = std::nullopt,
        .diagnostics = std::move(lexed.diagnostics),
        .normalizedText = {},
    };
  }
  return Parser(source_name, std::move(lexed.tokens), std::move(lexed.comments),
                std::move(lexed.diagnostics), limits)
      .Run();
}
} // namespace arken::schema::idl::v1
