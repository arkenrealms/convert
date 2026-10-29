#pragma once
#include "arken_cloudflare_client_v1/api.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace arken::conversion::cloudflare {
namespace fs=std::filesystem;
using Json=nlohmann::json;
using arken::cloudflare::v1::Require;
using arken::cloudflare::v1::Read;
using arken::cloudflare::v1::Write;
using arken::cloudflare::v1::PrivateDirectory;
using arken::cloudflare::v1::Hash;
using arken::cloudflare::v1::Random;
using arken::cloudflare::v1::Process;
using arken::cloudflare::v1::Lock;
using arken::cloudflare::v1::Response;
using arken::cloudflare::v1::Request;
using arken::cloudflare::v1::Transport;
using arken::cloudflare::v1::CurlTransport;
Json Api(const Transport&,std::string_view token,std::string_view method,std::string_view path,const Json& body=nullptr);
inline constexpr std::string_view account="120209abb139e1ee59612f72924b9c5e";
inline constexpr std::string_view worker="megaconvert-mcp-preview";
inline constexpr std::string_view bucket="megaconvert-preview-artifacts";
std::string AccountPath();
std::string WorkerPath();
std::string BucketPath();
Json Package(const fs::path& repo,const fs::path& wasm);
Json SandboxPackage(const fs::path& repo,const fs::path& wasm);
Json ProductionPackage(const fs::path& repo,const fs::path& wasm);
Json StageProductionPackage(const fs::path& repo,const fs::path& wasm,const fs::path& output);
using arken::cloudflare::v1::DownloadedModules;
enum class ResourceKind {worker, bucket};
void RequireResourceAbsent(const Response&,const std::string& path,ResourceKind kind=ResourceKind::worker);
Json UploadModules(const Transport&,std::string_view token,const std::string& path,const Json& package,const Json& metadata,bool version);
void ValidatePlan(const Json&,const Json& package);
Json Plan(const Transport&,std::string_view token,const fs::path& repo,const fs::path& wasm);
Json Apply(const Transport&,std::string_view token,const fs::path& state,std::string_view expectedHash,const Json& currentPackage);
Json Smoke(const Transport&,const Json& plan,const fs::path& state);
Json Audit(const Transport&,std::string_view token,const Json& plan,const fs::path& state);
Json VerifyDownloadedPackage(std::string_view multipart,const Json& package);
void SelfTest(const fs::path&);
inline constexpr std::string_view sandboxWorker="arken-convert-sandbox";
std::string SandboxPath();
Json SandboxMetadata(const Json& package,std::string_view secret);
Json SandboxLifecycle(const Json& existing);
bool SameSandboxLifecycle(const Json&,const Json&);
void ValidateSandboxCandidate(const Json& frozen,const Json& current,std::string_view changedPaths);
void ValidateSandboxProof(const Json& package,const Json& proof);
Json SandboxPlan(const Transport&,std::string_view token,const fs::path& state,const fs::path& previewState,const fs::path& repo,const fs::path& wasm,const fs::path& proof);
Json SandboxApply(const Transport&,std::string_view token,const fs::path& state,std::string_view expectedHash,const Json& currentPackage);
Json SandboxRecover(const Transport&,std::string_view token,const fs::path& state);
Json SandboxObserve(const Transport&,std::string_view token,const fs::path& state);
Json SandboxVersionsObserve(const Transport&,std::string_view token,const fs::path& state);
Json SandboxAudit(const Transport&,std::string_view token,const Json& plan,const fs::path& state);
Json SandboxSmoke(const Transport&,const Json& plan,const fs::path& state);
Json PublicSmoke(const Transport&,const Json& plan,const fs::path& state);
void PublicSmokeTests(const fs::path&);
void SandboxTests(const fs::path&);
Json SandboxUpdatePlan(const Transport&,std::string_view,const fs::path& state,const fs::path& previousState,const fs::path& repo,const fs::path& wasm,const fs::path& proof);
Json SandboxUpdateApply(const Transport&,std::string_view,const fs::path&,std::string_view expectedHash,const Json& package);
Json SandboxUpdateRecover(const Transport&,std::string_view,const fs::path&);
void SandboxUpdateTests(const fs::path&);
Json CancelUnstartedSandboxUpdate(const fs::path&,std::string_view expectedHash);
}
