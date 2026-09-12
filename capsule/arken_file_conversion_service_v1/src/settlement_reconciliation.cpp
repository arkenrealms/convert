#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "payment_json.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
namespace arken::file_conversion::service::v1::payment {
namespace {
using detail::Json;namespace J=arken::schema::cpp::v1::tool_json;
constexpr std::string_view rpc="https://sepolia.base.org";
constexpr std::string_view asset="0x036cbd53842c5426634e7929541ec2318f3dcf7e";
constexpr std::string_view used="0x98de503528ee59b575ef0c0a2576a82497bfc029a5685b209e9ec333479b10a5";
constexpr std::string_view transferTopic="0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef";
template<std::size_t N> std::string Hex(const std::array<std::uint8_t,N>& value){
  arken::schema::cpp::v1::fixed_id<N,1> id;for(std::size_t i=0;i<N;++i)id.value[i]=std::byte(value[i]);return "0x"+J::write(id).template get<std::string>();}
std::string Data(const Json& j,std::size_t bytes){auto s=J::readScalar<std::string>(j);J::require(s.size()==2+bytes*2&&s.starts_with("0x"),"data width");
  for(std::size_t i=2;i<s.size();++i){auto& c=s[i];if(c>='A'&&c<='F')c=char(c-'A'+'a');J::require((c>='0'&&c<='9')||(c>='a'&&c<='f'),"data hex");}return s;}
std::uint64_t Quantity(const Json& j){const auto s=J::readScalar<std::string>(j);J::require(s.size()>=3&&s.size()<=18&&s.starts_with("0x")&&(s.size()==3||s[2]!='0'),"quantity shape");
  std::uint64_t n=0;const auto r=std::from_chars(s.data()+2,s.data()+s.size(),n,16);J::require(r.ec==std::errc{}&&r.ptr==s.data()+s.size(),"quantity overflow");return n;}
std::string Quantity(std::uint64_t n){char bytes[16];const auto r=std::to_chars(bytes,bytes+16,n,16);return "0x"+std::string(bytes,r.ptr);}
std::string Hash(const Json& j){const auto s=Data(j,32);J::require(s!=std::string("0x")+std::string(64,'0'),"zero hash");return s;}
std::string AddressTopic(const Address& a){return "0x"+std::string(24,'0')+Hex(a).substr(2);}
struct Block {std::uint64_t number,time;std::string hash;bool operator==(const Block&)const=default;};
Block Header(const Json& j){return {Quantity(j.at("number")),Quantity(j.at("timestamp")),Hash(j.at("hash"))};}
Json Call(unsigned id,std::string_view method,Json params){return {{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};}
Json Read(std::string_view slot,Json requests){return {{"kind","rpc"},{"slot",slot},{"request",{{"url",rpc},{"body",requests.dump()}}}};}
Json Pending(std::string_view code){return {{"kind","pending"},{"code",code}};}
std::map<unsigned,Json> Replies(const Json& value,std::initializer_list<unsigned> ids){
  const auto array=detail::Parse(J::readScalar<std::string>(value),262144);J::require(array.is_array()&&array.size()==ids.size(),"RPC batch size");std::map<unsigned,Json> results;
  for(const auto& reply:array){detail::Fields(reply,{"jsonrpc","id","result"});J::require(reply.at("jsonrpc")=="2.0","RPC version");
    const auto id=J::readScalar<unsigned>(reply.at("id"));J::require(std::find(ids.begin(),ids.end(),id)!=ids.end()&&results.emplace(id,reply.at("result")).second,"RPC id mismatch");}
  return results;
}
void Identity(const Json& log,const Json& event){
  J::require(!J::readScalar<bool>(log.at("removed"))&&Hash(log.at("transactionHash"))==Hash(event.at("transactionHash"))
    &&Hash(log.at("blockHash"))==Hash(event.at("blockHash"))&&Quantity(log.at("blockNumber"))==Quantity(event.at("blockNumber"))
    &&Quantity(log.at("transactionIndex"))==Quantity(event.at("transactionIndex")),"log identity mismatch");
}
void AuthorizationLog(const Json& log,const Operation& op){
  J::require(Data(log.at("address"),20)==asset&&log.at("topics").is_array()&&log.at("topics").size()==3
    &&Data(log.at("topics")[0],32)==used&&Data(log.at("topics")[1],32)==AddressTopic(op.quote.scope.payer)
    &&Data(log.at("topics")[2],32)==Hex(op.authorization->nonce)&&Data(log.at("data"),0)=="0x","authorization event mismatch");
}
std::uint64_t Amount(const Json& j){const auto s=Data(j,32);J::require(s.substr(2,48)==std::string(48,'0'),"amount overflow");
  std::uint64_t value=0;const auto r=std::from_chars(s.data()+50,s.data()+66,value,16);J::require(r.ec==std::errc{}&&r.ptr==s.data()+66,"amount invalid");return value;}
Json Verify(const Json& j){
  detail::Fields(j,{"operation","anchor","logs","receipt","now"});const auto op=detail::ReadOperation(j.at("operation"));
  J::require(ValidOperationRecord(op)&&op.authorization&&op.output&&(op.stage==Stage::settling||op.stage==Stage::settlementUnknown),"operation not reconcilable");
  J::require(op.quote.scope.chain==84532&&Hex(op.quote.scope.asset)==asset,"sandbox profile required");
  const auto now=J::readScalar<std::uint64_t>(j.at("now"));
  if(j.at("anchor").is_null())return Read("anchor",Json::array({Call(1,"eth_chainId",Json::array()),Call(2,"eth_getBlockByNumber",Json::array({"safe",false}))}));
  const auto anchorReplies=Replies(j.at("anchor"),{1,2});J::require(Quantity(anchorReplies.at(1))==84532,"wrong chain");
  const auto anchor=Header(anchorReplies.at(2));J::require(anchor.time<=now,"future anchor");const auto first=anchor.number>2047?anchor.number-2047:0;
  if(j.at("logs").is_null())return Read("logs",Json::array({Call(3,"eth_getLogs",Json::array({Json{{"address",asset},{"fromBlock",Quantity(first)},{"toBlock",Quantity(anchor.number)},
    {"topics",Json::array({used,AddressTopic(op.quote.scope.payer),Hex(op.authorization->nonce)})}}}))}));
  const auto logs=Replies(j.at("logs"),{3}).at(3);J::require(logs.is_array(),"logs shape");
  if(logs.empty())return Pending("settlement_not_observed");J::require(logs.size()==1,"ambiguous authorization");
  const auto& event=logs[0];AuthorizationLog(event,op);Identity(event,event);const auto eventIndex=Quantity(event.at("logIndex"));
  const auto number=Quantity(event.at("blockNumber"));J::require(number>=first&&number<=anchor.number,"event outside search");
  if(j.at("receipt").is_null())return Read("receipt",Json::array({Call(4,"eth_getTransactionReceipt",Json::array({Hash(event.at("transactionHash"))})),
    Call(5,"eth_getBlockByNumber",Json::array({Quantity(number),false})),Call(6,"eth_getBlockByNumber",Json::array({Quantity(anchor.number),false}))}));
  const auto receipts=Replies(j.at("receipt"),{4,5,6});const auto& receipt=receipts.at(4);
  if(receipt.is_null())return Pending("settlement_receipt_pending");
  const auto block=Header(receipts.at(5)),anchorNow=Header(receipts.at(6));
  J::require(anchorNow==anchor&&block.number==number&&block.hash==Hash(event.at("blockHash"))&&block.time<=anchor.time
    &&op.authorization->validAfter<block.time&&block.time<op.authorization->validBefore,"block evidence mismatch");
  J::require(Quantity(receipt.at("status"))==1&&Hash(receipt.at("transactionHash"))==Hash(event.at("transactionHash"))
    &&Hash(receipt.at("blockHash"))==block.hash&&Quantity(receipt.at("blockNumber"))==number
    &&Quantity(receipt.at("transactionIndex"))==Quantity(event.at("transactionIndex")),"receipt mismatch");
  const auto& all=receipt.at("logs");J::require(all.is_array()&&all.size()<=128,"receipt log limit");unsigned tokenLogs=0,authorizations=0,transfers=0;
  for(const auto& log:all){if(Data(log.at("address"),20)!=asset)continue;++tokenLogs;Identity(log,event);const auto index=Quantity(log.at("logIndex"));
    const auto& topics=log.at("topics");J::require(topics.is_array()&&topics.size()==3,"token topics invalid");const auto topic=Data(topics[0],32);
    if(topic==used){AuthorizationLog(log,op);J::require(index==eventIndex,"authorization index mismatch");++authorizations;}
    else if(topic==transferTopic){J::require(index>eventIndex&&Data(topics[1],32)==AddressTopic(op.quote.scope.payer)&&Data(topics[2],32)==AddressTopic(op.quote.recipient)
      &&Amount(log.at("data"))==op.quote.amount,"transfer mismatch");++transfers;}
  }
  J::require(tokenLogs==2&&authorizations==1&&transfers==1,"ambiguous token transfers");
  const auto room=op.stage==Stage::settling?2u:1u;J::require(op.revision<=std::numeric_limits<std::uint64_t>::max()-room,"revision overflow");
  Event e;e.expectedRevision=op.revision;e.now=now;Json commands=Json::array();
  const auto command=[&]{return Json{{"version",1},{"quote",detail::WriteQuote(op.quote)},{"event",detail::WriteEvent(e)}}.dump();};
  if(op.stage==Stage::settling){e.kind=EventKind::settlementUncertain;commands.push_back(command());++e.expectedRevision;}
  Transfer transfer;transfer.scope=op.quote.scope;transfer.nonce=op.authorization->nonce;transfer.recipient=op.quote.recipient;transfer.amount=op.quote.amount;
  const auto hash=J::read<arken::schema::cpp::v1::fixed_id<32,1>>(Hash(event.at("transactionHash")).substr(2));
  for(std::size_t i=0;i<32;++i)transfer.transaction[i]=std::to_integer<std::uint8_t>(hash.value[i]);
  e.kind=EventKind::reconciledSucceeded;e.transfer=transfer;commands.push_back(command());
  return {{"kind","complete"},{"commands",commands},{"evidence",{{"chain",84532},{"transaction",Hex(transfer.transaction)},
    {"blockHash",block.hash},{"blockNumber",Quantity(number)},{"safeAnchorHash",anchor.hash},{"safeAnchorNumber",Quantity(anchor.number)},
    {"finality","rpc-safe-not-irreversible"}}}};
}
}
std::string SettlementReconciliationPacket(std::string_view text) noexcept {
  try{return Verify(detail::Parse(text,1048576)).dump();}
  catch(const std::exception&){return Pending("settlement_evidence_unavailable").dump();}
  catch(...){return Pending("settlement_reconciliation_unavailable").dump();}
}
}
