#include "arken_file_conversion_service_v1/payment_journal.hpp"
#include "arken_paid_operation_v1/report_access.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "payment_json.hpp"
#include "payment_journal_internal.hpp"

namespace arken::file_conversion::service::v1::payment {
namespace {
namespace J = arken::schema::cpp::v1::tool_json;
namespace P = arken::paid_operation::v1;
using detail::Json;
using detail::Parse;
using detail::Fields;
std::string Hash(std::string_view text) {
  const auto digest = arken::digest::v1::sha256(std::as_bytes(std::span(text.data(), text.size())));
  J::require(bool(digest), "journal digest unavailable");
  std::array<char, 71> output{};
  J::require(arken::digest::v1::format_sha256(*digest, output), "journal digest format");
  return std::string(output.data() + 7, 64);
}
Json Context(const std::string& configurationText) {
  const auto configuration = Parse(configurationText, 1024);
  Fields(configuration, {"schema", "origin", "namespaceId"});
  const auto origin = configuration.at("origin").get<std::string>();
  const auto namespaceId = configuration.at("namespaceId").get<std::string>();
  J::require(configuration.at("schema") == "arken.convert.journal-configuration.v1" &&
      origin == "https://convert.mcp.arken.gg" && namespaceId.size() == 32 &&
      namespaceId.find_first_not_of("0123456789abcdef") == namespaceId.npos, "journal configuration");
  return {{"origin", origin}, {"namespaceId", namespaceId}, {"configurationSha256", Hash(configurationText)}};
}
Json Admission(const Json& packet) {
  Fields(packet, {"reportConfiguration", "serviceConfiguration", "now", "request"});
  const auto& request = packet.at("request");
  Fields(request, {"method", "path", "origin", "hasOrigin", "authorization", "contentType"});
  const auto path = request.at("path").get<std::string>();
  if (!path.starts_with("/_arken/convert-reports/")) return {{"kind", "public"}};
  const auto context = Context(packet.at("serviceConfiguration").get<std::string>());
  const auto grant = Parse(packet.at("reportConfiguration").get<std::string>(), 2048);
  Fields(grant, {"schema", "grantId", "credentialDigest", "configurationSha256", "notBefore", "expires"});
  const auto authorization = request.at("authorization").get<std::string>();
  const auto configurationDigest = context.at("configurationSha256").get<std::string>();
  const auto now = J::readScalar<std::uint64_t>(packet.at("now"));
  J::require(P::AdmitReportRead(
      {grant.at("schema"), grant.at("grantId"), grant.at("credentialDigest"), grant.at("configurationSha256"),
       J::readScalar<std::uint64_t>(grant.at("notBefore")), J::readScalar<std::uint64_t>(grant.at("expires"))},
      {request.at("method"), path, request.at("origin"), request.at("contentType"), authorization, request.at("hasOrigin").get<bool>()},
      "arken.convert.journal-read-grant.v1", "/_arken/convert-reports/journal", context.at("origin").get<std::string>(), configurationDigest,
      authorization.size() == 71 ? Hash(std::string_view(authorization).substr(7)) : "", now), "journal access denied");
  return {{"kind", "report"}, {"grantId", grant.at("grantId")}, {"configurationSha256", configurationDigest},
    {"namespaceId", context.at("namespaceId")}, {"bodyLimit", 1024}, {"deadlineMs", 5000}};
}
constexpr std::string_view denied = R"({"code":"journal_access_denied","kind":"response","status":403})";
}
std::string JournalReportContextPacket(std::string_view text) noexcept {
  try {
    const auto input = Parse(text, 2048); Fields(input, {"serviceConfiguration"});
    return Context(input.at("serviceConfiguration").get<std::string>()).dump();
  } catch (...) { return R"({"packetError":"invalid_journal_configuration"})"; }
}
std::string JournalReportAdmissionPacket(std::string_view text) noexcept {
  try { return Admission(Parse(text)).dump(); }
  catch (...) { return std::string(denied); }
}
std::string JournalReportRequestPacket(std::string_view text) noexcept {
  Json packet, authorized;
  std::string body;
  try {
    packet = Parse(text); body = packet.at("body").get<std::string>(); packet.erase("body");
    authorized = Admission(packet);
    J::require(authorized.at("kind") == "report", "journal route required");
  } catch (...) { return std::string(denied); }
  try {
    const auto page = Parse(body, 1024);
    Fields(page, {"actorId", "after", "limit"});
    const auto actorId = page.at("actorId").get<std::string>(), after = page.at("after").get<std::string>();
    const auto limit = J::readScalar<std::uint32_t>(page.at("limit"));
    J::require(P::ReportIdentifier(actorId) && (after.empty() || detail::JournalOperationKey(after)) &&
        limit > 0 && limit <= journalMaximumRows, "journal page request");
    authorized["kind"] = "read"; authorized["actorId"] = actorId;
    authorized["after"] = after; authorized["limit"] = limit;
    return authorized.dump();
  } catch (...) { return R"({"code":"journal_request_invalid","kind":"response","status":400})"; }
}
}
