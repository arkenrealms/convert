#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_file_conversion_service_v1/reconciliation_configuration.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "payment_json.hpp"
#include "rpc_evidence.hpp"
#include "transfer_evidence_internal.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
namespace arken::file_conversion::service::v1::payment {
namespace {
using detail::Json;
namespace J = arken::schema::cpp::v1::tool_json;
using rpc::Data;
using rpc::Hash;
using rpc::Header;
using rpc::Quantity;
using rpc::Replies;
constexpr std::string_view used =
    "0x98de503528ee59b575ef0c0a2576a82497bfc029a5685b209e9ec333479b10a5";
template <std::size_t N>
std::string Hex(const std::array<std::uint8_t, N> &value) {
  arken::schema::cpp::v1::fixed_id<N, 1> id;
  for (std::size_t i = 0; i < N; ++i)
    id.value[i] = std::byte(value[i]);
  return "0x" + J::write(id).template get<std::string>();
}
std::string AddressTopic(const Address &a) {
  return "0x" + std::string(24, '0') + Hex(a).substr(2);
}
Json Call(unsigned id, std::string_view method, Json params) {
  return {
      {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Json Read(std::string_view slot, Json requests, std::string_view endpoint) {
  return {{"kind", "rpc"},
          {"slot", slot},
          {"request", {{"url", endpoint}, {"body", requests.dump()}}}};
}
Json Pending(std::string_view code) {
  return {{"kind", "pending"}, {"code", code}};
}
template <std::size_t N> std::array<std::uint8_t, N> Fixed(const Json &j) {
  const auto text = Data(j, N);
  const auto id =
      J::read<arken::schema::cpp::v1::fixed_id<N, 1>>(text.substr(2));
  std::array<std::uint8_t, N> out{};
  for (std::size_t i = 0; i < N; ++i)
    out[i] = std::to_integer<std::uint8_t>(id.value[i]);
  return out;
}
Json Observe(const Json &j) {
  detail::Fields(
      j, {"expected", "anchor", "logs", "receipt", "now", "rpcClientKey"});
  const auto &e = j.at("expected");
  detail::Fields(e, {"profileId", "payer", "recipient", "amount", "nonce",
                     "validAfter", "validBefore"});
  const auto profile = PaymentProfileById(e.at("profileId").get<std::string>());
  J::require(profile.has_value(), "unsupported reconciliation profile");
  const transfer_evidence::Expectation expected{
      {profile->chain, profile->asset, Fixed<20>(e.at("payer"))},
      Fixed<20>(e.at("recipient")),
      Fixed<32>(e.at("nonce")),
      J::readScalar<std::uint64_t>(e.at("amount")),
      J::readScalar<std::uint64_t>(e.at("validAfter")),
      J::readScalar<std::uint64_t>(e.at("validBefore"))};
  J::require(expected.amount > 0 &&
                 expected.validAfter < expected.validBefore &&
                 std::ranges::any_of(
                     expected.scope.payer, [](auto b) { return b != 0; }) &&
                 std::ranges::any_of(
                     expected.recipient, [](auto b) { return b != 0; }),
             "invalid authorization expectation");
  if (!j.at("rpcClientKey").is_null() && !j.at("rpcClientKey").is_string())
    return Pending(profile->reconciliationProvider ==
                           ReconciliationProvider::cdpBaseMainnet
                       ? "mainnet_rpc_client_key_invalid"
                       : "testnet_rpc_configuration_rejected");
  const auto resolved = ResolveReconciliationEndpoint(
      *profile, j.at("rpcClientKey").is_null()
                    ? std::nullopt
                    : std::optional<std::string_view>(
                          j.at("rpcClientKey").get_ref<const std::string &>()));
  if (!resolved.error.empty())
    return Pending(resolved.error);
  const auto &endpoint = resolved.url;
  const auto asset = Hex(expected.scope.asset);
  const auto now = J::readScalar<std::uint64_t>(j.at("now"));
  if (j.at("anchor").is_null())
    return Read("anchor",
                Json::array({Call(1, "eth_chainId", Json::array()),
                             Call(2, "eth_getBlockByNumber",
                                  Json::array({"safe", false}))}),
                endpoint);
  const auto anchorReplies = Replies(j.at("anchor"), {1, 2});
  J::require(Quantity(anchorReplies.at(1)) == profile->chain, "wrong chain");
  const auto anchor = Header(anchorReplies.at(2));
  J::require(anchor.time <= now, "future anchor");
  const auto first = anchor.number > 2047 ? anchor.number - 2047 : 0;
  if (j.at("logs").is_null())
    return Read("logs",
                Json::array({Call(
                    3, "eth_getLogs",
                    Json::array({Json{
                        {"address", asset},
                        {"fromBlock", Quantity(first)},
                        {"toBlock", Quantity(anchor.number)},
                        {"topics",
                         Json::array({used, AddressTopic(expected.scope.payer),
                                      Hex(expected.nonce)})}}}))}),
                endpoint);
  const auto logs = Replies(j.at("logs"), {3}).at(3);
  J::require(logs.is_array(), "logs shape");
  if (logs.empty())
    return Pending("settlement_not_observed");
  J::require(logs.size() == 1, "ambiguous authorization");
  const auto &event = logs[0];
  transfer_evidence::AuthorizationLog(event, expected);
  transfer_evidence::Identity(event, event);
  const auto number = Quantity(event.at("blockNumber"));
  J::require(number >= first && number <= anchor.number,
             "event outside search");
  if (j.at("receipt").is_null())
    return Read(
        "receipt",
        Json::array({Call(4, "eth_getTransactionReceipt",
                          Json::array({Hash(event.at("transactionHash"))})),
                     Call(5, "eth_getBlockByNumber",
                          Json::array({Quantity(number), false})),
                     Call(6, "eth_getBlockByNumber",
                          Json::array({Quantity(anchor.number), false}))}),
        endpoint);
  const auto receipts = Replies(j.at("receipt"), {4, 5, 6});
  const auto &receipt = receipts.at(4);
  if (receipt.is_null())
    return Pending("settlement_receipt_pending");
  const auto block = transfer_evidence::Receipt(
      expected, event, anchor, receipt, receipts.at(5), receipts.at(6));

  return {{"kind", "verified"},
          {"evidence",
           {{"chain", profile->chain},
            {"profileId", profile->id},
            {"transaction", Hash(event.at("transactionHash"))},
            {"blockHash", block.hash},
            {"blockNumber", Quantity(number)},
            {"safeAnchorHash", anchor.hash},
            {"safeAnchorNumber", Quantity(anchor.number)},
            {"finality", "rpc-safe-not-irreversible"}}}};
}
Json Verify(const Json &j) {
  detail::Fields(
      j, {"operation", "anchor", "logs", "receipt", "now", "rpcClientKey"});
  const auto op = detail::ReadOperation(j.at("operation"));
  J::require(
      ValidOperationRecord(op) && op.authorization && op.output &&
          (op.stage == Stage::settling || op.stage == Stage::settlementUnknown),
      "operation not reconcilable");
  const auto profile = PaymentProfileForQuote(op.quote);
  J::require(profile.has_value(), "unsupported reconciliation profile");
  const Json expected{
      {"profileId", profile->id},
      {"payer", Hex(op.quote.scope.payer)},
      {"recipient", Hex(op.quote.recipient)},
      {"amount", J::writeScalar(op.quote.amount)},
      {"nonce", Hex(op.authorization->nonce)},
      {"validAfter", J::writeScalar(op.authorization->validAfter)},
      {"validBefore", J::writeScalar(op.authorization->validBefore)}};
  const auto observed = Observe({{"expected", expected},
                                 {"anchor", j.at("anchor")},
                                 {"logs", j.at("logs")},
                                 {"receipt", j.at("receipt")},
                                 {"now", j.at("now")},
                                 {"rpcClientKey", j.at("rpcClientKey")}});
  if (observed.at("kind") != "verified")
    return observed;
  const auto room = op.stage == Stage::settling ? 2u : 1u;
  J::require(op.revision <= std::numeric_limits<std::uint64_t>::max() - room,
             "revision overflow");
  Event e;
  e.expectedRevision = op.revision;
  e.now = J::readScalar<std::uint64_t>(j.at("now"));
  Json commands = Json::array();
  const auto command = [&] {
    return Json{{"version", 1},
                {"quote", detail::WriteQuote(op.quote)},
                {"event", detail::WriteEvent(e)}}
        .dump();
  };
  if (op.stage == Stage::settling) {
    e.kind = EventKind::settlementUncertain;
    commands.push_back(command());
    ++e.expectedRevision;
  }
  e.kind = EventKind::reconciledSucceeded;
  e.transfer = Transfer{Fixed<32>(observed.at("evidence").at("transaction")),
                        op.authorization->nonce, op.quote.scope,
                        op.quote.recipient, op.quote.amount};
  commands.push_back(command());
  return {{"kind", "complete"},
          {"commands", commands},
          {"evidence", observed.at("evidence")}};
}
} // namespace
std::string SettlementReconciliationPacket(std::string_view text) noexcept {
  try {
    return Verify(detail::Parse(text, 1048576)).dump();
  } catch (const std::exception &) {
    return Pending("settlement_evidence_unavailable").dump();
  } catch (...) {
    return Pending("settlement_reconciliation_unavailable").dump();
  }
}
std::string PaymentAuthorizationEvidencePacket(std::string_view text) noexcept {
  try {
    return Observe(detail::Parse(text, 1048576)).dump();
  } catch (const std::exception &) {
    return Pending("settlement_evidence_unavailable").dump();
  } catch (...) {
    return Pending("settlement_reconciliation_unavailable").dump();
  }
}
} // namespace arken::file_conversion::service::v1::payment
