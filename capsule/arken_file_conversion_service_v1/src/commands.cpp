#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
#include "arken_schema_generated/command-json.hpp"
namespace arken::file_conversion::service::v1 {
namespace image = arken::image::v1;
namespace J = arken::schema::cpp::v1::tool_json;
namespace S = arken::schema::generated::conversion::v1;
namespace mcp = arken::mcp::v1;
using Json = J::Json;
namespace {
mcp::Server makeServer(ServerMode mode) {
  mcp::ServerDescription description{"Arken Convert",
                      "r4553",
                      std::string(Surface(mode).instructions),
                      S::commandCatalog,
                      {{1, false, false, false, {}},
                       {2, false, false, mode != ServerMode::preview, {}}}};
  description.toolNameStyle=mcp::ToolNameStyle::familyLocal;
  return mcp::Server(std::move(description));
}
const mcp::Server &server(ServerMode mode) {
  switch (mode) {
  case ServerMode::preview: {
    static const auto value = makeServer(mode);
    return value;
  }
  case ServerMode::sandbox: {
    static const auto value = makeServer(mode);
    return value;
  }
  case ServerMode::production: {
    static const auto value = makeServer(mode);
    return value;
  }
  }
  throw std::invalid_argument("Unknown conversion mode");
}
} // namespace
std::string Catalog() { return std::string(S::commandCatalog); }
std::string PlanRequest(std::string_view text, ServerMode mode) noexcept {
  Json id = nullptr;
  try {
    const bool payments = mode != ServerMode::preview;
    const auto plan = server(mode).plan(text);
    if (!plan.isCommand)
      return plan.response.dump();
    id = plan.id;
    const auto commandId = plan.commandId;
    const auto &input = plan.arguments;
    Json args;
    if (commandId == 1) {
      const auto value = J::read<S::upload_request>(input);
      const auto output = static_cast<image::Format>(value.output_format);
      const auto quality = value.jpeg_quality ? value.jpeg_quality->value : 95u;
      const auto alpha =
          value.alpha_policy
              ? (static_cast<std::uint32_t>(*value.alpha_policy) - 1u)
              : 0u;
      J::require(output == image::Format::jpeg ||
                     (!value.jpeg_quality && alpha == 0),
                 "JPEG options require JPEG output");
      args = J::write(value);
      args["codec"] = {
          {"input", static_cast<std::uint32_t>(value.input_format)},
          {"output", static_cast<std::uint32_t>(value.output_format)},
          {"quality", quality},
          {"alpha", alpha}};
    } else if (commandId == 2)
      args = J::write(J::read<S::file_target>(input));
    else
      return mcp::failure(id, -32602, "Unknown tool").dump();
    Json payment = nullptr;
    if (plan.metadata.contains("x402/payment")) {
      J::require(payments && commandId == 2,
                 "payment not accepted by this tool");
      const auto &payload = plan.metadata.at("x402/payment");
      J::require(payload.is_object(), "payment must be an object");
      payment = payload.dump();
    }
    return Json{{"kind", "command"},
                {"commandId", commandId},
                {"arguments", args},
                {"payment", payment},
                {"responsePrefix", mcp::responsePrefix(id)}}
        .dump();
  } catch (const std::exception &) {
    return mcp::failure(id, -32602, "Invalid command arguments").dump();
  } catch (...) {
    return mcp::failure(id, -32603, "Conversion command unavailable").dump();
  }
}
} // namespace arken::file_conversion::service::v1
