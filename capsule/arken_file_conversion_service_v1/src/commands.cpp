#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken_schema_generated/command-json.hpp"
#include "payment_json.hpp"
namespace arken::file_conversion::service::v1 {
namespace image = arken::image::v1;
namespace J = arken::schema::cpp::v1::tool_json;
namespace S = arken::schema::generated::conversion::v1;
using Json=J::Json;
namespace {
std::string Failure(const Json& id,int code,std::string_view text) {
    return Json{{"kind","response"},{"status",200},{"body",Json{{"jsonrpc","2.0"},{"id",id},
        {"error",{{"code",code},{"message",text}}}}.dump()}}.dump();
}
}
std::string Catalog() { return std::string(J::commandCatalog); }
std::string PlanRequest(std::string_view text,ServerMode mode) noexcept {
    Json id=nullptr;
    try {
        const auto surface=Surface(mode);const bool payments=mode!=ServerMode::preview;
        const auto request=payment::detail::Parse(text);
        if(!request.is_object()||!request.contains("jsonrpc")||!request.at("jsonrpc").is_string()
            ||request.at("jsonrpc")!="2.0")return Failure(id,-32600,"Invalid request");
        if(request.contains("id")) {
            id=request.at("id");
            if(!id.is_string()&&!id.is_number_integer())return Failure(nullptr,-32600,"Invalid request ID");
            if(id.is_string()&&id.get_ref<const std::string&>().size()>128)return Failure(nullptr,-32600,"Request ID too long");
        }
        if(!request.contains("method")||!request.at("method").is_string())return Failure(id,-32600,"Invalid method");
        const auto method=request.at("method").get<std::string>();
        if(!request.contains("id")) {
            if(method=="notifications/initialized"||method=="notifications/cancelled")
                return Json{{"kind","response"},{"status",202},{"body",""}}.dump();
            return Json{{"kind","response"},{"status",400},{"body",""}}.dump();
        }
        const auto catalog=Json::parse(J::commandCatalog);
        const auto params=request.value("params",Json::object());
        if(!params.is_object())return Failure(id,-32602,"Parameters must be an object");
        const auto reply=[&](Json result,bool initialize=false){return Json{{"kind","response"},{"status",200},{"initialize",initialize},
            {"body",Json{{"jsonrpc","2.0"},{"id",id},{"result",std::move(result)}}.dump()}}.dump();};
        if(method=="initialize") {
          if(!params.contains("protocolVersion")||!params["protocolVersion"].is_string()
              ||!params.contains("capabilities")||!params["capabilities"].is_object()
              ||!params.contains("clientInfo")||!params["clientInfo"].is_object()
              ||!params["clientInfo"].contains("name")||!params["clientInfo"]["name"].is_string()
              ||!params["clientInfo"].contains("version")||!params["clientInfo"]["version"].is_string())
            return Failure(id,-32602,"Invalid initialization parameters");
          return reply({{"protocolVersion","2025-11-25"},{"capabilities",{{"tools",Json::object()}}},
            {"serverInfo",{{"name","Arken Convert"},{"version","r4354"}}},
            {"instructions",surface.instructions}},true);
        }
        if(method=="ping")return reply(Json::object());
        if(method=="tools/list") {
            auto tools=Json::array();
            for(const auto& command:catalog.at("commands"))
                tools.push_back({{"name",command.at("name")},{"description",command.at("description")},
                    {"inputSchema",command.at("inputSchema")},{"outputSchema",command.at("outputSchema")},
                    {"annotations",{{"readOnlyHint",false},{"destructiveHint",false},
                        {"idempotentHint",command.at("idempotency")=="idempotent"},{"openWorldHint",payments&&command.at("commandId")==2}}}});
            return reply({{"tools",tools}});
        }
        if(method!="tools/call")return Failure(id,-32601,"Method not found");
        const auto name=params.at("name").get<std::string>();
        const auto input=params.value("arguments",Json::object());
        std::uint32_t commandId=0;
        for(const auto& command:catalog.at("commands"))
            if(command.at("name")==name)commandId=command.at("commandId").get<std::uint32_t>();
        Json args;
        if(commandId==1) {
            const auto value=J::read<S::upload_request>(input);
            const auto output=static_cast<image::Format>(value.output_format);
            const auto quality=value.jpeg_quality?value.jpeg_quality->value:95u;
            const auto alpha=value.alpha_policy?(static_cast<std::uint32_t>(*value.alpha_policy)-1u):0u;
            J::require(output==image::Format::jpeg || (!value.jpeg_quality&&alpha==0),"JPEG options require JPEG output");
            args=J::write(value);
            args["codec"]={{"input",static_cast<std::uint32_t>(value.input_format)},
                {"output",static_cast<std::uint32_t>(value.output_format)},{"quality",quality},{"alpha",alpha}};
        } else if(commandId==2) {
            const auto value=J::read<S::file_target>(input);
            args=J::write(value);
        } else return Failure(id,-32602,"Unknown tool");
        Json payment=nullptr;
        if(params.contains("_meta")){
            J::require(params.at("_meta").is_object(),"MCP metadata must be an object");
            if(params.at("_meta").contains("x402/payment")){
                J::require(payments&&commandId==2,"payment not accepted by this tool");
                const auto& payload=params.at("_meta").at("x402/payment");
                J::require(payload.is_object(),"payment must be an object");payment=payload.dump();
            }
        }
        return Json{{"kind","command"},{"commandId",commandId},{"arguments",args},{"payment",payment},
            {"responsePrefix","{\"jsonrpc\":\"2.0\",\"id\":"+id.dump()+",\"result\":"}}.dump();
    } catch(const nlohmann::json::parse_error&) { return Failure(nullptr,-32700,"Invalid JSON"); }
    catch(const std::exception&) { return Failure(id,-32602,"Invalid command arguments"); }
    catch(...) { return Failure(id,-32603,"Conversion command unavailable"); }
}
}
