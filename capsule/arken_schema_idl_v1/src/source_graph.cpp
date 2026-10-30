#include "arken_schema_idl_v1/idl.hpp"
#include "arken_schema_idl_v1/source_format.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arken::schema::idl::v1
{
namespace
{

struct NormalizedPath
{
    std::string value;
    std::string errorCode;
    std::string errorMessage;

    [[nodiscard]] bool ok() const
    {
        return errorCode.empty();
    }
};

bool HasSourceExtension(std::string_view value)
{
    return HasSchemaSourceExtension(value);
}

bool IsPathCharacter(char value)
{
    return (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') ||
        value == '_' || value == '-' || value == '.';
}

NormalizedPath NormalizeLogicalPath(
    std::string_view path,
    bool import_path,
    std::size_t maximum_path_bytes
)
{
    const std::string prefix = import_path ? "Import" : "Source";
    const std::string code_prefix = import_path ? "idl.import_path" : "idl.source_path";
    if (path.empty())
        return {{}, code_prefix + "_empty", prefix + " path cannot be empty."};
    if (path.size() > maximum_path_bytes)
    {
        return {
            {},
            code_prefix + "_too_long",
            prefix + " path exceeds the configured byte limit.",
        };
    }
    if (path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos)
    {
        return {
            {},
            code_prefix + "_outside_root",
            prefix + " path must be project-relative and use forward slashes.",
        };
    }

    std::vector<std::string_view> segments;
    std::size_t begin = 0;
    while (begin <= path.size())
    {
        const std::size_t slash = path.find('/', begin);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        const std::string_view segment = path.substr(begin, end - begin);
        if (segment == "..")
        {
            return {
                {},
                code_prefix + "_traversal",
                prefix + " path cannot contain parent traversal.",
            };
        }
        if (!segment.empty() && segment != ".")
        {
            if (!std::all_of(segment.begin(), segment.end(), IsPathCharacter))
            {
                return {
                    {},
                    code_prefix + "_noncanonical",
                    prefix + " path must use lowercase ASCII path characters.",
                };
            }
            segments.push_back(segment);
        }
        if (slash == std::string_view::npos) break;
        begin = slash + 1;
    }
    if (segments.empty())
        return {{}, code_prefix + "_empty", prefix + " path cannot be empty."};

    std::string normalized;
    for (std::size_t index = 0; index < segments.size(); ++index)
    {
        if (index != 0) normalized.push_back('/');
        normalized.append(segments[index]);
    }
    if (!HasSourceExtension(normalized))
    {
        return {
            {},
            code_prefix + "_extension",
            prefix + " path must use .aoschema or a preserved .arken-schema input.",
        };
    }
    return {.value = std::move(normalized)};
}

NormalizedPath NormalizeLogicalImportRoot(
    std::string_view path,
    std::size_t maximum_path_bytes
)
{
    if (path.empty()) return {.value = {}};
    if (path.size() > maximum_path_bytes)
    {
        return {
            {},
            "idl.source_import_root_too_long",
            "Source import root exceeds the configured byte limit.",
        };
    }
    if (path.front() == '/' || path.back() == '/' ||
        path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos)
    {
        return {
            {},
            "idl.source_import_root_outside_root",
            "Source import root must be project-relative and use forward slashes.",
        };
    }

    std::string normalized;
    std::size_t begin = 0;
    while (begin <= path.size())
    {
        const std::size_t slash = path.find('/', begin);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        const std::string_view segment = path.substr(begin, end - begin);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return {
                {},
                "idl.source_import_root_noncanonical",
                "Source import root must use canonical non-traversing path segments.",
            };
        }
        if (!std::all_of(segment.begin(), segment.end(), IsPathCharacter))
        {
            return {
                {},
                "idl.source_import_root_noncanonical",
                "Source import root must use lowercase ASCII path characters.",
            };
        }
        if (!normalized.empty()) normalized.push_back('/');
        normalized.append(segment);
        if (slash == std::string_view::npos) break;
        begin = slash + 1;
    }
    return {.value = std::move(normalized)};
}

SourceSpan EmptySpan()
{
    const SourcePosition position;
    return {position, position};
}

struct EdgeLocation
{
    std::string sourceName;
    SourceSpan span;
};

class ProjectParser
{
public:
    explicit ProjectParser(const ParseLimits& limits) : limits_(limits)
    {
    }

    ProjectParseResult Run(std::string_view entry_path, const std::vector<SourceFile>& sources)
    {
        if (limits_.maximumDiagnostics == 0)
        {
            const auto span = EmptySpan();
            return {
                .project = std::nullopt,
                .diagnostics = {{
                    .code = "idl.invalid_parse_limits",
                    .severity = DiagnosticSeverity::Error,
                    .message = "The diagnostic limit must be greater than zero.",
                    .sourceName = "<project>",
                    .span = span,
                }},
            };
        }
        if (sources.size() > limits_.maximumSources)
        {
            AddDiagnostic(
                "idl.source_limit_exceeded",
                "IDL source count exceeds the configured project limit.",
                "<project>",
                EmptySpan()
            );
            return Finish();
        }

        IndexSources(sources);
        const auto entry = NormalizeLogicalPath(entry_path, false, limits_.maximumPathBytes);
        if (!entry.ok())
        {
            AddDiagnostic(entry.errorCode, entry.errorMessage, "<project>", EmptySpan());
            return Finish();
        }
        if (!sources_.contains(entry.value))
        {
            AddDiagnostic(
                "idl.entry_source_missing",
                "The project entry source is not present in the source set.",
                entry.value,
                EmptySpan()
            );
            return Finish();
        }

        entryPath_ = entry.value;
        Visit(entryPath_, 0, std::nullopt);
        return Finish();
    }

private:
    enum class VisitState
    {
        Visiting,
        Done,
    };

    struct IndexedSource
    {
        std::string normalizedPath;
        std::string normalizedImportRoot;
        const SourceFile* source = nullptr;
    };

    void IndexSources(const std::vector<SourceFile>& sources)
    {
        std::vector<IndexedSource> candidates;
        candidates.reserve(sources.size());
        std::size_t total_bytes = 0;
        bool total_limit_reported = false;
        for (const auto& source : sources)
        {
            if (source.text.size() > limits_.maximumTotalInputBytes -
                    std::min(total_bytes, limits_.maximumTotalInputBytes))
            {
                if (!total_limit_reported)
                {
                    AddDiagnostic(
                        "idl.total_input_too_large",
                        "IDL project input exceeds the configured total byte limit.",
                        source.path,
                        EmptySpan()
                    );
                    total_limit_reported = true;
                }
                total_bytes = limits_.maximumTotalInputBytes;
            }
            else
            {
                total_bytes += source.text.size();
            }

            const auto normalized = NormalizeLogicalPath(
                source.path,
                false,
                limits_.maximumPathBytes
            );
            if (!normalized.ok())
            {
                AddDiagnostic(normalized.errorCode, normalized.errorMessage, source.path, EmptySpan());
                continue;
            }
            const auto import_root = NormalizeLogicalImportRoot(
                source.importRoot,
                limits_.maximumPathBytes
            );
            if (!import_root.ok())
            {
                AddDiagnostic(
                    import_root.errorCode,
                    import_root.errorMessage,
                    source.path,
                    EmptySpan()
                );
                continue;
            }
            candidates.push_back({
                .normalizedPath = normalized.value,
                .normalizedImportRoot = import_root.value,
                .source = &source,
            });
        }

        std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.normalizedPath != rhs.normalizedPath)
                return lhs.normalizedPath < rhs.normalizedPath;
            if (lhs.source->path != rhs.source->path) return lhs.source->path < rhs.source->path;
            return lhs.source->text < rhs.source->text;
        });
        for (const auto& candidate : candidates)
        {
            if (!candidate.normalizedImportRoot.empty())
                importRootPrefixes_.push_back(candidate.normalizedImportRoot);
        }
        std::sort(importRootPrefixes_.begin(), importRootPrefixes_.end());
        importRootPrefixes_.erase(
            std::unique(importRootPrefixes_.begin(), importRootPrefixes_.end()),
            importRootPrefixes_.end()
        );
        for (const auto& candidate : candidates)
        {
            const auto [existing, inserted] = sources_.emplace(
                candidate.normalizedPath,
                candidate.source
            );
            if (!inserted)
            {
                AddDiagnostic(
                    "idl.duplicate_source_path",
                    "Multiple source inputs normalize to the same project-relative path.",
                    candidate.source->path,
                    EmptySpan(),
                    {{
                        .message = "The first source with this normalized path is here.",
                        .sourceName = existing->second->path,
                        .span = EmptySpan(),
                    }}
                );
            }
            else
            {
                importRoots_.emplace(
                    candidate.normalizedPath,
                    candidate.normalizedImportRoot
                );
            }
        }
    }

    void Visit(
        const std::string& path,
        std::size_t depth,
        std::optional<EdgeLocation> incoming_edge
    )
    {
        if (depth > limits_.maximumImportDepth)
        {
            AddDiagnostic(
                "idl.import_depth_exceeded",
                "IDL import depth exceeds the configured limit.",
                incoming_edge.has_value() ? incoming_edge->sourceName : path,
                incoming_edge.has_value() ? incoming_edge->span : EmptySpan()
            );
            return;
        }
        if (const auto state = states_.find(path); state != states_.end())
        {
            if (state->second == VisitState::Visiting && incoming_edge.has_value())
                AddCycleDiagnostic(path, *incoming_edge);
            return;
        }

        states_.emplace(path, VisitState::Visiting);
        stackPaths_.push_back(path);
        stackEdges_.push_back(std::move(incoming_edge));

        const SourceFile& source = *sources_.at(path);
        auto parsed = Parse(path, source.text, limits_);
        if (!parsed.ok())
        {
            hadError_ = true;
            for (auto& diagnostic : parsed.diagnostics) AddDiagnostic(std::move(diagnostic));
        }
        else
        {
            ParsedSource parsed_source{
                .path = path,
                .document = std::move(*parsed.document),
                .normalizedText = std::move(parsed.normalizedText),
            };
            ResolveImports(parsed_source, importRoots_.at(path));
            auto [position, inserted] = parsed_.emplace(path, std::move(parsed_source));
            if (inserted)
            {
                std::vector<const ImportDeclaration*> imports;
                imports.reserve(position->second.document.imports.size());
                for (const auto& import : position->second.document.imports) imports.push_back(&import);
                std::sort(imports.begin(), imports.end(), [](const auto* lhs, const auto* rhs) {
                    return lhs->path < rhs->path;
                });
                for (const auto* import : imports)
                {
                    if (import->path.empty()) continue;
                    if (!sources_.contains(import->path))
                    {
                        AddDiagnostic(
                            "idl.import_source_missing",
                            "Imported source is not present in the project source set.",
                            path,
                            import->pathSpan
                        );
                        continue;
                    }
                    Visit(import->path, depth + 1, EdgeLocation{path, import->pathSpan});
                }
                position->second.normalizedText = Format(position->second.document);
            }
        }

        stackEdges_.pop_back();
        stackPaths_.pop_back();
        states_[path] = VisitState::Done;
        if (parsed_.contains(path)) dependencyOrder_.push_back(path);
    }

    void ResolveImports(ParsedSource& source, std::string_view import_root)
    {
        std::map<std::string, SourceSpan> normalized_paths;
        for (auto& import : source.document.imports)
        {
            const auto normalized = NormalizeLogicalPath(
                import.path,
                true,
                limits_.maximumPathBytes
            );
            if (!normalized.ok())
            {
                AddDiagnostic(
                    normalized.errorCode,
                    normalized.errorMessage,
                    source.path,
                    import.pathSpan
                );
                import.path.clear();
                continue;
            }
            import.path = normalized.value;
            // Registered import roots reserve their first path segment as an
            // explicit project-root qualifier; every other mounted import is
            // resolved relative to the source's own virtual root.
            const bool root_qualified = std::any_of(
                importRootPrefixes_.begin(),
                importRootPrefixes_.end(),
                [&](const std::string& candidate_root) {
                    return import.path.starts_with(candidate_root + '/');
                }
            );
            if (!import_root.empty() && !root_qualified)
            {
                const auto mounted = NormalizeLogicalPath(
                    std::string(import_root) + '/' + import.path,
                    true,
                    limits_.maximumPathBytes
                );
                if (!mounted.ok())
                {
                    AddDiagnostic(
                        mounted.errorCode,
                        mounted.errorMessage,
                        source.path,
                        import.pathSpan
                    );
                    import.path.clear();
                    continue;
                }
                import.path = mounted.value;
            }
            const auto [existing, inserted] = normalized_paths.emplace(import.path, import.pathSpan);
            if (!inserted)
            {
                AddDiagnostic(
                    "idl.duplicate_import",
                    "Imports must remain unique after path normalization.",
                    source.path,
                    import.pathSpan,
                    {{
                        .message = "The first normalized import is here.",
                        .sourceName = source.path,
                        .span = existing->second,
                    }}
                );
            }
        }
    }

    void AddCycleDiagnostic(const std::string& target, const EdgeLocation& current_edge)
    {
        std::vector<RelatedDiagnosticSpan> related;
        const auto start = std::find(stackPaths_.begin(), stackPaths_.end(), target);
        if (start != stackPaths_.end())
        {
            const std::size_t start_index = static_cast<std::size_t>(start - stackPaths_.begin());
            for (std::size_t index = start_index + 1; index < stackEdges_.size(); ++index)
            {
                if (!stackEdges_[index].has_value()) continue;
                related.push_back({
                    .message = "This import participates in the cycle.",
                    .sourceName = stackEdges_[index]->sourceName,
                    .span = stackEdges_[index]->span,
                });
            }
        }
        AddDiagnostic(
            "idl.import_cycle",
            "IDL imports must form an acyclic source graph.",
            current_edge.sourceName,
            current_edge.span,
            std::move(related)
        );
    }

    void AddDiagnostic(Diagnostic diagnostic)
    {
        if (diagnostic.severity == DiagnosticSeverity::Error) hadError_ = true;
        if (diagnostics_.size() >= limits_.maximumDiagnostics) return;
        diagnostics_.push_back(std::move(diagnostic));
    }

    void AddDiagnostic(
        std::string code,
        std::string message,
        std::string source_name,
        SourceSpan span,
        std::vector<RelatedDiagnosticSpan> related_spans = {}
    )
    {
        hadError_ = true;
        AddDiagnostic({
            .code = std::move(code),
            .severity = DiagnosticSeverity::Error,
            .message = std::move(message),
            .sourceName = std::move(source_name),
            .span = span,
            .relatedSpans = std::move(related_spans),
        });
    }

    ProjectParseResult Finish()
    {
        std::sort(diagnostics_.begin(), diagnostics_.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.sourceName != rhs.sourceName) return lhs.sourceName < rhs.sourceName;
            if (lhs.span.begin.offset != rhs.span.begin.offset)
                return lhs.span.begin.offset < rhs.span.begin.offset;
            if (lhs.code != rhs.code) return lhs.code < rhs.code;
            return lhs.message < rhs.message;
        });
        if (hadError_)
            return {.project = std::nullopt, .diagnostics = std::move(diagnostics_)};

        Project project{.entryPath = entryPath_};
        project.sources.reserve(dependencyOrder_.size());
        for (const auto& path : dependencyOrder_)
            project.sources.push_back(std::move(parsed_.at(path)));
        return {
            .project = std::move(project),
            .diagnostics = std::move(diagnostics_),
        };
    }

    const ParseLimits& limits_;
    std::map<std::string, const SourceFile*> sources_;
    std::map<std::string, std::string> importRoots_;
    std::vector<std::string> importRootPrefixes_;
    std::map<std::string, VisitState> states_;
    std::map<std::string, ParsedSource> parsed_;
    std::vector<std::string> dependencyOrder_;
    std::vector<std::string> stackPaths_;
    std::vector<std::optional<EdgeLocation>> stackEdges_;
    std::vector<Diagnostic> diagnostics_;
    std::string entryPath_;
    bool hadError_ = false;
};
}

bool ProjectParseResult::ok() const
{
    return project.has_value() && std::none_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        }
    );
}

ProjectParseResult ParseProject(
    std::string_view entry_path,
    const std::vector<SourceFile>& sources,
    const ParseLimits& limits
)
{
    return ProjectParser(limits).Run(entry_path, sources);
}
}
