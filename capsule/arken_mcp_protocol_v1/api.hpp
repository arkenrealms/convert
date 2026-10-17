#pragma once
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arken::mcp::v1 {
using Json = ::arken::schema::cpp::v1::tool_json::Json;
inline constexpr std::string_view protocolVersion = "2025-11-25";
inline constexpr std::size_t maximumRequestBytes = 16384;
inline constexpr std::size_t maximumConfiguredRequestBytes = 2u * 1024u * 1024u;
inline constexpr std::size_t defaultCatalogMaximumBytes = 2u * 1024u * 1024u;
inline constexpr std::size_t maximumConfiguredCatalogBytes = 4u * 1024u * 1024u;
inline constexpr std::string_view familyEndpointPath = "/v1";
enum class ToolNameStyle { qualified, familyLocal };
// Presentation only. Canonical generated command identity remains unchanged.
[[nodiscard]] std::string shortToolName(std::string_view qualifiedName);
// Native clients retain canonical requests/receipts; project only the outgoing
// protocol envelope. This does not accept two public spellings on the server.
[[nodiscard]] Json familyRequest(const Json& canonicalRequest);

struct ToolPresentation {
  std::uint32_t commandId = 0;
  bool readOnly = false;
  bool destructive = false;
  bool openWorld = false;
  std::string description;
  // Trusted optional full-envelope restriction; zero inherits the server.
  std::size_t requestMaximumBytes = 0;
  // Optional trusted presentation name (for aggregate family prefixes). Only
  // this spelling is accepted on the wire; canonical dispatch is unchanged.
  std::string publicName{};
};
struct ServerDescription {
  std::string name, version, instructions;
  std::string_view catalog;
  std::vector<ToolPresentation> presentations;
  // Trusted owner policy, never read from request metadata. Existing services
  // keep their 16 KiB envelope unless explicitly qualified for a larger one.
  std::size_t requestMaximumBytes = maximumRequestBytes;
  ToolNameStyle toolNameStyle{ToolNameStyle::qualified};
  // Trusted static schema admission, independent of caller request budgets.
  std::size_t catalogMaximumBytes = defaultCatalogMaximumBytes;
};
struct RequestPlan {
  bool isCommand = false;
  Json response;
  Json id;
  std::uint32_t commandId = 0;
  std::string commandName;
  Json arguments;
  Json metadata;
};

// Trusted native catalog identities, never assembled from a caller request.
// The observation contract is separate from dispatch and grants no authority.
struct CallSummaryTool {
  std::string publicName, canonicalName;
};
enum class CostExchange { operation, quote };
// Private metadata only. The operation digest must already be one-way; never
// pass a public file/operation capability as operationDigest.
[[nodiscard]] Json operationCostLink(std::string_view serviceId, std::string_view operationDigest);
// Domain separation shared by file owners and protected receipt readers.
// The caller hashes the returned preimage with its existing SHA-256 owner.
[[nodiscard]] std::string operationCostPreimage(std::string_view serviceId, std::string_view operationId);
[[nodiscard]] Json summarizeCall(const Json&, std::string_view serviceId,
                                 int httpStatus, std::span<const CallSummaryTool>);

// Pure protocol planning. A tool call never executes, authenticates, reserves a
// budget or starts a provider here. The owning domain must admit it separately.
class Server {
public:
  explicit Server(ServerDescription);
  [[nodiscard]] RequestPlan plan(std::string_view) const;
  [[nodiscard]] std::uint32_t resolve(std::string_view) const noexcept;
  // Owner composition/invocation, never a fallback for public wire admission.
  [[nodiscard]] std::uint32_t resolveCanonical(std::string_view) const noexcept;
  [[nodiscard]] std::string_view canonicalCommandName(std::uint32_t) const noexcept;
  // Private, closed completion metadata. No execution, arguments, caller IDs,
  // credentials or arbitrary names. serviceId is supplied by the native owner.
  // Non-object envelopes or invalid owner/status inputs produce null.
  [[nodiscard]] Json callSummary(const Json& request, std::string_view serviceId,
                                 int httpStatus) const;
  [[nodiscard]] Json costContext(std::uint32_t commandId, std::string_view serviceId,
                                std::string_view release, std::size_t canonicalArgumentBytes,
                                CostExchange exchange, int httpStatus) const;

private:
  ServerDescription description_;
  Json commands_, tools_;
  std::vector<CallSummaryTool> summaryTools_;
};

[[nodiscard]] Json parseRequestJson(std::string_view);
// Explicit owner-selected budget; parseRequestJson retains its 16 KiB default.
[[nodiscard]] Json parseBoundedJson(std::string_view, std::size_t maximumBytes);
[[nodiscard]] Json failure(const Json &id, int code, std::string_view message);
[[nodiscard]] Json reply(const Json &id, Json result, bool initialize = false);
[[nodiscard]] Json toolReply(const Json &id, const Json &result, bool error);
[[nodiscard]] std::string responsePrefix(const Json &id);

struct HttpRequest {
  std::string method, path, origin, originHeader, contentType, accept, version;
  bool hasOrigin = false;
};
struct HttpAdmission {
  int status = 200;
  std::string code;
  [[nodiscard]] bool ok() const noexcept { return status == 200; }
};
// Stateless JSON-response profile. GET receives 405 (no SSE stream). Version
// absence is checked after parsing: only initialize may omit its header.
// The host selects one endpoint path; this is not a client-supplied alias.
[[nodiscard]] HttpAdmission admitHttp(const HttpRequest &,std::string_view endpointPath="/mcp");
} // namespace arken::mcp::v1
