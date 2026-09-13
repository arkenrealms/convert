#include "rpc_evidence.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"
#include <algorithm>
#include <charconv>
namespace arken::file_conversion::service::v1::payment::rpc {
using detail::Json;namespace J=arken::schema::cpp::v1::tool_json;
std::string Data(const Json& j,std::size_t bytes){auto s=J::readScalar<std::string>(j);J::require(s.size()==2+bytes*2&&s.starts_with("0x"),"data width");
  for(std::size_t i=2;i<s.size();++i){auto& c=s[i];if(c>='A'&&c<='F')c=char(c-'A'+'a');J::require((c>='0'&&c<='9')||(c>='a'&&c<='f'),"data hex");}return s;}
std::uint64_t Quantity(const Json& j){const auto s=J::readScalar<std::string>(j);J::require(s.size()>=3&&s.size()<=18&&s.starts_with("0x")&&(s.size()==3||s[2]!='0'),"quantity shape");
  std::uint64_t n=0;const auto r=std::from_chars(s.data()+2,s.data()+s.size(),n,16);J::require(r.ec==std::errc{}&&r.ptr==s.data()+s.size(),"quantity overflow");return n;}
std::string Quantity(std::uint64_t n){char bytes[16];const auto r=std::to_chars(bytes,bytes+16,n,16);return "0x"+std::string(bytes,r.ptr);}
std::string Hash(const Json& j){const auto s=Data(j,32);J::require(s!=std::string("0x")+std::string(64,'0'),"zero hash");return s;}
Block Header(const Json& j){return {Quantity(j.at("number")),Quantity(j.at("timestamp")),Hash(j.at("hash"))};}
std::map<unsigned,Json> Replies(const Json& value,std::initializer_list<unsigned> ids){
  const auto array=detail::Parse(J::readScalar<std::string>(value),262144);J::require(array.is_array()&&array.size()==ids.size(),"RPC batch size");std::map<unsigned,Json> results;
  for(const auto& reply:array){detail::Fields(reply,{"jsonrpc","id","result"});J::require(reply.at("jsonrpc")=="2.0","RPC version");
    const auto id=J::readScalar<unsigned>(reply.at("id"));J::require(std::find(ids.begin(),ids.end(),id)!=ids.end()&&results.emplace(id,reply.at("result")).second,"RPC id mismatch");}
  return results;
}
}
