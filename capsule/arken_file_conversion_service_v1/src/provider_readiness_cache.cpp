#include "arken/digest/v1/digest.hpp"
#include "arken_file_conversion_service_v1/provider_readiness.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
namespace arken::file_conversion::service::v1 {
namespace {
namespace J = arken::schema::cpp::v1::tool_json;
using Json = J::Json;
Json cached(const Json &p) {
  J::require(p.is_object() && p.size() == 6, "provider cache packet");
  const auto &c = p.at("configuration");
  J::require(c.is_object() && c.size() == 4, "provider cache context");
  const auto &context = c.at("context").get_ref<const std::string &>();
  J::require(!context.empty() && context.size() <= 8192,
             "provider context bound");
  const auto now = J::readScalar<std::uint64_t>(p.at("now"));
  J::require(now > 0 && now <= 4102444800, "provider cache clock");
  const auto bytes = c.dump();
  const auto hash = arken::digest::v1::sha256(
      std::as_bytes(std::span(bytes.data(), bytes.size())));
  J::require(bool(hash), "provider context digest");
  std::array<char, 71> out{};
  J::require(arken::digest::v1::format_sha256(*hash, out),
             "provider context digest format");
  const std::string identity(out.data() + 7, 64);
  const auto ready = [&](const Json &record, bool reused) {
    return Json{{"kind", "ready"},  {"ready", true},
                {"code", "ready"},  {"fingerprint", identity},
                {"record", record}, {"reused", reused}};
  };
  if (!p.at("cached").is_null()) {
    const auto &r = p.at("cached");
    J::require(r.is_object() && r.size() == 3, "provider cache fields");
    const auto at = J::readScalar<std::uint64_t>(r.at("checkedAt")),
               until = J::readScalar<std::uint64_t>(r.at("expiresAt"));
    J::require(at <= 4102444800 && until == at + 300,
               "provider cache lifetime");
    if (r.at("fingerprint") == identity && at <= now && now < until)
      return ready(r, true);
  }
  const Json credentials{{"keyId", c.at("keyId")},
                         {"keySecret", c.at("keySecret")},
                         {"rpcClientKey", c.at("rpcClientKey")}};
  auto result = Json::parse(ProviderReadinessPacket(Json{
      {"configuration", credentials},
      {"anonymous", p.at("anonymous")},
      {"supported", p.at("supported")},
      {"rpc", p.at("rpc")},
      {"now", p.at("now")}}.dump()));
  if (result.at("kind") == "ready")
    return ready({{"fingerprint", identity},
                  {"checkedAt", std::to_string(now)},
                  {"expiresAt", std::to_string(now + 300)}},
                 false);
  result["fingerprint"] = identity;
  return result;
}
} // namespace
std::string CachedProviderReadinessPacket(std::string_view input) noexcept {
  try {
    return cached(J::parse(input, 1048576)).dump();
  } catch (...) {
    return R"({"kind":"unavailable","ready":false,"code":"production_configuration_or_probe_invalid"})";
  }
}
} // namespace arken::file_conversion::service::v1
