#include "arken_schema_cpp_v1/api.hpp"

#include <exception>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "arken_schema_idl_v1/semantic.hpp"

namespace schema_cpp = ::arken::schema::cpp::v1;
namespace idl = ::arken::schema::idl::v1;
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
struct Arguments
{
    std::string entry;
    std::map<std::string, fs::path> sources;
    fs::path output;
    bool overwriteExisting = false;
    bool commandCatalog = false;
    bool contextCommandCatalog = false;
    bool commandJson = false;
    bool compactSource = false;
    bool separateModelValidation = false;
    std::set<std::string> commandOnly;
};

std::optional<Arguments> ParseArguments(int argc, char** argv, std::string* error)
{
    Arguments result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view key(argv[index]);
        if (key == "--entry" && index + 1 < argc)
        {
            result.entry = argv[++index];
        }
        else if (key == "--source" && index + 1 < argc)
        {
            const std::string source(argv[++index]);
            const auto separator = source.find('=');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= source.size())
            {
                if (error)
                    *error = "--source must use LOGICAL_PATH=FILESYSTEM_PATH";
                return std::nullopt;
            }
            if (!result.sources.emplace(source.substr(0, separator), fs::path(source.substr(separator + 1))).second)
            {
                if (error)
                    *error = "duplicate logical source path";
                return std::nullopt;
            }
        }
        else if (key == "--output" && index + 1 < argc)
        {
            result.output = fs::path(argv[++index]);
        }
        else if (key == "--overwrite-existing")
        {
            result.overwriteExisting = true;
        }
        else if (key == "--command-catalog")
        {
            result.commandCatalog = true;
        }
        else if (key == "--command-json") { result.commandJson = true; result.commandCatalog = true; }
        else if (key == "--context-command-catalog") { result.contextCommandCatalog = true; }
        else if (key == "--compact-source") { result.compactSource = true; }
        else if (key == "--separate-model-validation") { result.separateModelValidation = true; }
        else if (key == "--command-only" && index + 1 < argc) { result.commandOnly.emplace(argv[++index]); }
        else
        {
            if (error)
                *error = "unknown or incomplete argument: " + std::string(key);
            return std::nullopt;
        }
    }
    if (result.entry.empty() || result.sources.empty() || result.output.empty())
    {
        if (error)
            *error = "--entry, at least one --source, and --output are required";
        return std::nullopt;
    }
    if (!result.sources.contains(result.entry))
    {
        if (error)
            *error = "entry source must be present in --source arguments";
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> ReadSource(const fs::path& path, std::string* error)
{
    std::error_code filesystem_error;
    const auto size = fs::file_size(path, filesystem_error);
    if (filesystem_error || size > 16u * 1024u * 1024u)
    {
        if (error)
            *error = "source is missing or exceeds 16 MiB: " + path.string();
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        if (error)
            *error = "failed to open source: " + path.string();
        return std::nullopt;
    }
    std::string content(static_cast<std::size_t>(size), '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!input)
    {
        if (error)
            *error = "failed to read source: " + path.string();
        return std::nullopt;
    }
    return content;
}

std::optional<std::string> ReadExistingArtifact(const fs::path& path, std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        if (error)
            *error = "failed to read existing generated artifact: " + path.string();
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct ArtifactDecision
{
    fs::path outputPath;
    fs::path temporaryPath;
    bool changed = false;
};

void RemoveTemporaryArtifacts(const std::vector<ArtifactDecision>& decisions)
{
    for (const auto& decision : decisions)
    {
        std::error_code ignored;
        fs::remove(decision.temporaryPath, ignored);
    }
}

void PrintDiagnostics(const std::vector<idl::Diagnostic>& diagnostics)
{
    for (const auto& diagnostic : diagnostics)
    {
        std::cerr << diagnostic.code << ": " << diagnostic.message;
        if (!diagnostic.sourceName.empty())
            std::cerr << " [" << diagnostic.sourceName << "]";
        std::cerr << '\n';
    }
}
}

int main(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index)
    if (std::string_view(argv[index]) == "--version")
    {
        std::cout << "arken-schema-cpp-v1-codegen r6078-command-selection-model-descriptions\n";
        return 0;
    }
    try
    {
        std::string error;
        const auto arguments = ParseArguments(argc, argv, &error);
        if (!arguments.has_value())
        {
            std::cerr << error << '\n';
            return 2;
        }

        std::vector<idl::SourceFile> sources;
        sources.reserve(arguments->sources.size());
        for (const auto& [logical_path, filesystem_path] : arguments->sources)
        {
            const auto content = ReadSource(filesystem_path, &error);
            if (!content.has_value())
            {
                std::cerr << error << '\n';
                return 3;
            }
            sources.push_back({logical_path, *content});
        }

        auto project = idl::ParseProject(arguments->entry, sources);
        if (!project.ok() || !project.project.has_value())
        {
            PrintDiagnostics(project.diagnostics);
            return 4;
        }
        if (!arguments->commandOnly.empty())
        {
            if (project.project->sources.size() != 1)
                throw std::invalid_argument("--command-only requires one self-contained source");
            auto document = project.project->sources.front().document;
            if (document.catalog || !document.imports.empty())
                throw std::invalid_argument("--command-only does not admit catalogs or imports");
            std::set<std::string> models;
            for (const auto& name : arguments->commandOnly) {
                const auto command = std::find_if(document.commands.begin(), document.commands.end(),
                    [&](const auto& value) { return value.name == name; });
                if (command == document.commands.end())
                    throw std::invalid_argument("--command-only names an unknown command: " + name);
                models.insert(command->requestModelName);
                models.insert(command->responseModelName);
                models.insert(command->errorModelNames.begin(), command->errorModelNames.end());
            }
            std::erase_if(document.commands, [&](const auto& command) {
                return !arguments->commandOnly.contains(command.name);
            });
            document.events.clear();
            std::erase_if(document.models, [&](const auto& model) { return !models.contains(model.name); });
            for (const auto& model : document.models)
                if (model.external || model.catalogIdentity || !model.relations.empty() || !model.joins.empty())
                    throw std::invalid_argument("--command-only requires self-contained command models");
            // Reparse and validate the projection; never copy generated validators
            // or silently drop a retained model's dependency or constraints.
            sources = {{arguments->entry, idl::Format(document)}};
            project = idl::ParseProject(arguments->entry, sources);
            if (!project.ok() || !project.project) { PrintDiagnostics(project.diagnostics); return 4; }
        }
        const auto manifest = idl::BuildSemanticManifest(*project.project);
        if (!manifest.ok() || !manifest.manifest.has_value())
        {
            PrintDiagnostics(manifest.diagnostics);
            return 5;
        }
        schema_cpp::CppGenerationOptions generation_options;
        generation_options.compactSource = arguments->compactSource;
    generation_options.separateModelValidation = arguments->separateModelValidation;
        auto generated = schema_cpp::GenerateCppArtifacts(*manifest.manifest, generation_options);
        if (!generated.ok())
        {
            PrintDiagnostics(generated.diagnostics);
            return 6;
        }
        if(arguments->commandJson) {
            const auto codecs=schema_cpp::GenerateCommandJsonCodecs(*manifest.manifest,generated);
            if(!codecs.ok()){PrintDiagnostics(codecs.diagnostics);return 6;}
            generated.artifacts.insert(generated.artifacts.end(),codecs.artifacts.begin(),codecs.artifacts.end());
        }
        if (arguments->commandCatalog)
        {
            const auto catalog = schema_cpp::GenerateCommandCatalog(*manifest.manifest);
            if (!catalog.ok()) { PrintDiagnostics(catalog.diagnostics); return 6; }
            generated.artifacts.push_back({arguments->entry,
                "arken_schema_generated/command-catalog.json", "", catalog.json});
        }
        if (arguments->contextCommandCatalog)
        {
            const auto catalog = schema_cpp::GenerateContextCommandCatalog(*manifest.manifest);
            if (!catalog.ok()) { PrintDiagnostics(catalog.diagnostics); return 6; }
            generated.artifacts.push_back({arguments->entry,
                "arken_schema_generated/context-command-catalog.json", "", catalog.json});
        }

        std::vector<ArtifactDecision> decisions;
        decisions.reserve(generated.artifacts.size());
        for (const auto& artifact : generated.artifacts)
        {
            const fs::path output_path = arguments->output / artifact.relativePath;
            ArtifactDecision decision{
                .outputPath = output_path,
                .temporaryPath = fs::path(output_path.string() + ".arken-codegen-tmp"),
                .changed = true,
            };
            std::error_code filesystem_error;
            const bool exists = fs::exists(output_path, filesystem_error);
            if (filesystem_error)
            {
                std::cerr << "failed to inspect generated artifact: " << output_path << '\n';
                return 7;
            }
            if (exists)
            {
                const auto existing = ReadExistingArtifact(output_path, &error);
                if (!existing.has_value())
                {
                    std::cerr << error << '\n';
                    return 7;
                }
                decision.changed = *existing != artifact.content;
                if (decision.changed && !arguments->overwriteExisting)
                {
                    std::cerr
                        << "refusing to replace existing generated artifact without --overwrite-existing: "
                        << output_path << '\n'
                        << "generate every language binding into a staging root, validate matching fingerprints, "
                           "then opt in to replacement\n";
                    return 10;
                }
            }
            decisions.push_back(std::move(decision));
        }

        for (std::size_t index = 0; index < generated.artifacts.size(); ++index)
        {
            if (!decisions[index].changed)
                continue;
            std::error_code filesystem_error;
            fs::create_directories(decisions[index].outputPath.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                std::cerr << "failed to create generated artifact directory: "
                          << decisions[index].outputPath.parent_path() << '\n';
                RemoveTemporaryArtifacts(decisions);
                return 7;
            }
            std::ofstream output(decisions[index].temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                std::cerr << "failed to open temporary generated artifact: "
                          << decisions[index].temporaryPath << '\n';
                RemoveTemporaryArtifacts(decisions);
                return 7;
            }
            const auto& content = generated.artifacts[index].content;
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output)
            {
                std::cerr << "failed to write temporary generated artifact: "
                          << decisions[index].temporaryPath << '\n';
                RemoveTemporaryArtifacts(decisions);
                return 8;
            }
        }

        for (const auto& decision : decisions)
        {
            if (!decision.changed)
                continue;
            std::error_code filesystem_error;
            fs::rename(decision.temporaryPath, decision.outputPath, filesystem_error);
            if (filesystem_error)
            {
                std::cerr << "failed to atomically replace generated artifact: " << decision.outputPath
                          << " (" << filesystem_error.message() << ")\n";
                RemoveTemporaryArtifacts(decisions);
                return 8;
            }
        }
        RemoveTemporaryArtifacts(decisions);

        Json artifacts = Json::array();
        std::size_t changed_artifacts = 0;
        for (std::size_t index = 0; index < generated.artifacts.size(); ++index)
        {
            const auto& artifact = generated.artifacts[index];
            const auto& decision = decisions[index];
            if (decision.changed)
                ++changed_artifacts;
            artifacts.push_back({
                {"sourcePath", artifact.sourcePath},
                {"outputPath", decision.outputPath.string()},
                {"changed", decision.changed},
            });
        }
        std::cout << Json{
            {"ok", true},
            {"compactSource", arguments->compactSource},
            {"changedArtifacts", changed_artifacts},
            {"artifacts", std::move(artifacts)},
        }.dump() << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "schema code generation failed: " << exception.what() << '\n';
        return 9;
    }
}
