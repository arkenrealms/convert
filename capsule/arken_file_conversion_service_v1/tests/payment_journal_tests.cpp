#include "arken_file_conversion_service_v1/payment_journal.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace P = arken::file_conversion::service::v1::payment;
using Json = nlohmann::json;
unsigned checks;
Json fixtures = Json::array();
void Check(bool condition, const char* reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}
struct Actor {
  P::Operation operation;
  std::optional<P::NonceClaim> claim;
  std::map<std::string, std::string> storage;
  std::vector<Json> entries;
  Actor() {
    auto& q = operation.quote;
    q.operation[0] = 1; q.tenant[0] = 2; q.input[0] = 3; q.options[0] = 4;
    q.release[0] = 5; q.priceVersion[0] = 6;
    const auto profile = *P::PaymentProfileById("base-mainnet-v1");
    q.scope.chain = profile.chain; q.scope.asset = profile.asset; q.scope.payer[0] = 7;
    q.recipient = P::ImagePaymentRecipient(); q.amount = 10000; q.expiresAt = 200;
  }
  void Step(P::EventKind kind) {
    P::Event event; event.kind = kind; event.expectedRevision = operation.revision; event.now = 100;
    if (kind == P::EventKind::request) {
      P::Authorization auth; auth.payload[0] = 8; auth.nonce[0] = 9;
      auth.validAfter = 90; auth.validBefore = 150; event.authorization = auth;
    }
    if (kind == P::EventKind::verificationPassed) event.verifiedPayer = operation.quote.scope.payer;
    if (kind == P::EventKind::beginExecution) event.executingRelease = operation.quote.release;
    if (kind == P::EventKind::outputCommitted) {
      P::Output out; out.digest[0] = 10; out.release = operation.quote.release;
      out.bytes = 100; out.width = 10; out.height = 10; event.output = out;
    }
    if (kind == P::EventKind::settlementSucceeded || kind == P::EventKind::reconciledSucceeded) {
      P::Transfer transfer; transfer.transaction[0] = 11; transfer.nonce = operation.authorization->nonce;
      transfer.scope = operation.quote.scope; transfer.recipient = operation.quote.recipient;
      transfer.amount = operation.quote.amount; event.transfer = transfer;
    }
    if (kind == P::EventKind::failBeforeSettlement) event.failure = P::Failure::storageFailed;
    const auto input = Json::parse(P::InputPacket(operation, operation.quote, claim, event));
    const auto command = Json{{"version", 1}, {"quote", input.at("quote")}, {"event", input.at("event")}}.dump();
    auto keys = Json::parse(P::StorageLookupPacket(Json{{"command", command}, {"record", nullptr}}.dump()));
    const auto get = [&](const Json& key) -> Json {
      if (key.is_null()) return nullptr;
      const auto at = storage.find(key.get<std::string>());
      return at == storage.end() ? Json(nullptr) : Json(at->second);
    };
    const auto record = get(keys.at("operationKey"));
    keys = Json::parse(P::StorageLookupPacket(Json{{"command", command}, {"record", record}}.dump()));
    const auto storageInput = Json{{"command", command}, {"record", record},
      {"scope", get(keys.at("scopeKey"))}, {"claim", get(keys.at("nonceKey"))}}.dump();
    const auto writes = Json::parse(P::StorageAdvancePacket(storageInput));
    fixtures.push_back({{"mode", 2}, {"input", storageInput}, {"output", writes.dump()}});
    const auto expected = P::Advance(operation, operation.quote, claim, event);
    Check(expected.error == P::Error::none && expected.changed, "journal fixture transition failed");
    Check(writes.at("decision") == Json::parse(P::DecisionPacket(expected)), "stored journal transition differs");
    for (const auto& write : writes.at("writes")) storage[write.at("key").get<std::string>()] = write.at("value");
    operation = expected.operation; claim = expected.nonceClaim;
    const auto key = keys.at("operationKey").get<std::string>();
    entries.push_back(Json{{"key", key}, {"value", storage.at(key)}});
  }
  Json Page(const Json& entry) const {
    return {{"scope", storage.at("scope")}, {"after", ""}, {"limit", 16}, {"now", "300"}, {"entries", Json::array({entry})}};
  }
};
Json Project(const Json& packet) {
  const auto input = packet.dump();
  const auto output = P::JournalProjectPacket(input);
  fixtures.push_back({{"mode", 10}, {"input", input}, {"output", output}});
  return Json::parse(output);
}
void Rejected(const Json& packet) {
  const auto result = Project(packet);
  Check(result == Json{{"packetError", "invalid_payment_journal_page"}}, "unsafe journal page accepted or error leaked input");
}
Json ChangeRecord(Json entry, const auto& change) {
  auto record = Json::parse(entry.at("value").get<std::string>());
  change(record); entry["value"] = record.dump(); return entry;
}
void Tests() {
  Actor actor;
  for (auto kind : {P::EventKind::request, P::EventKind::verificationPassed, P::EventKind::beginExecution,
       P::EventKind::outputCommitted, P::EventKind::beginSettlement, P::EventKind::settlementUncertain,
       P::EventKind::reconciledSucceeded}) actor.Step(kind);
  const std::vector<std::string> stages{"verifying", "authorized", "computing", "outputReady", "settling", "settlementUnknown", "settled"};
  for (std::size_t i = 0; i < actor.entries.size(); ++i) {
    const auto packet = actor.Page(actor.entries[i]);
    const auto frozen = actor.storage;
    const auto result = Project(packet); const auto& row = result.at("rows").at(0);
    Check(row.at("stage") == stages.at(i), "retained payment stage lost");
    Check(row.at("tool").is_null() && row.at("updatedAt").is_null() && row.at("classification") == "unknown", "invented report metadata");
    Check(row.size() == 13 && !row.contains("authorization") && !row.contains("nonce") && !row.contains("output") && !row.contains("tenant"), "journal projection leaks private state");
    Check(row.at("transaction").is_null() == (i != 6), "transaction invented or lost");
    Check(result.at("reachedEnd") && result.at("nextCursor").is_null() && result.at("snapshot") == "actor-live-page", "incorrect actor coverage");
    Check(Project(packet) == result && actor.storage == frozen, "journal read changed state or is nondeterministic");
  }
  for (auto kind : {P::EventKind::failBeforeSettlement, P::EventKind::settlementRejected}) {
    Actor failed;
    if (kind == P::EventKind::settlementRejected) for (auto step : {P::EventKind::request, P::EventKind::verificationPassed,
        P::EventKind::beginExecution, P::EventKind::outputCommitted, P::EventKind::beginSettlement}) failed.Step(step);
    failed.Step(kind);
    const auto row = Project(failed.Page(failed.entries.back())).at("rows").at(0);
    Check(row.at("stage") == (kind == P::EventKind::failBeforeSettlement ? "failedNoCharge" : "settlementFailed"), "failed record misclassified");
  }
  auto base = actor.Page(actor.entries.back());
  const auto operationKey = base.at("entries").at(0).at("key").get<std::string>();
  auto read = Json{{"scope", base.at("scope")}, {"after", ""}, {"limit", 1}};
  const auto plan = Json::parse(P::JournalReadPlanPacket(read.dump()));
  fixtures.push_back({{"mode", 9}, {"input", read.dump()}, {"output", plan.dump()}});
  Check(plan.at("limit") == 2 && plan.at("prefix") == "operation/" && plan.at("scopeKey") == "scope", "unbounded or wrong storage read plan");
  Check(Project(base).at("actorKey") == plan.at("actorKey"), "actor identity changed between phases");
  auto quote = Actor{}.operation;
  auto quoted = base;
  quoted["entries"][0]["value"] = Json{{"version", 1}, {"operation", Json::parse(P::WriteOperationRecord(quote))}}.dump();
  Check(Project(quoted).at("rows").at(0).at("stage") == "quoted", "quoted stage lost");
  auto large = base;
  large["entries"][0] = ChangeRecord(large["entries"][0], [](Json& value) {
    value["operation"]["quote"]["amount"] = "9007199254740993";
    value["operation"]["transfer"]["amount"] = "9007199254740993";
    value["operation"]["revision"] = "9007199254740995";
  });
  const auto largeRow = Project(large).at("rows").at(0);
  Check(largeRow.at("amountAtomic") == "9007199254740993" && largeRow.at("revision") == "9007199254740995", "journal integer precision lost");
  auto second = ChangeRecord(base["entries"][0], [](Json& value) { value["operation"]["quote"]["operation"] = std::string(64, 'f'); });
  second["key"] = "operation/" + std::string(64, 'f');
  auto paged = base; paged["limit"] = 1; paged["entries"].push_back(second);
  const auto pageOne = Project(paged);
  Check(!pageOne.at("reachedEnd") && pageOne.at("nextCursor") == operationKey && pageOne.at("rows").size() == 1, "lookahead cursor skipped row");
  paged["after"] = operationKey; paged["entries"] = Json::array({second});
  Check(Project(paged).at("reachedEnd") && Project(paged).at("rows").at(0).at("operation") == std::string(64, 'f'), "exclusive next page failed");
  paged["entries"] = Json::array(); Check(Project(paged).at("reachedEnd"), "empty scoped tail rejected");
  auto empty = base; empty["scope"] = nullptr; empty["entries"] = Json::array();
  Check(Project(empty).at("actorKey").is_null() && Project(empty).at("rows").empty(), "empty actor misrepresented");
  empty["after"] = operationKey; Rejected(empty);
  for (unsigned scenario = 0; scenario < 26; ++scenario) {
    auto bad = base;
    if (scenario == 0) bad["scope"] = nullptr;
    if (scenario == 1) bad["after"] = operationKey;
    if (scenario == 2) bad["entries"].push_back(bad["entries"][0]);
    if (scenario == 3) bad["entries"][0]["key"] = "authorization/" + std::string(64, '1');
    if (scenario == 4) bad["entries"][0]["key"] = "operation/" + std::string(64, 'f');
    if (scenario == 5) bad["entries"][0]["value"] = std::string(16385, ' ');
    if (scenario == 6) bad["entries"][0] = ChangeRecord(bad["entries"][0], [](Json& r) { r["version"] = 2; });
    if (scenario == 7) bad["entries"][0] = ChangeRecord(bad["entries"][0], [](Json& r) { r["operation"]["authorization"] = nullptr; });
    if (scenario == 8) bad["entries"][0] = ChangeRecord(bad["entries"][0], [](Json& r) { r["operation"]["quote"]["scope"]["payer"] = std::string(40, 'f'); });
    if (scenario == 9) bad["entries"][0] = ChangeRecord(bad["entries"][0], [](Json& r) { r["operation"]["transfer"]["amount"] = "10001"; });
    if (scenario == 10) bad["entries"][0] = ChangeRecord(bad["entries"][0], [](Json& r) { r["operation"]["quote"]["recipient"] = r["operation"]["transfer"]["recipient"] = std::string(40, 'a'); });
    if (scenario == 11) { auto s = Json::parse(bad["scope"].get<std::string>()); s["payer"] = std::string(40, 'f'); bad["scope"] = s.dump(); }
    if (scenario == 12) { auto s = Json::parse(bad["scope"].get<std::string>()); s["chain"] = 1; bad["scope"] = s.dump(); }
    if (scenario == 13) bad["limit"] = 0;
    if (scenario == 14) bad["limit"] = 17;
    if (scenario == 15) bad["now"] = "0";
    if (scenario == 16) bad["now"] = "4102444801";
    if (scenario == 17) bad["now"] = 300;
    if (scenario == 18) bad["now"] = "0300";
    if (scenario == 19) bad["after"] = "operation/" + std::string(64, 'F');
    if (scenario == 20) bad["extra"] = "secret";
    if (scenario == 21) bad["entries"][0]["extra"] = "secret";
    if (scenario == 22) bad["entries"] = Json::object();
    if (scenario == 23) { bad["limit"] = 1; bad["entries"].push_back(second); bad["entries"].push_back(second); }
    if (scenario == 24) { bad["limit"] = 1; second["value"] = "{\"secret\":true}"; bad["entries"].push_back(second); }
    if (scenario == 25) { const auto raw = bad["entries"][0]["value"].get<std::string>(); bad["entries"][0]["value"] = "{\"version\":1," + raw.substr(1); }
    Rejected(bad);
  }
  for (const auto& text : {std::string("{"), "{\"scope\":null," + base.dump().substr(1), std::string(P::journalMaximumPacketBytes + 1, ' ')})
    Check(Json::parse(P::JournalProjectPacket(text)) == Json{{"packetError", "invalid_payment_journal_page"}}, "malformed journal input not bounded/redacted");
  read["limit"] = 0;
  Check(Json::parse(P::JournalReadPlanPacket(read.dump())).contains("packetError"), "invalid read plan accepted");
}
} // namespace

unsigned PaymentJournalTests() { checks = 0; fixtures = Json::array(); Tests(); return checks; }
std::string PaymentJournalFixtures() { return fixtures.dump(); }
