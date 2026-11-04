#include "arken_schema_cpp_v1/command_catalog.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <map>
#include <sstream>
#include <stdexcept>
namespace arken::schema::cpp::v1 {
CppGenerationResult
GenerateCommandJsonCodecs(const idl::v1::SemanticManifest &manifest,
                          const CppGenerationResult &generated) {
  CppGenerationResult result;
  try {
    const auto catalog = GenerateCommandCatalog(manifest);
    if (!catalog.ok()) {
      result.diagnostics = catalog.diagnostics;
      return result;
    }
    std::set<std::uint32_t> models;
    for (const auto &c : manifest.ir.commands) {
      if (c.request)
        models.insert(c.request->model.value);
      if (c.response)
        models.insert(c.response->model.value);
      for (const auto &e : c.errors)
        models.insert(e.model.value);
    }
    std::ostringstream out;
    out << "#pragma once\n#include <algorithm>\n#include <array>\n#include "
           "<type_traits>\n#include \"arken_schema_cpp_v1/tool_json.hpp\"\n";
    std::set<std::string> includedSources;
    std::set<std::string> separateValidationSources;
    std::string completeIncludes;
    // JSON requires model validation, not storage bindings, operation tokens,
    // or the complete reflection catalog. All share the same validator owner.
    for (const auto &artifact : generated.artifacts)
      if (includedSources.insert(artifact.sourcePath).second) {
        const auto path = artifact.relativePath.substr(0, artifact.relativePath.size() - 4u) + "_validation.hpp";
        const bool separate = std::ranges::find(generated.artifacts, path, &GeneratedCppArtifact::relativePath) != generated.artifacts.end();
        if (separate) {
          separateValidationSources.insert(artifact.sourcePath);
          completeIncludes += "#include " + nlohmann::json(artifact.relativePath).dump() + "\n";
        }
        out << "#include " << nlohmann::json(separate ? path : artifact.relativePath).dump() << "\n";
      }
    const auto entry = std::ranges::find(manifest.ir.sources, manifest.ir.entrySource,
                                        &idl::v1::SemanticSource::handle);
    if (entry == manifest.ir.sources.end()) throw std::invalid_argument("catalog entry source missing");
    const auto entryArtifact = std::ranges::find(generated.artifacts, entry->path,
                                                &GeneratedCppArtifact::sourcePath);
    if (entryArtifact == generated.artifacts.end()) throw std::invalid_argument("catalog entry artifact missing");
    // Catalog data is a separate dependency: codec-only consumers must not
    // parse hundreds of thousands of unrelated scalar initializers.
    std::ostringstream catalogOut;
    catalogOut << "#pragma once\n#include <string_view>\nnamespace " << entryArtifact->namespaceName << " {\n";
    if(catalog.json.size()>60000){
      // Adjacent string literals still form one overlength literal. Keep
      // large catalogs as immutable bytes with no dynamic initialization or
      // constexpr-copy step budget, preserving the public string_view API.
      catalogOut << "inline constexpr char commandCatalogStorage[]={\n";
      std::size_t column=0;
      for(unsigned char byte:catalog.json){
        catalogOut << "char(" << static_cast<unsigned>(byte) << "),";
        if(++column==32){catalogOut << '\n';column=0;}
      }
      catalogOut << "0};\ninline constexpr std::string_view commandCatalog{commandCatalogStorage,sizeof(commandCatalogStorage)-1};\n";
    }else catalogOut << "inline constexpr std::string_view commandCatalog=" << nlohmann::json(catalog.json).dump() << ";\n";
    catalogOut << "}\n";
    out << "namespace arken::schema::cpp::v1::tool_json {\n";
    std::map<std::string, bool> emittedEnums;
    for (const auto &model : manifest.ir.models) {
      if (!models.contains(model.handle.value)) continue;
      for (const auto &field : model.fields) {
        if (field.type.kind != idl::v1::SemanticTypeKind::Enum) continue;
        const auto semantic = std::ranges::find(manifest.ir.semanticTypes,
            field.type.semanticTypeId, &idl::v1::SemanticTypeDefinition::semanticTypeId);
        if (semantic == manifest.ir.semanticTypes.end() || !semantic->definingEnum)
          throw std::invalid_argument("enum semantic declaration missing");
        const auto enumeration = std::ranges::find(manifest.ir.enums,
            *semantic->definingEnum, &idl::v1::SemanticEnum::handle);
        const auto wire = std::ranges::find(manifest.ir.wireTypes,
            semantic->wireType, &idl::v1::WireTypeDefinition::handle);
        if (enumeration == manifest.ir.enums.end() || wire == manifest.ir.wireTypes.end())
          throw std::invalid_argument("enum wire declaration missing");
        const auto artifact = std::ranges::find(generated.artifacts,
            enumeration->provenance.sourcePath, &GeneratedCppArtifact::sourcePath);
        if (artifact == generated.artifacts.end()) throw std::invalid_argument("enum source artifact missing");
        const auto type = "::" + artifact->namespaceName + "::" + enumeration->name;
        const bool text = wire->kind == idl::v1::WirePrimitiveKind::Utf8;
        if (auto old=emittedEnums.find(type); old!=emittedEnums.end()) {
          if (old->second!=text) throw std::invalid_argument("one C++ enum has conflicting JSON representations");
          continue;
        }
        emittedEnums.emplace(type,text);
        out << "template<> struct Codec<" << type << "> { using T=" << type << ";\n"
            << "static T read(const Json& input){\n";
        if(text)out<<"require(input.is_string(),\"enum name required\");const auto& value=input.get_ref<const std::string&>();\n";
        else out<<"const auto value=readScalar<std::uint32_t>(input);\n";
        for(const auto& value:enumeration->values)
          out<<"if(value=="<<(text?nlohmann::json(value.name).dump():std::to_string(value.valueId.value))
             <<")return T::"<<value.name<<";\n";
        out<<"throw std::invalid_argument(\"unknown enum value\");}\nstatic Json write(T value){switch(value){\n";
        for(const auto& value:enumeration->values)
          out<<"case T::"<<value.name<<":return "<<(text?nlohmann::json(value.name).dump():std::to_string(value.valueId.value))<<";\n";
        out<<"}throw std::invalid_argument(\"unknown enum value\");}};\n";
      }
    }
    for (const auto &m : manifest.ir.models) {
      if (!models.contains(m.handle.value))
        continue;
      const auto artifact =
          std::ranges::find(generated.artifacts, m.provenance.sourcePath,
                            &GeneratedCppArtifact::sourcePath);
      if (artifact == generated.artifacts.end())
        throw std::invalid_argument("model source artifact missing");
      const auto type = "::" + artifact->namespaceName + "::" + m.name;
      const auto validator = type + (separateValidationSources.contains(m.provenance.sourcePath) ? "_validator" : "_binding");
      out << "template<> struct Codec<" << type << "> {\nusing T=" << type
          << ";\nstatic T read(const Json& input){ "
             "require(input.is_object(),\"object required\");\n";
      out << "constexpr std::array<std::string_view," << m.fields.size()
          << "> keys{";
      for (const auto &f : m.fields)
        out << nlohmann::json(f.name).dump() << ",";
      out << "};\nfor(auto "
             "it=input.begin();it!=input.end();++it)require(std::find(keys."
             "begin(),keys.end(),it.key())!=keys.end(),\"unknown field\");\nT "
             "value;\n";
      for (const auto &f : m.fields) {
        const auto member = f.cppName.value_or(f.name),
                   key = nlohmann::json(f.name).dump();
        const auto access = "value." + member;
        out << "if(input.contains(" << key << ")){\n";
        if (f.cardinality == idl::v1::FieldCardinality::Repeated) {
          out << "const auto& entries=input.at(" << key
              << "); require(entries.is_array()&&entries.size()<="
              << *f.maximumItems
              << ",\"bounded array required\"); for(const auto& entry:entries)"
              << access << ".push_back(Codec<typename decltype(" << access
              << ")::value_type>::read(entry));\n";
        } else if (f.cardinality == idl::v1::FieldCardinality::OptionalOne)
          out << access << "=Codec<typename decltype(" << access
              << ")::value_type>::read(input.at(" << key << "));\n";
        else
          out << access << "=Codec<decltype(" << access << ")>::read(input.at("
              << key << "));\n";
        out << "}";
        if (f.required)
          out << "else throw std::invalid_argument(\"required field "
                 "missing\");";
        out << "\n";
      }
      out << "require(" << validator
          << "::validate(value).ok(),\"model validation "
             "failed\");return value;}\nstatic Json write(const T& "
             "value){require("
          << validator
          << "::validate(value).ok(),\"model validation failed\");Json "
             "out=Json::object();\n";
      for (const auto &f : m.fields) {
        const auto access = "value." + f.cppName.value_or(f.name),
                   key = nlohmann::json(f.name).dump();
        if (f.cardinality == idl::v1::FieldCardinality::Repeated)
          out << "out[" << key
              << "]=Json::array();for(const auto& item:" << access << ")out["
              << key << "].push_back(tool_json::write(item));\n";
        else if (f.cardinality == idl::v1::FieldCardinality::OptionalOne)
          out << "if(" << access << ")out[" << key << "]=tool_json::write(*"
              << access << ");\n";
        else
          out << "out[" << key << "]=tool_json::write(" << access << ");\n";
      }
      out << "return out;}\n};\n";
    }
    out << "}\n";
    result.artifacts.push_back(
        {"command-codecs", "arken_schema_generated/command-codecs.hpp",
         "arken::schema::cpp::v1::tool_json", out.str()});
    result.artifacts.push_back(
        {"command-catalog", "arken_schema_generated/command-catalog.hpp",
         entryArtifact->namespaceName, catalogOut.str()});
    result.artifacts.push_back(
        {"command-json", "arken_schema_generated/command-json.hpp",
         "arken::schema::cpp::v1::tool_json",
         "#pragma once\n" + completeIncludes + "#include \"command-codecs.hpp\"\n#include \"command-catalog.hpp\"\n"});
  } catch (const std::exception &error) {
    result.artifacts.clear();
    result.diagnostics.push_back({.code = "cpp.command_json_unsupported",
                                  .message = error.what(),
                                  .sourceName = "command-json"});
  }
  return result;
}
} // namespace arken::schema::cpp::v1
