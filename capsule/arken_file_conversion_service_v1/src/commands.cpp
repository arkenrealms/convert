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
struct PairTool {
  std::uint32_t commandId;
  S::input_kind input;
  S::output_kind output;
  const char* description;
};
constexpr PairTool pairTools[]{
  {3, S::input_kind::png, S::output_kind::jpeg,
   "Convert a PNG image to JPEG (JPG). First prepare_upload with input_format=png and output_format=jpeg, then PUT the bytes to upload_url. Call with the returned file_id and private file_token. Set jpeg_quality and explicit alpha_policy=flatten_white at preparation if transparency must be flattened onto white; JPEG cannot preserve transparency. Returns the original downloadable result on repeat, without another charge."},
  {4, S::input_kind::jpeg, S::output_kind::png,
   "Convert a JPEG (JPG) photo to PNG. First prepare_upload with input_format=jpeg and output_format=png, then PUT the bytes to upload_url. Call with the returned file_id and private file_token. PNG does not restore detail already lost to JPEG compression. Returns the original downloadable result on repeat, without another charge."},
  {5, S::input_kind::bmp, S::output_kind::png,
   "Convert a BMP bitmap image to PNG. First prepare_upload with input_format=bmp and output_format=png, then PUT the bytes to upload_url. Call with the returned file_id and private file_token. Returns the original downloadable result on repeat, without another charge."},
  {6, S::input_kind::bmp, S::output_kind::jpeg,
   "Convert a BMP bitmap image to JPEG (JPG). First prepare_upload with input_format=bmp and output_format=jpeg, then PUT the bytes to upload_url. Set jpeg_quality and explicit alpha_policy=flatten_white at preparation if transparency must be flattened onto white. Call with the returned file_id and private file_token. Returns the original downloadable result on repeat, without another charge."},
  {7, S::input_kind::tga, S::output_kind::png,
   "Convert a TGA (Targa) image to PNG. First prepare_upload with input_format=tga and output_format=png, then PUT the bytes to upload_url. Call with the returned file_id and private file_token. Returns the original downloadable result on repeat, without another charge."},
  {8, S::input_kind::dds, S::output_kind::png,
   "Convert a supported DDS texture image to PNG. First prepare_upload with input_format=dds and output_format=png, then PUT the bytes to upload_url. Call with the returned file_id and private file_token. Unsupported DDS profiles are rejected by the codec. Returns the original downloadable result on repeat, without another charge."}
};
const PairTool* pairTool(std::uint32_t id) {
  for(const auto& pair:pairTools) if(pair.commandId==id) return &pair;
  return nullptr;
}
bool conversionTool(std::uint32_t id) { return id==2 || pairTool(id)!=nullptr; }
std::vector<mcp::ToolPresentation> presentations(ServerMode mode) {
  std::vector<mcp::ToolPresentation> result{
    {1,false,false,false,"Prepare an immutable upload with explicit input_format and output_format (PNG, JPEG/JPG, BMP, TGA; supported DDS input). PUT the exact source bytes to upload_url, then call convert_image or the matching named conversion tool with file_id and private file_token. Set JPEG quality and alpha policy here; limits are 8 MiB, 1,048,576 pixels and 4096 pixels per dimension."},
    {2,false,false,mode!=ServerMode::preview,"Convert an uploaded image using the immutable formats and options chosen in prepare_upload. Returns a private download URL and output metadata. Named conversions such as png_to_jpeg use the same file, result and payment identity; repeating either entry point does not charge again."}};
  for(const auto& pair:pairTools) result.push_back({pair.commandId,false,false,mode!=ServerMode::preview,pair.description});
  return result;
}
mcp::Server makeServer(ServerMode mode) {
  mcp::ServerDescription description{"Arken Convert",
                      "r4574",
                      std::string(Surface(mode).instructions),
                      S::commandCatalog,
                      presentations(mode)};
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
std::string CallSummary(std::string_view text, int httpStatus) noexcept {
  try {
    return server(ServerMode::production)
        .callSummary(mcp::parseRequestJson(text), "convert", httpStatus).dump();
  } catch (...) {
    // Never expose parser diagnostics or fail a completed product response.
    return "null";
  }
}
std::string CostContextPacket(std::string_view text) noexcept {
  try {
    const auto packet=mcp::parseBoundedJson(text,65536);
    J::require(packet.is_object()&&packet.size()==4,"cost context fields");
    const auto request=J::readScalar<std::string>(packet.at("request"));
    const auto plan=Json::parse(PlanRequest(request,ServerMode::production));
    if(plan.at("kind")!="command")return "null";
    auto arguments=plan.at("arguments");arguments.erase("codec");
    const auto exchange=J::readScalar<std::string>(packet.at("exchangeKind"));
    J::require(exchange=="quote"||exchange=="operation","cost exchange kind");
    return server(ServerMode::production).costContext(plan.at("commandId").get<std::uint32_t>(),"convert",
        J::readScalar<std::string>(packet.at("release")),arguments.dump().size(),
        exchange=="quote"?mcp::CostExchange::quote:mcp::CostExchange::operation,
        J::readScalar<int>(packet.at("httpStatus"))).dump();
  } catch(...) {return "null";}
}
std::string Catalog() {
  auto catalog=Json::parse(S::commandCatalog);
  const auto descriptions=presentations(ServerMode::production);
  for(auto& command:catalog.at("commands"))
    for(const auto& p:descriptions)
      if(p.commandId>=3&&command.at("commandId")==p.commandId) command["description"]=p.description;
  return catalog.dump();
}
std::string ConversionTargetPacket(std::string_view text) noexcept {
  try {
    const auto packet=mcp::parseRequestJson(text);
    J::require(packet.is_object()&&packet.size()==2&&packet.contains("commandId")&&packet.contains("arguments"),"invalid target packet");
    const auto id=J::readScalar<std::uint32_t>(packet.at("commandId"));
    J::require(conversionTool(id),"not a conversion tool");
    auto arguments=packet.at("arguments");
    arguments.erase("codec"); // Derived native options, not a second format authority.
    const auto upload=J::read<S::upload_request>(arguments);
    if(const auto* pair=pairTool(id);pair &&
       (upload.input_format!=pair->input||upload.output_format!=pair->output))
      return Json{{"allowed",false},{"code","conversion_format_mismatch"}}.dump();
    return Json{{"allowed",true}}.dump();
  } catch(const std::exception&) {
    return "{\"allowed\":false,\"code\":\"invalid_conversion_target\"}";
  } catch(...) {
    return "{\"allowed\":false,\"code\":\"conversion_target_unavailable\"}";
  }
}
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
    } else if (conversionTool(commandId))
      args = J::write(J::read<S::file_target>(input));
    else
      return mcp::failure(id, -32602, "Unknown tool").dump();
    Json payment = nullptr;
    if (plan.metadata.contains("x402/payment")) {
      J::require(payments && conversionTool(commandId),
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
