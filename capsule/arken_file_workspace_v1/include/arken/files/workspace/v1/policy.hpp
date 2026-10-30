#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arken::files::workspace::v1 {
inline constexpr std::uint64_t maximum_file_bytes = 1024ULL * 1024 * 1024;
inline constexpr std::size_t maximum_entries = 20000;
inline constexpr std::size_t maximum_depth = 20;
struct Identity { std::uint64_t bytes{}; std::string sha256; };
struct Source { std::string reference; std::string name; Identity content; };
enum class TransferAction { create, reuse, reject };
struct TransferPlan {
  TransferAction action{TransferAction::reject};
  std::vector<std::string> path;
  std::string error;
};
inline bool valid_digest(std::string_view value) {
  if(value.size()!=64)return false;
  for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
}
inline bool valid_reference(std::string_view value) {
  if(value.size()!=36)return false;
  for(std::size_t i=0;i<value.size();++i){
    if(i==8||i==13||i==18||i==23){if(value[i]!='-')return false;}
    else if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
  }
  return true;
}
inline bool valid_relative_path(const std::vector<std::string>& path) {
  if(path.empty()||path.size()>maximum_depth)return false;
  for(const auto& part:path){
    if(part.empty()||part.size()>255||part=="."||part=="..")return false;
    for(unsigned char c:part)if(c<32||c==127||c=='/'||c=='\\')return false;
  }
  return true;
}
inline std::string destination_name(std::string_view name) {
  // Identity lives in the UUID directory, not in a lossy filename transform.
  if(name.empty()||name.size()>200)return "file";
  std::string result;
  for(unsigned char c:name)result+=c<32||c==127||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|'?'_':static_cast<char>(c);
  while(!result.empty()&&(result.back()=='.'||result.back()==' '))result.pop_back();
  if(result.empty()||result=="."||result=="..")return "file";
  auto stem=result.substr(0,result.find('.'));for(char& c:stem)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
  if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9'))result="file-"+result;
  return result;
}
inline TransferPlan plan_transfer(const Source& source,bool destination_exists,const Identity& destination={},std::string_view root_name="Arken Files") {
  if(!valid_reference(source.reference)||!valid_digest(source.content.sha256)||source.content.bytes>maximum_file_bytes)
    return {TransferAction::reject,{},"Invalid or oversized source file."};
  if(!valid_relative_path({std::string(root_name)}))return {TransferAction::reject,{},"Invalid destination collection."};
  TransferPlan result{TransferAction::create,{std::string(root_name),source.reference,destination_name(source.name)}, {}};
  if(destination_exists){
    if(!valid_digest(destination.sha256)||destination.bytes!=source.content.bytes||destination.sha256!=source.content.sha256)
      return {TransferAction::reject,result.path,"A different file already occupies this destination. Nothing was overwritten."};
    result.action=TransferAction::reuse;
  }
  return result;
}
inline bool may_adopt_directory_file(const Source& source,const Identity& readback,bool write_closed,bool browser_copy_committed) {
  return plan_transfer(source,false).action!=TransferAction::reject&&write_closed&&browser_copy_committed
      &&valid_digest(readback.sha256)&&source.content.bytes==readback.bytes&&source.content.sha256==readback.sha256;
}
} // namespace arken::files::workspace::v1
