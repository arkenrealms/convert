#include "arken_mcp_protocol_v1/api.hpp"
#include <algorithm>

namespace arken::mcp::v1 {
namespace {
bool serviceIdentity(std::string_view value) {
  return !value.empty() && value.size() <= 40 &&
      std::ranges::all_of(value, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
      });
}
bool digestIdentity(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char c) {
    return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
  });
}
}
Json Server::costContext(std::uint32_t commandId, std::string_view serviceId,
                        std::string_view release, std::size_t bytes,
                        CostExchange exchange, int status) const {
  const auto command = canonicalCommandName(commandId);
  if (command.empty() || !serviceIdentity(serviceId) || !digestIdentity(release) ||
      status < 100 || status > 599 ||
      (exchange != CostExchange::quote && exchange != CostExchange::operation)) return nullptr;
  return {{"schema", "arken.mcp.cost-context.v1"}, {"service", serviceId},
          {"command", command}, {"release", release},
          {"inputClass", bytes <= 1024 ? "json-le-1k" : bytes <= 16384 ? "json-le-16k" : bytes <= 262144 ? "json-le-256k" : "json-over-256k"},
          {"exchangeKind", exchange == CostExchange::quote ? "quote" : "operation"}, {"httpStatus", status}};
}
Json operationCostLink(std::string_view serviceId, std::string_view digest) {
  if (!serviceIdentity(serviceId) || !digestIdentity(digest)) return nullptr;
  return {{"schema", "arken.mcp.operation-cost-link.v1"}, {"service", serviceId}, {"operation", digest}};
}
std::string operationCostPreimage(std::string_view serviceId, std::string_view operationId) {
  if (!serviceIdentity(serviceId) || !digestIdentity(operationId)) return {};
  return "arken.mcp.operation-cost.v1/" + std::string(serviceId) + "/" + std::string(operationId);
}
Json Server::callSummary(const Json& request, std::string_view serviceId,
                         int httpStatus) const {
  return summarizeCall(request, serviceId, httpStatus, summaryTools_);
}
Json summarizeCall(const Json& request, std::string_view serviceId,
                   int httpStatus, std::span<const CallSummaryTool> tools) {
  if (!request.is_object() || httpStatus < 100 || httpStatus > 599 ||
      !serviceIdentity(serviceId)) return nullptr;
  std::string method = "unknown", tool;
  const auto supplied = request.find("method");
  if (supplied != request.end() && supplied->is_string())
    for (const auto name : {"initialize", "ping", "tools/list", "tools/call",
                           "notifications/initialized", "notifications/cancelled"})
      if (*supplied == name) { method = name; break; }
  if (method == "tools/call") {
    tool = "unknown";
    const auto params = request.find("params");
    if (params != request.end() && params->is_object()) {
      const auto name = params->find("name");
      if (name != params->end() && name->is_string()) {
        const auto command = std::ranges::find(tools,
            name->get_ref<const std::string&>(), &CallSummaryTool::publicName);
        if (command != tools.end()) tool = command->canonicalName;
      }
    }
  }
  return {{"schema", "arken.mcp.call.v1"}, {"service", serviceId},
          {"method", method}, {"tool", tool}, {"httpStatus", httpStatus}};
}
} // namespace arken::mcp::v1
