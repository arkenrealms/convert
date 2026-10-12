#include "arken_file_conversion_service_v1/payment_journal.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "payment_json.hpp"
#include "payment_journal_internal.hpp"
#include "payment_actor_roles.hpp"
#include <algorithm>
#include <stdexcept>

namespace arken::file_conversion::service::v1::payment {
namespace {
namespace J = arken::schema::cpp::v1::tool_json;
using detail::Json;
using detail::Fields;
using detail::Parse;
template<std::size_t N> std::string Hex(const std::array<std::uint8_t, N>& bytes) {
  arken::schema::cpp::v1::fixed_id<N, 1> id;
  for (std::size_t i = 0; i < N; ++i) id.value[i] = std::byte(bytes[i]);
  return J::write(id).template get<std::string>();
}
Address ReadAddress(const Json& value) {
  const auto id = J::read<arken::schema::cpp::v1::fixed_id<20, 1>>(value);
  Address bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::to_integer<std::uint8_t>(id.value[i]);
  J::require(std::any_of(bytes.begin(), bytes.end(), [](auto byte) { return byte != 0; }), "empty journal address");
  return bytes;
}
using detail::JournalOperationKey;
struct Page {
  std::optional<Scope> scope;
  std::string actorKey, after;
  std::string_view actorKind = "empty";
  std::uint32_t limit;
  explicit Page(const Json& input)
      : after(J::readScalar<std::string>(input.at("after"))),
        limit(J::readScalar<std::uint32_t>(input.at("limit"))) {
    J::require(limit > 0 && limit <= journalMaximumRows, "journal row limit");
    J::require(after.empty() || JournalOperationKey(after), "journal cursor");
    if (!input.at("scope").is_null()) {
      const auto stored = Parse(J::readScalar<std::string>(input.at("scope")), 512);
      if (stored == detail::PublicUploadActorRole()) {
        J::require(after.empty(), "nonpayment journal cursor");
        actorKind = "public-upload-budget";
        actorKey = detail::publicUploadActorKey;
        return;
      }
      Fields(stored, {"chain", "asset", "payer"});
      scope = Scope{J::readScalar<std::uint32_t>(stored.at("chain")), ReadAddress(stored.at("asset")), ReadAddress(stored.at("payer"))};
      Quote quote; quote.scope = *scope;
      J::require(PaymentProfileForQuote(quote).has_value(), "unsupported journal scope");
      actorKey = std::to_string(scope->chain) + "/" + Hex(scope->asset) + "/" + Hex(scope->payer);
      actorKind = "payment";
    } else J::require(after.empty(), "unscoped journal cursor");
  }
};
std::string_view StageName(Stage value) {
  switch (value) {
  case Stage::quoted: return "quoted";
  case Stage::verifying: return "verifying";
  case Stage::authorized: return "authorized";
  case Stage::computing: return "computing";
  case Stage::outputReady: return "outputReady";
  case Stage::settling: return "settling";
  case Stage::settlementUnknown: return "settlementUnknown";
  case Stage::settled: return "settled";
  case Stage::failedNoCharge: return "failedNoCharge";
  case Stage::settlementFailed: return "settlementFailed";
  }
  throw std::invalid_argument("invalid journal stage");
}
Json ProjectRow(const Page& page, const Json& entry) {
  Fields(entry, {"key", "value"});
  const auto envelope = Parse(J::readScalar<std::string>(entry.at("value")));
  Fields(envelope, {"version", "operation"});
  J::require(J::readScalar<std::uint32_t>(envelope.at("version")) == 1, "journal record version");
  const auto operation = ReadOperationRecord(envelope.at("operation").dump());
  J::require(operation.has_value(), "invalid journal operation");
  const auto& quote = operation->quote;
  J::require(page.scope && quote.scope == *page.scope &&
      quote.recipient == ImagePaymentRecipient() &&
      entry.at("key") == "operation/" + Hex(quote.operation), "journal record identity");
  const auto profile = PaymentProfileForQuote(quote);
  J::require(profile.has_value(), "journal payment profile");
  // Whitelist only monitor evidence. Never emit authorizations, nonces, tenant,
  // input/options commitments, converted output or original request content.
  return {{"operation", Hex(quote.operation)}, {"release", Hex(quote.release)},
    {"revision", J::writeScalar(operation->revision)}, {"stage", StageName(operation->stage)},
    {"tool", nullptr}, {"updatedAt", nullptr}, {"network", profile->network},
    {"asset", profile->assetText}, {"recipient", "0x" + Hex(quote.recipient)},
    {"amountAtomic", J::writeScalar(quote.amount)}, {"payer", "0x" + Hex(quote.scope.payer)},
    {"transaction", operation->transfer ? Json("0x" + Hex(operation->transfer->transaction)) : Json(nullptr)},
    {"classification", "unknown"}};
}
} // namespace

std::string JournalReadPlanPacket(std::string_view text) noexcept {
  try {
    const auto input = Parse(text, 4096);
    Fields(input, {"scope", "after", "limit"});
    const Page page(input);
    return Json{{"schema", "arken.convert.payment-journal-read.v1"}, {"scopeKey", "scope"},
      {"actorKind", page.actorKind}, {"actorKey", page.actorKey.empty() ? Json(nullptr) : Json(page.actorKey)}, {"prefix", "operation/"},
      {"after", page.after}, {"limit", page.limit + 1}}.dump();
  } catch (const std::exception&) { return "{\"packetError\":\"invalid_payment_journal_plan\"}"; }
  catch (...) { return "{\"packetError\":\"payment_journal_unavailable\"}"; }
}
std::string JournalProjectPacket(std::string_view text) noexcept {
  try {
    const auto input = Parse(text, journalMaximumPacketBytes);
    Fields(input, {"scope", "after", "limit", "now", "entries"});
    const Page page(input);
    const auto now = J::readScalar<std::uint64_t>(input.at("now"));
    J::require(now > 0 && now <= 4102444800ULL, "journal observation time");
    const auto& entries = input.at("entries");
    J::require(entries.is_array() && entries.size() <= page.limit + 1, "journal entries bound");
    J::require(page.scope || entries.empty(), "unscoped journal records");
    Json rows = Json::array();
    std::string previous = page.after, last;
    for (const auto& entry : entries) {
      Fields(entry, {"key", "value"});
      const auto key = J::readScalar<std::string>(entry.at("key"));
      J::require(JournalOperationKey(key) && key > previous, "journal key order");
      previous = key;
      auto row = ProjectRow(page, entry); // Validate lookahead before reporting success.
      if (rows.size() < page.limit) { rows.push_back(std::move(row)); last = key; }
    }
    const bool reachedEnd = entries.size() <= page.limit;
    return Json{{"schema", "arken.convert.payment-journal-page.v1"}, {"service", "convert"},
      {"actorKind", page.actorKind}, {"actorKey", page.actorKey.empty() ? Json(nullptr) : Json(page.actorKey)},
      {"observedAt", J::writeScalar(now)}, {"after", page.after}, {"limit", page.limit},
      {"nextCursor", reachedEnd ? Json(nullptr) : Json(last)}, {"reachedEnd", reachedEnd},
      {"snapshot", "actor-live-page"}, {"evidence", "retained-payment-records"},
      {"metadata", {{"tool", "not-retained"}, {"updatedAt", "not-retained"}}}, {"rows", rows}}.dump();
  } catch (const std::exception&) { return "{\"packetError\":\"invalid_payment_journal_page\"}"; }
  catch (...) { return "{\"packetError\":\"payment_journal_unavailable\"}"; }
}
} // namespace arken::file_conversion::service::v1::payment
