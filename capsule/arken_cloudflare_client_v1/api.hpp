#pragma once
#include <filesystem>
#include <exception>
#include <functional>
#include <iosfwd>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace arken::cloudflare::v1 {
namespace fs=std::filesystem;
using Json=nlohmann::json;
void Require(bool,std::string_view);
// Preserve owned numeric transport/provider codes, never exception payloads,
// URLs, request bodies or credentials. JSON failures expose only their type id.
std::string PublicErrorCode(const std::exception&,std::string_view fallback="cloudflare_command_failed");
std::string Read(const fs::path&,std::size_t limit=16*1024*1024,bool secret=false);
// Opt-in strict custody read: absolute normalized path, no symlink at any
// component, private effective-user-owned parent and single-link regular file.
// Does not create, discover, copy, or change permissions on credentials.
inline constexpr std::size_t workerQualificationByteLimit=4u*1024u*1024u;
// Generated module custody may need more bytes than a qualification report.
// Callers still select their own narrower bound; this is the hard I/O ceiling.
inline constexpr std::size_t privateFileByteLimit=16u*1024u*1024u;
std::string ReadPrivateFile(const fs::path&, std::size_t limit);
void Write(const fs::path&,std::string_view,bool exclusive=false);
// Owner-only, no-link directory traversal; exclusive durable publication.
// A post-install durability error reports private_file_commit_uncertain.
// Larger generated release evidence opts in; ordinary writes keep the 1 MiB cap.
void WritePrivateFileExclusive(const fs::path&, std::string_view, std::size_t limit=1024u*1024u);
enum class PrivateFileCommit { confirmed, conflict, busy, rejected, uncertain };
// Cooperative, private single-writer compare/exchange. The final guard runs
// under the lock; no provider I/O belongs in it. Equal pre/post bytes reconcile
// durability without rewriting. Uncertain never authorizes rollback/replay.
PrivateFileCommit CompareExchangePrivateFile(const fs::path&, std::string_view expected,
    std::string_view replacement, std::function<bool()> const& final_guard);
void PrivateDirectory(const fs::path&);
// Bounded stdin capture to a new owner-only file; never overwrites a credential
// or emits its value. The caller separately verifies authorization with the API.
void CaptureApiToken(const fs::path&,std::istream&);
std::string Hash(std::string_view);
std::string Random();
std::string Process(std::vector<std::string>, int inputFd = -1);
struct Lock {int fd=-1;explicit Lock(const fs::path&);~Lock();Lock(const Lock&)=delete;Lock& operator=(const Lock&)=delete;};
struct Response {int status=0;std::string body;std::map<std::string,std::string> headers{};};
struct Request {std::string method,url,body,type="application/json";std::vector<std::string> headers;};
struct HttpOptions {std::size_t maximumResponseBytes=16u*1024u*1024u;unsigned timeoutSeconds=45;bool compressedResponses=false;bool captureResponseHeaders=false;std::vector<std::string> forbiddenResponseHeaders{};bool http11=false;std::string dnsOverHttpsUrl{};};
// Bounded final-response content/security headers; never cookies or credentials.
std::map<std::string,std::string> HttpObservationHeaders(std::string_view);
// Opt-in final-response prohibition. Header values are never returned or logged.
void RejectResponseHeaders(std::string_view,const std::vector<std::string>&);
using Transport=std::function<Response(const Request&)>;
// One-shot HTTPS only. Credentials remain in owner-only temporary curl config,
// never argv or returned diagnostics. No implicit mutation retries or redirects.
Transport CurlTransport(const fs::path& scratch,HttpOptions={});
std::map<std::string,std::string> DownloadedModules(std::string_view multipart);
}
