#include "arken_file_conversion_service_v1/public_admission.hpp"
#include "payment_json.hpp"
#include "arken_schema_generated/command-json.hpp"
#include <set>
namespace arken::file_conversion::service::v1 {
namespace {
namespace J=arken::schema::cpp::v1::tool_json;
namespace S=arken::schema::generated::conversion::v1;
using payment::detail::Json;
constexpr std::uint64_t maximumFiles=64,maximumBytes=64*1024*1024,maximumFileBytes=8*1024*1024;
std::string Identifier(const Json& j){return J::write(J::read<arken::schema::cpp::v1::fixed_id<32,1>>(j)).get<std::string>();}
Json Role(){return {{"version",1},{"role","public-upload-budget"}};}
Json Capability(const Json& j){payment::detail::Fields(j,{"kind","fileId","fileToken"});
  if(j.at("fileToken").is_null())return {{"allowed",false},{"code","file_capability_required"}};
  const auto value=J::read<S::file_target>(Json{{"file_id",j.at("fileId")},{"file_token",j.at("fileToken")}});
  J::require(value.file_token.has_value(),"file capability absent");
  const auto hash=payment::detail::PayloadDigest(J::write(*value.file_token).get<std::string>());
  arken::schema::cpp::v1::fixed_id<32,1> owner;for(std::size_t i=0;i<32;++i)owner.value[i]=std::byte(hash[i]);
  return {{"allowed",true},{"owner",J::write(owner)}};
}
Json Access(const Json& j){payment::detail::Fields(j,{"kind","sandbox","hasAuthorization","administrator","intent","fileId","fileToken"});
  const bool supplied=J::readScalar<bool>(j.at("hasAuthorization")),administrator=J::readScalar<bool>(j.at("administrator"));
  J::require(!administrator||supplied,"invalid administrator observation");
  if(administrator)return {{"mode","operator"}};
  if(supplied||!J::readScalar<bool>(j.at("sandbox")))return {{"mode","denied"},{"code","authorization_required"}};
  const auto intent=J::readScalar<std::string>(j.at("intent"));
  if(intent=="discovery"||intent=="upload")return {{"mode","public"}};
  J::require(intent=="file","public access intent invalid");
  Json capability;
  try{capability=Capability(Json{{"kind","capability"},{"fileId",j.at("fileId")},{"fileToken",j.at("fileToken")}});}
  catch(const std::exception&){return {{"mode","denied"},{"code","file_capability_invalid"}};}
  if(!capability.at("allowed").get<bool>())return {{"mode","denied"},{"code",capability.at("code")}};
  return {{"mode","public"},{"owner",capability.at("owner")}};
}
Json Reserve(const Json& j){payment::detail::Fields(j,{"kind","record","scope","operation","bytes","now"});
  const auto operation=Identifier(j.at("operation"));const auto bytes=J::readScalar<std::uint64_t>(j.at("bytes")),now=J::readScalar<std::uint64_t>(j.at("now"));
  J::require(bytes>0&&bytes<=maximumFileBytes&&now<=4102444800,"public admission input invalid");const auto day=now/86400;
  J::require(j.at("scope").is_null()||payment::detail::Parse(J::readScalar<std::string>(j.at("scope")))==Role(),"wrong actor role");
  J::require(j.at("scope").is_null()==j.at("record").is_null(),"partial admission state");
  Json entries=Json::array();std::uint64_t recordDay=day,total=0;
  if(!j.at("record").is_null()){
    J::require(!j.at("scope").is_null(),"admission role missing");const auto record=payment::detail::Parse(J::readScalar<std::string>(j.at("record")));
    payment::detail::Fields(record,{"version","day","reservations"});J::require(J::readScalar<unsigned>(record.at("version"))==1,"admission version");
    recordDay=J::readScalar<std::uint64_t>(record.at("day"));entries=record.at("reservations");J::require(entries.is_array()&&entries.size()<=maximumFiles&&recordDay<=day,"admission state or clock invalid");
    std::set<std::string> ids;
    for(const auto& entry:entries){payment::detail::Fields(entry,{"operation","bytes"});J::require(ids.insert(Identifier(entry.at("operation"))).second,"duplicate reservation");
      const auto size=J::readScalar<std::uint64_t>(entry.at("bytes"));J::require(size>0&&size<=maximumFileBytes&&size<=maximumBytes-total,"invalid reservation bytes");total+=size;}
  }
  if(recordDay<day){entries=Json::array();total=0;}
  for(const auto& entry:entries)if(entry.at("operation")==operation){J::require(entry.at("bytes")==j.at("bytes"),"reservation identity conflict");
    return {{"allowed",true},{"replayed",true},{"writes",Json::array()}};}
  if(entries.size()>=maximumFiles||bytes>maximumBytes-total)return {{"allowed",false},{"code","public_upload_budget_exceeded"},
    {"retryAfterSeconds",(day+1)*86400-now},{"writes",Json::array()}};
  entries.push_back({{"operation",operation},{"bytes",J::writeScalar(bytes)}});
  Json writes=Json::array({{{"key","admission"},{"value",Json{{"version",1},{"day",J::writeScalar(day)},{"reservations",entries}}.dump()}}});
  if(j.at("scope").is_null())writes.push_back({{"key","scope"},{"value",Role().dump()}});
  return {{"allowed",true},{"replayed",false},{"writes",writes}};
}
}
std::string PublicAdmissionPacket(std::string_view text) noexcept {
  try{const auto j=payment::detail::Parse(text);const auto kind=J::readScalar<std::string>(j.at("kind"));
    if(kind=="lookup"){payment::detail::Fields(j,{"kind"});return Json{{"actorKey","public-upload-budget-v1"},{"recordKey","admission"},{"scopeKey","scope"}}.dump();}
    if(kind=="capability")return Capability(j).dump();if(kind=="access")return Access(j).dump();if(kind=="reserve")return Reserve(j).dump();
    return "{\"packetError\":\"public_admission_kind_invalid\"}";
  }catch(const std::exception&){return "{\"packetError\":\"public_admission_invalid\"}";}
  catch(...){return "{\"packetError\":\"public_admission_unavailable\"}";}
}
}
