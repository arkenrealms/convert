#pragma once
#include "api.hpp"
#include <functional>
#include <iosfwd>
namespace arken::mcp::v1 {
struct StdioLimits {
 std::size_t requestBytes=maximumRequestBytes;
 std::size_t responseBytes=maximumConfiguredCatalogBytes;
};
// The domain dispatcher returns the existing response envelope. Only its MCP
// body goes to stdout; empty notification responses produce no output frame.
int serveStdio(std::istream&,std::ostream&,std::ostream& diagnostics,
 const StdioLimits&,const std::function<Json(std::string_view)>& dispatch) noexcept;
}
