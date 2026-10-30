#pragma once

#include "arken_schema_idl_v1/semantic.hpp"
#include "generator.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace arken::schema::cpp::v1 {
// Discovery serialization budget; consumers retain independent runtime limits.
inline constexpr std::size_t maximumCommandCatalogBytes=4u*1024u*1024u;
struct CommandCatalogResult {
  std::string json;
  std::vector<idl::v1::Diagnostic> diagnostics;
  [[nodiscard]] bool ok() const noexcept {
    return diagnostics.empty() && !json.empty();
  }
};

// A transport-neutral projection of the same IR used by typed C++ bindings.
// This catalog is discovery metadata, never authorization or runtime state.
// tool-json.v1 represents fixed identifiers as lowercase hex and 64-bit
// integers as canonical decimal strings; adapters must honor that profile.
[[nodiscard]] CommandCatalogResult
GenerateCommandCatalog(const idl::v1::SemanticManifest &manifest);
// Browser context transport around the unchanged native command arguments.
// This projection does not alter semantic IR, allocation, or C++ bindings.
[[nodiscard]] CommandCatalogResult
GenerateContextCommandCatalog(const idl::v1::SemanticManifest &manifest);
[[nodiscard]] CppGenerationResult GenerateCommandJsonCodecs(
    const idl::v1::SemanticManifest&,const CppGenerationResult&);
} // namespace arken::schema::cpp::v1
