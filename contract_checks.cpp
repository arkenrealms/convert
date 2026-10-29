#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_service_v1/paid_conversion.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace service = arken::file_conversion::service::v1;
using Json = nlohmann::json;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--version")
        { std::cout << "arken-convert-mcp-contract-tests-r6242\n"; return 0; }
    try
    {
        Check(argc == 1, "unexpected arguments");
        const auto catalog = Json::parse(service::Catalog());
        const auto response = Json::parse(service::PlanRequest(
            R"({"jsonrpc":"2.0","id":9007199254740993,"method":"tools/list"})", service::ServerMode::production));
        Check(response.at("kind") == "response", "discovery did not return a response plan");
        const auto list = Json::parse(response.at("body").get<std::string>());
        Check(list.at("id") == 9007199254740993ULL, "exact request id changed");
        const auto& tools = list.at("result").at("tools");
        Check(tools.size() == 8 && tools.size() == catalog.at("commands").size(), "catalog coverage changed");
        const auto call = Json{{"jsonrpc","2.0"},{"id",7},{"method","tools/call"},
            {"params",{{"name","png_to_jpeg"},{"arguments",{{"file_id",std::string(64,'1')},{"file_token",std::string(64,'2')}}}}}}.dump();
        const auto planned = Json::parse(service::PlanRequest(call, service::ServerMode::production));
        Check(planned.at("kind") == "command" && planned.at("commandId") == 3, "named conversion route changed");
        const auto cost = Json::parse(service::CostContextPacket(Json{{"request",call},
            {"release",std::string(64,'a')},{"exchangeKind","operation"},{"httpStatus",503}}.dump()));
        Check(cost.at("command") == "conversion.v1.png_to_jpeg" && cost.at("httpStatus") == 503,
            "failed-call cost attribution changed");
        Check(cost.dump().find(std::string(64,'2')) == std::string::npos, "private capability leaked to cost metadata");
        const auto observation = Json::parse(service::ConversionObservation(7,9,5,6,0));
        Check(observation.at("reason") == "unsupported_compression", "failure classification changed");
        std::cout << "PASS 8-tool MCP discovery, exact IDs, named dispatch, failed-call cost and privacy contracts\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
