#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::cpp::v1
{
struct CppGenerationOptions
{
    std::string rootNamespace = "arken::schema::generated";
    std::string includeRoot = "arken_schema_generated";
    // Physical layout only; retain full APIs, companions and physical newlines.
    // The controlled emitter uses escaped single-line literals, not raw C++.
    bool compactSource = false;
    // JSON consumers can omit reflection/storage without changing model rules.
    bool separateModelValidation = false;
};

struct GeneratedCppArtifact
{
    std::string sourcePath;
    std::string relativePath;
    std::string namespaceName;
    std::string content;
};

struct CppGenerationResult
{
    std::vector<GeneratedCppArtifact> artifacts;
    std::vector<idl::v1::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] std::string ProjectCppFieldIdentifier(std::string_view canonicalName);

[[nodiscard]] CppGenerationResult GenerateCppArtifacts(
    const idl::v1::SemanticManifest& manifest,
    const CppGenerationOptions& options = {}
);
}
