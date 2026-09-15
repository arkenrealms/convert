#pragma once
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arken::mcp::v1 {
using Json = ::arken::schema::cpp::v1::tool_json::Json;
inline constexpr std::string_view protocolVersion = "2025-11-25";
inline constexpr std::size_t maximumRequestBytes = 16384;

struct ToolPresentation {
  std::uint32_t commandId = 0;
  bool readOnly = false;
  bool destructive = false;
  bool openWorld = false;
  std::string description;
};
struct ServerDescription {
  std::string name, version, instructions;
  std::string_view catalog;
  std::vector<ToolPresentation> presentations;
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

// Pure protocol planning. A tool call never executes, authenticates, reserves a
// budget or starts a provider here. The owning domain must admit it separately.
class Server {
public:
  explicit Server(ServerDescription);
  [[nodiscard]] RequestPlan plan(std::string_view) const;
  [[nodiscard]] std::uint32_t resolve(std::string_view) const noexcept;

private:
  ServerDescription description_;
  Json commands_, tools_;
};

[[nodiscard]] Json parseRequestJson(std::string_view);
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
[[nodiscard]] HttpAdmission admitHttp(const HttpRequest &);
} // namespace arken::mcp::v1
