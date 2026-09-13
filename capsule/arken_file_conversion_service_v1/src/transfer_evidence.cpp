#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include "arken_file_conversion_service_v1/reconciliation_configuration.hpp"
#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "transfer_evidence_internal.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <algorithm>
#include <charconv>
namespace arken::file_conversion::service::v1::payment {
namespace {
using detail::Json;
namespace J = arken::schema::cpp::v1::tool_json;
using rpc::Data;
using rpc::Hash;
using rpc::Quantity;
constexpr std::string_view transferTopic =
    "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef";
template <std::size_t N>
std::string Hex(const std::array<std::uint8_t, N> &bytes) {
  arken::schema::cpp::v1::fixed_id<N, 1> id;
  for (std::size_t i = 0; i < N; ++i)
    id.value[i] = std::byte(bytes[i]);
  return "0x" + J::write(id).template get<std::string>();
}
template <std::size_t N> std::array<std::uint8_t, N> Fixed(const Json &value) {
  const auto text = Data(value, N);
  const auto id =
      J::read<arken::schema::cpp::v1::fixed_id<N, 1>>(text.substr(2));
  std::array<std::uint8_t, N> out{};
  for (std::size_t i = 0; i < N; ++i)
    out[i] = std::to_integer<std::uint8_t>(id.value[i]);
  return out;
}
std::string AddressTopic(const Address &a) {
  return "0x" + std::string(24, '0') + Hex(a).substr(2);
}
std::uint64_t Amount(const Json &value) {
  const auto s = Data(value, 32);
  J::require(s.substr(2, 48) == std::string(48, '0'), "amount overflow");
  std::uint64_t n{};
  const auto r = std::from_chars(s.data() + 50, s.data() + 66, n, 16);
  J::require(r.ec == std::errc{} && r.ptr == s.data() + 66, "amount invalid");
  return n;
}
Json Call(unsigned id, std::string_view method, Json params) {
  return {
      {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Json Pending(std::string_view code) {
  return {{"kind", "pending"}, {"code", code}};
}
} // namespace
namespace transfer_evidence {
void Identity(const Json &log, const Json &event) {
  J::require(!J::readScalar<bool>(log.at("removed")) &&
                 Hash(log.at("transactionHash")) ==
                     Hash(event.at("transactionHash")) &&
                 Hash(log.at("blockHash")) == Hash(event.at("blockHash")) &&
                 Quantity(log.at("blockNumber")) ==
                     Quantity(event.at("blockNumber")) &&
                 Quantity(log.at("transactionIndex")) ==
                     Quantity(event.at("transactionIndex")),
             "log identity mismatch");
}
void AuthorizationLog(const Json &log, const Expectation &e) {
  J::require(Data(log.at("address"), 20) == Hex(e.scope.asset) &&
                 log.at("topics").is_array() && log.at("topics").size() == 3 &&
                 Data(log.at("topics")[0], 32) == authorizationTopic &&
                 Data(log.at("topics")[1], 32) == AddressTopic(e.scope.payer) &&
                 Data(log.at("topics")[2], 32) == Hex(e.nonce) &&
                 Data(log.at("data"), 0) == "0x",
             "authorization event mismatch");
}
rpc::Block Receipt(const Expectation &e, const Json &event,
                   const rpc::Block &anchor, const Json &receipt,
                   const Json &blockValue, const Json &anchorValue) {
  const auto block = rpc::Header(blockValue),
             anchorNow = rpc::Header(anchorValue);
  const auto number = Quantity(event.at("blockNumber")),
             eventIndex = Quantity(event.at("logIndex"));
  J::require(anchorNow == anchor && block.number == number &&
                 number <= anchor.number &&
                 block.hash == Hash(event.at("blockHash")) &&
                 block.time <= anchor.time && e.validAfter < block.time &&
                 block.time < e.validBefore,
             "block evidence mismatch");
  J::require(Quantity(receipt.at("status")) == 1 &&
                 Hash(receipt.at("transactionHash")) ==
                     Hash(event.at("transactionHash")) &&
                 Hash(receipt.at("blockHash")) == block.hash &&
                 Quantity(receipt.at("blockNumber")) == number &&
                 Quantity(receipt.at("transactionIndex")) ==
                     Quantity(event.at("transactionIndex")),
             "receipt mismatch");
  const auto &all = receipt.at("logs");
  J::require(all.is_array() && all.size() <= 128, "receipt log limit");
  unsigned tokenLogs = 0, authorizations = 0, transfers = 0;
  for (const auto &log : all) {
    if (Data(log.at("address"), 20) != Hex(e.scope.asset))
      continue;
    ++tokenLogs;
    Identity(log, event);
    const auto index = Quantity(log.at("logIndex"));
    const auto &topics = log.at("topics");
    J::require(topics.is_array() && topics.size() == 3, "token topics invalid");
    const auto topic = Data(topics[0], 32);
    if (topic == authorizationTopic) {
      AuthorizationLog(log, e);
      J::require(index == eventIndex, "authorization index mismatch");
      ++authorizations;
    } else if (topic == transferTopic) {
      J::require(index > eventIndex &&
                     Data(topics[1], 32) == AddressTopic(e.scope.payer) &&
                     Data(topics[2], 32) == AddressTopic(e.recipient) &&
                     Amount(log.at("data")) == e.amount,
                 "transfer mismatch");
      ++transfers;
    }
  }
  J::require(tokenLogs == 2 && authorizations == 1 && transfers == 1,
             "ambiguous token transfers");
  return block;
}
} // namespace transfer_evidence
namespace {
Json InspectTransfer(const Json &input) {
  detail::Fields(input, {"expected", "anchor", "receipt", "blocks", "now",
                         "rpcClientKey"});
  const auto &wanted = input.at("expected");
  detail::Fields(wanted, {"profileId", "payer", "recipient", "amount", "nonce",
                          "validAfter", "validBefore", "transaction"});
  const auto profile =
      PaymentProfileById(J::readScalar<std::string>(wanted.at("profileId")));
  J::require(profile.has_value(), "transfer profile unavailable");
  transfer_evidence::Expectation e{
      {profile->chain, profile->asset, Fixed<20>(wanted.at("payer"))},
      Fixed<20>(wanted.at("recipient")),
      Fixed<32>(wanted.at("nonce")),
      J::readScalar<std::uint64_t>(wanted.at("amount")),
      J::readScalar<std::uint64_t>(wanted.at("validAfter")),
      J::readScalar<std::uint64_t>(wanted.at("validBefore"))};
  J::require(e.recipient == ImagePaymentRecipient() &&
                 e.amount == profile->imagePrice &&
                 e.validAfter < e.validBefore && e.validBefore <= 4102444800 &&
                 std::ranges::any_of(
                     e.scope.payer, [](auto b) { return b != 0; }),
             "transfer expectation invalid");
  const auto transaction = Hash(wanted.at("transaction"));
  const auto now = J::readScalar<std::uint64_t>(input.at("now"));
  J::require(now <= 4102444800, "transfer clock invalid");
  const auto &key = input.at("rpcClientKey");
  J::require(key.is_null() || key.is_string(), "transfer rpc key invalid");
  const auto endpoint = ResolveReconciliationEndpoint(
      *profile, key.is_null() ? std::nullopt
                              : std::optional<std::string_view>(
                                    key.get_ref<const std::string &>()));
  if (!endpoint.error.empty())
    return Pending(endpoint.error);
  const auto read = [&](std::string_view slot, Json calls) {
    return Json{{"kind", "rpc"},
                {"slot", slot},
                {"request", {{"url", endpoint.url}, {"body", calls.dump()}}}};
  };
  if (input.at("anchor").is_null())
    return read("anchor", Json::array({Call(1, "eth_chainId", Json::array()),
                                       Call(2, "eth_getBlockByNumber",
                                            Json::array({"safe", false}))}));
  const auto anchors = rpc::Replies(input.at("anchor"), {1, 2});
  J::require(Quantity(anchors.at(1)) == profile->chain,
             "transfer chain mismatch");
  const auto anchor = rpc::Header(anchors.at(2));
  J::require(anchor.time <= now, "transfer future anchor");
  if (input.at("receipt").is_null())
    return read("receipt", Json::array({Call(3, "eth_getTransactionReceipt",
                                             Json::array({transaction}))}));
  const auto receipt = rpc::Replies(input.at("receipt"), {3}).at(3);
  if (receipt.is_null())
    return Pending("transfer_receipt_pending");
  J::require(Hash(receipt.at("transactionHash")) == transaction,
             "transfer transaction mismatch");
  if (Quantity(receipt.at("status")) != 1)
    return Pending("transfer_transaction_failed");
  const auto number = Quantity(receipt.at("blockNumber"));
  if (number > anchor.number)
    return Pending("transfer_not_safe_yet");
  const auto &logs = receipt.at("logs");
  J::require(logs.is_array() && logs.size() <= 128, "transfer log limit");
  Json event;
  unsigned matches = 0;
  for (const auto &log : logs) {
    if (Data(log.at("address"), 20) != profile->assetText)
      continue;
    const auto &topics = log.at("topics");
    if (!topics.is_array() || topics.empty() ||
        Data(topics[0], 32) != transfer_evidence::authorizationTopic)
      continue;
    transfer_evidence::AuthorizationLog(log, e);
    transfer_evidence::Identity(log, log);
    event = log;
    ++matches;
  }
  if (matches == 0)
    return Pending("transfer_authorization_not_observed");
  J::require(matches == 1, "transfer authorization ambiguous");
  if (input.at("blocks").is_null())
    return read(
        "blocks",
        Json::array({Call(4, "eth_getBlockByNumber",
                          Json::array({Quantity(number), false})),
                     Call(5, "eth_getBlockByNumber",
                          Json::array({Quantity(anchor.number), false}))}));
  const auto blocks = rpc::Replies(input.at("blocks"), {4, 5});
  const auto block = transfer_evidence::Receipt(e, event, anchor, receipt,
                                                blocks.at(4), blocks.at(5));
  return {{"kind", "verified"},
          {"evidence",
           {{"chain", profile->chain},
            {"profileId", profile->id},
            {"transaction", transaction},
            {"token", profile->assetText},
            {"payer", Hex(e.scope.payer)},
            {"recipient", Hex(e.recipient)},
            {"amount", std::to_string(e.amount)},
            {"nonce", Hex(e.nonce)},
            {"blockHash", block.hash},
            {"blockNumber", Quantity(number)},
            {"safeAnchorHash", anchor.hash},
            {"safeAnchorNumber", Quantity(anchor.number)},
            {"finality", "rpc-safe-not-irreversible"},
            {"irreversibleFinalityProved", false}}},
          {"ledgerMutations", 0},
          {"paymentRequests", 0}};
}
} // namespace
std::string PaymentTransferEvidencePacket(std::string_view text) noexcept {
  try {
    return InspectTransfer(detail::Parse(text, 1048576)).dump();
  } catch (const std::exception &) {
    return Pending("transfer_evidence_unavailable").dump();
  } catch (...) {
    return Pending("transfer_verifier_unavailable").dump();
  }
}
} // namespace arken::file_conversion::service::v1::payment
