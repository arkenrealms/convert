#include "arken_file_conversion_service_v1/payment_journal.hpp"
#include "arken/digest/v1/digest.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <span>
#include <stdexcept>

namespace {
namespace P = arken::file_conversion::service::v1::payment;
using Json = nlohmann::json;
Json vectors = Json::array(), seed;
unsigned checks;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
std::string Hash(std::string_view text) {
  const auto hash = arken::digest::v1::sha256(std::as_bytes(std::span(text.data(), text.size())));
  std::array<char, 71> result{};
  Check(hash && arken::digest::v1::format_sha256(*hash, result), "fixture SHA-256");
  return std::string(result.data() + 7, 64);
}
Json Run(unsigned mode, const std::string& input) {
  const auto output = mode == 11 ? P::JournalReportAdmissionPacket(input) :
    mode == 12 ? P::JournalReportRequestPacket(input) : P::JournalReportContextPacket(input);
  vectors.push_back({{"mode", mode}, {"input", input}, {"output", output}});
  return Json::parse(output);
}
}
unsigned PaymentJournalAccessTests() {
  checks = 0; vectors = Json::array();
  const auto configuration = Json{{"schema", "arken.convert.journal-configuration.v1"},
    {"origin", "https://convert.mcp.arken.gg"}, {"namespaceId", std::string(32, '1')}}.dump();
  const std::string token(64, '2');
  const Json grant{{"schema", "arken.convert.journal-read-grant.v1"}, {"grantId", std::string(64, '3')},
    {"configurationSha256", Hash(configuration)}, {"credentialDigest", Hash(token)}, {"notBefore", "100"}, {"expires", "200"}};
  const Json request{{"method", "POST"}, {"path", "/_arken/convert-reports/journal"}, {"origin", "https://convert.mcp.arken.gg"},
    {"hasOrigin", false}, {"authorization", "Bearer " + token}, {"contentType", "application/json"}};
  const Json input{{"serviceConfiguration", configuration}, {"reportConfiguration", grant.dump()}, {"now", "150"}, {"request", request}};
  const Json body{{"actorId", std::string(64, '4')}, {"after", ""}, {"limit", 16}};
  seed = {{"admission", input}, {"body", body}};
  Check(Run(11, input.dump()).at("kind") == "report", "valid report grant rejected");
  auto page = input; page["body"] = body.dump();
  Check(Run(12, page.dump()).at("actorId") == body.at("actorId"), "actor identity lost");
  Check(Run(13, Json{{"serviceConfiguration", configuration}}.dump()).at("configurationSha256") == Hash(configuration), "context identity lost");
  auto publicInput = input; publicInput["request"]["path"] = "/v1"; publicInput["reportConfiguration"] = "";
  Check(Run(11, publicInput.dump()) == Json{{"kind", "public"}}, "public request needs private grant");
  for (unsigned i = 0; i < 21; ++i) {
    auto bad = input, changedGrant = grant;
    if (i == 0) bad["now"] = "200";
    if (i == 1) bad["now"] = "99";
    if (i == 2) changedGrant["schema"] = "arken.physics.journal-read-grant.v1";
    if (i == 3) changedGrant["grantId"] = "invalid";
    if (i == 4) changedGrant["configurationSha256"] = std::string(64, 'f');
    if (i == 5) changedGrant["credentialDigest"] = std::string(64, 'f');
    if (i == 6) changedGrant["expires"] = "3000000";
    if (i == 7) changedGrant["notBefore"] = "0";
    if (i == 8) bad["request"]["method"] = "GET";
    if (i == 9) bad["request"]["path"] = "/_arken/convert-reports/journal?token=secret";
    if (i == 10) bad["request"]["origin"] = "https://attacker.example";
    if (i == 11) bad["request"]["hasOrigin"] = true;
    if (i == 12) bad["request"]["contentType"] = "text/plain";
    if (i == 13) bad["request"]["authorization"] = "Bearer " + std::string(64, 'f');
    if (i == 14) bad["request"]["authorization"] = "bearer " + token;
    if (i == 15) bad["request"]["hasOrigin"] = 0;
    if (i == 16) bad["now"] = 150;
    if (i == 17) bad["request"]["unknown"] = true;
    if (i == 18) changedGrant["expires"] = "4102444801";
    if (i == 19) bad["serviceConfiguration"] = configuration + " ";
    if (i == 20) changedGrant["credentialDigest"] = std::string(64, 'F');
    bad["reportConfiguration"] = changedGrant.dump();
    Check(Run(11, bad.dump()) == Json{{"kind", "response"}, {"status", 403}, {"code", "journal_access_denied"}}, "unsafe grant admitted");
  }
  for (unsigned i = 0; i < 8; ++i) {
    auto badBody = body, bad = input;
    if (i == 0) badBody["limit"] = 0;
    if (i == 1) badBody["limit"] = 17;
    if (i == 2) badBody["actorId"] = std::string(64, 'F');
    if (i == 3) badBody["actorId"] = "";
    if (i == 4) badBody["after"] = "authorization/secret";
    if (i == 5) badBody["unknown"] = true;
    bad["body"] = i == 6 ? std::string(1025, ' ') : i == 7 ? "{\"limit\":1," + badBody.dump().substr(1) : badBody.dump();
    Check(Run(12, bad.dump()).at("status") == 400, "unsafe page admitted");
  }
  Check(Run(11, "{\"now\":\"150\"," + input.dump().substr(1)).at("status") == 403, "duplicate grant field admitted");
  Check(Run(13, "{}").contains("packetError"), "missing actor context admitted");
  return checks;
}
std::string PaymentJournalAccessFixtures() { return Json{{"seed", seed}, {"vectors", vectors}}.dump(); }
