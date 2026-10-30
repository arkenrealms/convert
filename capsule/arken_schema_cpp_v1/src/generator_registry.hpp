#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/semantic.hpp"

namespace arken::schema::cpp::v1::generator_detail
{
struct CppRegistrySourceInfo
{
    const idl::v1::SemanticSource* source = nullptr;
    std::string namespaceName;
};

struct CppRegistrySection
{
    std::string content;
    std::vector<idl::v1::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] CppRegistrySection GenerateCppRegistrySection(
    const idl::v1::SemanticManifest& manifest,
    std::string_view allocation_fingerprint,
    const idl::v1::SemanticSource& entry_source,
    const std::vector<CppRegistrySourceInfo>& sources
);
} // namespace arken::schema::cpp::v1::generator_detail
