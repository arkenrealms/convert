#include "arken_paid_mcp_cloudflare_v1/source_package_files.hpp"
#include <map>
#include <regex>

namespace arken::paid::cloudflare::v1 {
C::Json PrepareSourcePackageFiles(std::string_view family, std::string_view mainModule,
    const C::fs::path& source, const std::vector<C::WorkerModule>& modules,
    const C::fs::path& output) {
    C::Require(std::regex_match(std::string(family), std::regex("[a-z][a-z0-9-]{0,63}")), "source_package_family_invalid");
    const auto root = C::fs::canonical(source);
    C::Require(output.is_absolute() && output == output.lexically_normal()
        && !C::fs::exists(output) && !C::fs::is_symlink(C::fs::symlink_status(output)),
        "source_package_new_output_required");
    C::Require(!modules.empty() && modules.size() <= 32, "source_package_module_count");
    struct Member {std::string type, bytes; bool proof;};
    std::map<std::string, Member> frozen;
    std::size_t total = 0;
    std::string mappings;
    for (const auto& module : modules) {
        C::Require(std::regex_match(module.name, std::regex("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}"))
            && module.name != "candidate.json" && module.name != "source-inputs.txt" && !frozen.contains(module.name),
            "source_package_module_name");
        const bool wasm = module.mediaType == "application/wasm" && module.name.ends_with(".wasm");
        const bool script = module.mediaType == "application/javascript+module" && module.name.ends_with(".js");
        const bool notice = module.mediaType == "text/plain" && module.name.ends_with(".txt");
        C::Require(wasm || script || notice, "source_package_module_type");
        C::Require(module.source.is_absolute() && !C::fs::is_symlink(C::fs::symlink_status(module.source))
            && C::fs::is_regular_file(module.source) && C::fs::canonical(module.source) == module.source.lexically_normal(),
            "source_package_physical_input_required");
        if (!wasm) {
            const auto relative = module.source.lexically_relative(root);
            C::Require(!relative.empty() && *relative.begin() != "..", "source_package_source_escape");
        }
        const auto input = module.source.string(), staged = (output / module.name).string();
        C::Require(input.find_first_of("|\r\n") == input.npos && staged.find_first_of("|\r\n") == staged.npos,
            "source_package_mapping_path_invalid");
        mappings += input + "|" + staged + "\n";
        auto bytes = C::Read(module.source, C::workerModuleByteLimit);
        C::Require(!bytes.empty() && (!wasm || bytes.starts_with(std::string("\0asm\1\0\0\0", 8))),
            "source_package_module_bytes");
        C::Require(bytes.size() <= C::workerPackageByteLimit - total, "source_package_byte_limit");
        total += bytes.size();
        frozen.emplace(module.name, Member{module.mediaType, std::move(bytes), module.requiresProof});
    }
    C::Require(frozen.contains(std::string(mainModule)), "source_package_entry_missing");
    C::Json files = C::Json::array();
    for (const auto& [name, member] : frozen)
        files.push_back({{"name", name}, {"mediaType", member.type}, {"bytes", member.bytes.size()},
            {"sha256", C::Hash(member.bytes)}, {"requiresProof", member.proof}});
    C::Json result{{"schema", "arken.paid-mcp.source-package.v1"}, {"status", "prepared"},
        {"scope", "local-qualification-only"}, {"deploymentAuthorized", false}, {"runtimeQualified", false},
        {"family", std::string(family)}, {"mainModule", std::string(mainModule)}, {"files", files},
        {"bytes", total}, {"packageSha256", C::Hash(files.dump())}, {"cloudMutations", 0}};
    C::PrivateDirectory(output);
    for (const auto& [name, member] : frozen) {
        C::Write(output / name, member.bytes, true);
        C::Require(C::Read(output / name, C::workerModuleByteLimit) == member.bytes, "source_package_write_changed");
    }
    C::Write(output / "source-inputs.txt", mappings, true);
    // The inventory is the commit marker. Existing outputs are never replaced.
    C::Write(output / "candidate.json", result.dump(2) + '\n', true);
    return result;
}
}
