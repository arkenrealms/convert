#include "arken_mcp_protocol_v1/api.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace arken::mcp::v1 {
namespace J = ::arken::schema::cpp::v1::tool_json;
Json parseRequestJson(std::string_view text) {
  return parseBoundedJson(text, maximumRequestBytes);
}
Json parseBoundedJson(std::string_view text, std::size_t maximumBytes) {
  J::require(maximumBytes > 0 && maximumBytes <= maximumConfiguredRequestBytes && text.size() <= maximumBytes, "MCP packet byte limit");
  std::vector<std::set<std::string>> keys;
  return Json::parse(
      text.begin(), text.end(),
      [&](int depth, Json::parse_event_t event, Json &value) {
        J::require(depth <= 32, "MCP request depth limit");
        if (event == Json::parse_event_t::object_start)
          keys.emplace_back();
        if (event == Json::parse_event_t::key)
          J::require(!keys.empty() &&
                         keys.back().insert(value.get<std::string>()).second,
                     "duplicate MCP key");
        if (event == Json::parse_event_t::object_end)
          keys.pop_back();
        return true;
      });
}
Json failure(const Json &id, int code, std::string_view text) {
  return {{"kind", "response"},
          {"status", 200},
          {"body", Json{{"jsonrpc", "2.0"},
                        {"id", id},
                        {"error", {{"code", code}, {"message", text}}}}
                       .dump()}};
}
Json reply(const Json &id, Json result, bool initialize) {
  return {{"kind", "response"},
          {"status", 200},
          {"initialize", initialize},
          {"body",
           Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}
               .dump()}};
}
Json toolReply(const Json &id, const Json &value, bool error) {
  Json result = {
      {"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})}};
  if (error)
    result["isError"] = true;
  else
    result["structuredContent"] = value;
  return reply(id, std::move(result));
}
std::string responsePrefix(const Json &id) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + id.dump() + ",\"result\":";
}
Server::Server(ServerDescription description)
    : description_(std::move(description)) {
  J::require(!description_.name.empty() && !description_.version.empty(),
             "MCP server identity required");
  J::require(description_.requestMaximumBytes > 0 &&
                 description_.requestMaximumBytes <= maximumConfiguredRequestBytes,
             "MCP server request budget invalid");
  J::require(description_.catalog.size() <= 2u * 1024u * 1024u,
             "catalog byte budget");
  const auto catalog = Json::parse(description_.catalog);
  J::require(catalog.at("schema") == "arken.schema.command-catalog.v1" &&
                 catalog.at("jsonProfile") == "arken.schema.tool-json.v1",
             "generated catalog required");
  J::require(catalog.at("semanticFingerprint").is_string() &&
                 !catalog.at("semanticFingerprint")
                      .get_ref<const std::string &>()
                      .empty(),
             "catalog fingerprint required");
  commands_ = catalog.at("commands");
  tools_ = Json::array();
  J::require(commands_.is_array() && commands_.size() <= 4096,
             "catalog command budget");
  std::set<std::string> names;
  std::set<std::uint32_t> ids, presentations;
  for (const auto &presentation : description_.presentations)
    J::require(presentations.insert(presentation.commandId).second,
               "duplicate tool presentation");
  for (const auto &command : commands_) {
    const auto name = command.at("name").get<std::string>();
    const auto id = J::readScalar<std::uint32_t>(command.at("commandId"));
    J::require(!name.empty() && id != 0 && names.insert(name).second &&
                   ids.insert(id).second,
               "duplicate or invalid command identity");
    J::require(command.at("ownerCapsule").is_string() &&
                   !command.at("ownerCapsule")
                        .get_ref<const std::string &>()
                        .empty() &&
                   command.at("streaming") == "unary",
               "unowned or unsupported command");
    const auto presentation = std::ranges::find(description_.presentations, id,
                                                &ToolPresentation::commandId);
    J::require(presentation != description_.presentations.end(),
               "explicit tool behavior required");
    const auto idempotency = command.at("idempotency").get<std::string>();
    J::require(idempotency == "safe" || idempotency == "idempotent" ||
                   idempotency == "none",
               "unknown idempotency");
    J::require(!presentation->readOnly || idempotency == "safe",
               "read-only presentation requires safe command");
    tools_.push_back({{"name", name},
                      {"description", presentation->description.empty()
                                          ? command.at("description")
                                          : Json(presentation->description)},
                      {"inputSchema", command.at("inputSchema")},
                      {"outputSchema", command.at("outputSchema")},
                      {"annotations",
                       {{"readOnlyHint", presentation->readOnly},
                        {"destructiveHint", presentation->destructive},
                        {"idempotentHint", idempotency != "none"},
                        {"openWorldHint", presentation->openWorld}}}});
  }
  J::require(ids == presentations, "foreign tool presentation");
}
std::uint32_t Server::resolve(std::string_view name) const noexcept {
  for (const auto &command : commands_)
    if (command.at("name").get_ref<const std::string &>() == name)
      return command.at("commandId").get<std::uint32_t>();
  return 0;
}
RequestPlan Server::plan(std::string_view text) const {
  Json id = nullptr;
  const auto fail = [&](int code, std::string_view message) {
    RequestPlan result;
    result.response = failure(id, code, message);
    return result;
  };
  try {
    const auto request = parseBoundedJson(text, description_.requestMaximumBytes);
    if (!request.is_object() || !request.contains("jsonrpc") ||
        request.at("jsonrpc") != "2.0")
      return fail(-32600, "Invalid request");
    if (request.contains("id")) {
      id = request.at("id");
      if (!id.is_string() && !id.is_number_integer()) {
        id = nullptr;
        return fail(-32600, "Invalid request ID");
      }
      if (id.is_string() && id.get_ref<const std::string &>().size() > 128) {
        id = nullptr;
        return fail(-32600, "Request ID too long");
      }
    }
    if (!request.contains("method") || !request.at("method").is_string())
      return fail(-32600, "Invalid method");
    const auto method = request.at("method").get<std::string>();
    if (!request.contains("id")) {
      RequestPlan result;
      result.response = {{"kind", "response"},
                         {"status", method == "notifications/initialized" ||
                                            method == "notifications/cancelled"
                                        ? 202
                                        : 400},
                         {"body", ""}};
      return result;
    }
    const auto params = request.value("params", Json::object());
    if (!params.is_object())
      return fail(-32602, "Parameters must be an object");
    const auto respond = [&](Json value, bool initialize = false) {
      RequestPlan result;
      result.response = reply(id, std::move(value), initialize);
      return result;
    };
    if (method == "initialize") {
      if (!params.contains("protocolVersion") ||
          !params.at("protocolVersion").is_string() ||
          !params.contains("capabilities") ||
          !params.at("capabilities").is_object() ||
          !params.contains("clientInfo") ||
          !params.at("clientInfo").is_object() ||
          !params.at("clientInfo").contains("name") ||
          !params.at("clientInfo").at("name").is_string() ||
          !params.at("clientInfo").contains("version") ||
          !params.at("clientInfo").at("version").is_string())
        return fail(-32602, "Invalid initialization parameters");
      return respond(
          {{"protocolVersion", protocolVersion},
           {"capabilities", {{"tools", Json::object()}}},
           {"serverInfo",
            {{"name", description_.name}, {"version", description_.version}}},
           {"instructions", description_.instructions}},
          true);
    }
    if (method == "ping")
      return respond(Json::object());
    if (method == "tools/list")
      return respond({{"tools", tools_}});
    if (method != "tools/call")
      return fail(-32601, "Method not found");
    const auto name = params.at("name").get<std::string>();
    const auto commandId = resolve(name);
    if (!commandId)
      return fail(-32602, "Unknown tool");
    RequestPlan plan;
    plan.id = id;
    plan.commandName = name;
    plan.commandId = commandId;
    plan.arguments = params.value("arguments", Json::object());
    plan.metadata = params.value("_meta", Json::object());
    J::require(plan.arguments.is_object() && plan.metadata.is_object(),
               "command arguments and metadata must be objects");
    plan.isCommand = true;
    return plan;
  } catch (const nlohmann::json::parse_error &) {
    id = nullptr;
    return fail(-32700, "Invalid JSON");
  } catch (const std::exception &) {
    return fail(-32602, "Invalid command arguments");
  }
}
namespace {
std::string media(std::string value) {
  value.resize(value.find(';') == std::string::npos ? value.size()
                                                    : value.find(';'));
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}
bool accepts(std::string_view text, std::string_view type) {
  while (!text.empty()) {
    const auto end = text.find(',');
    const auto item = text.substr(0, end);
    if (media(std::string(item)) == type) {
      bool enabled = true, seen = false;
      auto offset = item.find(';');
      while (offset != std::string_view::npos) {
        const auto next = item.find(';', offset + 1);
        const auto parameter = item.substr(
            offset + 1,
            next == std::string_view::npos ? next : next - offset - 1);
        const auto equals = parameter.find('=');
        if (equals != std::string_view::npos &&
            media(std::string(parameter.substr(0, equals))) == "q") {
          if (seen) {
            enabled = false;
            break;
          }
          seen = true;
          const auto q = media(std::string(parameter.substr(equals + 1)));
          if (q == "1")
            enabled = true;
          else if (q.size() >= 3 && q.size() <= 5 &&
                   (q[0] == '0' || q[0] == '1') && q[1] == '.') {
            bool valid = true, nonzero = false;
            for (std::size_t i = 2; i < q.size(); ++i) {
              valid = valid && q[i] >= '0' && q[i] <= '9';
              nonzero = nonzero || q[i] != '0';
            }
            enabled = valid && (q[0] == '0' ? nonzero : !nonzero);
          } else
            enabled = false;
        }
        offset = next;
      }
      if (enabled)
        return true;
    }
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end + 1);
  }
  return false;
}
} // namespace
HttpAdmission admitHttp(const HttpRequest &request,std::string_view endpointPath) {
  if(endpointPath!="/"&&endpointPath!="/mcp")return {503,"endpoint_configuration_invalid"};
  if (request.hasOrigin && request.originHeader != request.origin)
    return {403, "origin_denied"};
  if (request.path != endpointPath)
    return {404, "not_found"};
  if (request.method != "POST")
    return {405, "method_not_allowed"};
  if (media(request.contentType) != "application/json")
    return {415, "json_required"};
  if (!request.version.empty() && request.version != protocolVersion)
    return {400, "protocol_version_unsupported"};
  if (!accepts(request.accept, "application/json") ||
      !accepts(request.accept, "text/event-stream"))
    return {406, "accept_header_required"};
  return {};
}
} // namespace arken::mcp::v1
