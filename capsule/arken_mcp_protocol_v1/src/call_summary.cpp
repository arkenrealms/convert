#include "arken_mcp_protocol_v1/api.hpp"
#include <algorithm>

namespace arken::mcp::v1 {
Json Server::callSummary(const Json& request, std::string_view serviceId,
                         int httpStatus) const {
  return summarizeCall(request, serviceId, httpStatus, summaryTools_);
}
Json summarizeCall(const Json& request, std::string_view serviceId,
                   int httpStatus, std::span<const CallSummaryTool> tools) {
  if (!request.is_object() || httpStatus < 100 || httpStatus > 599 ||
      serviceId.empty() || serviceId.size() > 40 ||
      !std::all_of(serviceId.begin(), serviceId.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
      })) return nullptr;
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
