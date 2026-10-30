#include "arken_file_conversion_commands_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken/files/workspace/v1/native_files.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_schema_generated/command-json.hpp"
#include <array>

namespace arken::file_conversion::commands::v1 {
namespace mcp=arken::mcp::v1;
namespace J=arken::schema::cpp::v1::tool_json;
namespace S=arken::schema::generated::conversion_files::v1;
namespace C=arken::file_conversion::v1;
namespace I=arken::image::v1;
using Json=mcp::Json;
namespace {
struct Pair {std::uint32_t id;I::Format input,output;const char* description;};
constexpr Pair pairs[]{
 {3,I::Format::png,I::Format::jpeg,"Convert PNG to JPEG for smaller photo files. Use relative source_path and new output_path; transparency needs explicit alpha_policy=flatten_white."},
 {4,I::Format::jpeg,I::Format::png,"Convert JPEG to PNG for lossless subsequent edits. Uses relative source_path and a new output_path; does not restore detail already lost to JPEG compression."},
 {5,I::Format::bmp,I::Format::png,"Convert BMP to PNG for compact lossless image storage. Uses relative source_path and a new output_path."},
 {6,I::Format::bmp,I::Format::jpeg,"Convert BMP to JPEG for smaller photo files. Uses relative source_path and new output_path; transparency needs explicit alpha_policy=flatten_white."},
 {7,I::Format::tga,I::Format::png,"Convert TGA to PNG for sharing and standard image workflows. Uses relative source_path and a new output_path; preserves admitted transparency."},
 {8,I::Format::dds,I::Format::png,"Convert a supported DDS texture to PNG for inspection and editing. Uses relative source_path and a new output_path; unsupported DDS profiles are rejected."}
};
std::vector<mcp::ToolPresentation> presentations() {
 std::vector<mcp::ToolPresentation> result{{2,false,false,false,
  "Convert PNG, JPEG, BMP, TGA or supported DDS to PNG, JPEG, BMP or TGA. Relative source_path and new output_path stay inside the configured folder. No overwrites. Limits: 8 MiB input/output, 1,048,576 pixels, 4096 per dimension. JPEG transparency requires explicit flatten_white."}};
 for(const auto& p:pairs)result.push_back({p.id,false,false,false,p.description});
 return result;
}
const mcp::Server& server() {
 static const auto value=[] {
  mcp::ServerDescription d{"Arken Convert","r6262",
   "Convert files inside the launch-selected folder. Paths are relative; parents must exist. Output files are created exclusively, never overwritten. Discovery is free of filesystem effects. No upload, account, network or payment is required.",
   S::commandCatalog,presentations()};
  d.toolNameStyle=mcp::ToolNameStyle::familyLocal;return mcp::Server(std::move(d));
 }();return value;
}
struct Input {std::string source,output;C::ImageOptions options;};
template<class Request> Input input(const Request& v,I::Format from,I::Format to) {
 J::require(!v.source_path.value.empty()&&v.source_path.value.size()<=4096
  &&!v.output_path.value.empty()&&v.output_path.value.size()<=4096,"path bounds");
 J::require(v.source_path!=v.output_path,"output must be a new path");
 C::ImageOptions options{from,to};
 if constexpr(requires {v.jpeg_quality;v.alpha_policy;}) {
  J::require(to==I::Format::jpeg||(!v.jpeg_quality&&!v.alpha_policy),"JPEG options require JPEG output");
  options.jpegQuality=v.jpeg_quality?v.jpeg_quality->value:95u;
  options.alphaPolicy=v.alpha_policy?static_cast<I::AlphaPolicy>(static_cast<std::uint32_t>(*v.alpha_policy)-1u):I::AlphaPolicy::preserve;
 }
 return {v.source_path.value,v.output_path.value,options};
}
Input parse(std::uint32_t id,const Json& arguments) {
 if(id==2){const auto v=J::read<S::conversion_request>(arguments);return input(v,static_cast<I::Format>(v.input_format),static_cast<I::Format>(v.output_format));}
 for(const auto& p:pairs)if(p.id==id) {
  if(p.output==I::Format::jpeg)return input(J::read<S::pair_request>(arguments),p.input,p.output);
  return input(J::read<S::file_request>(arguments),p.input,p.output);
 }
 throw std::invalid_argument("Unknown conversion");
}
std::string media(I::Format format) {
 switch(format){case I::Format::png:return "image/png";case I::Format::jpeg:return "image/jpeg";
  case I::Format::bmp:return "image/bmp";case I::Format::tga:return "image/x-tga";default:throw std::invalid_argument("output format");}
}
Json error(const Json& id,std::string_view code,std::string_view message) {
 return mcp::toolReply(id,{{"code",code},{"message",message}},true);
}
}
struct Session::Impl {std::unique_ptr<arken::files::workspace::v1::NativeFiles> files;};
Session::Session(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
Session::~Session()=default;
std::unique_ptr<Session> Session::open(const std::filesystem::path& root,std::string& error) noexcept {
 try {
  auto files=arken::files::workspace::v1::NativeFiles::open(root,error);if(!files)return {};
  (void)server();auto value=std::make_unique<Impl>();value->files=std::move(files);
  return std::unique_ptr<Session>(new Session(std::move(value)));
 }catch(const std::exception&){error="Conversion server could not initialize.";return {};}
}
std::string catalog(){
 auto result=Json::parse(S::commandCatalog);
 const auto descriptions=presentations();
 for(auto& c:result.at("commands")) {
  for(const auto& p:descriptions)if(c.at("commandId")==p.commandId)c["description"]=p.description;
 }
 return result.dump();
}
Json Session::request(std::string_view text) noexcept {
 Json id=nullptr;
 bool published=false;
 try {
  const auto plan=server().plan(text);if(!plan.isCommand)return plan.response;id=plan.id;
  J::require(!plan.metadata.contains("x402/payment"),"Payments are not supported by this server");
  const auto value=parse(plan.commandId,plan.arguments);
  const auto source=impl_->files->read(value.source,C::maximumSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 8 MiB.");
  const auto converted=C::ConvertImage(source.bytes,value.options);
  if(!converted)return error(id,"conversionFailed",C::ErrorMessage(converted.error));
  const auto digest=arken::digest::v1::sha256(converted.bytes);std::array<char,71> hash{};
  J::require(digest&&arken::digest::v1::format_sha256(*digest,hash),"output digest");
  S::conversion_result result;result.output_path.value=value.output;result.media_type.value=media(value.options.output);
  result.width.value=converted.width;result.height.value=converted.height;
  result.output_bytes.value=static_cast<std::uint32_t>(converted.bytes.size());result.sha256.value.assign(hash.data(),hash.size());
  result.alpha_flattened.value=converted.alphaFlattened;
  const auto written=impl_->files->create(value.output,converted.bytes);published=written.published;result.durable.value=written.durable;
  if(!written) {
   if(written.published){auto detail=J::write(result);detail["code"]="outputPublished";
    detail["message"]="Output was published but finalization failed. Inspect this path and digest before retrying.";
    detail["published"]=true;return mcp::toolReply(id,detail,true);}
   return error(id,written.error,"Output was not created. Choose a new relative path with an existing parent; existing files are never overwritten.");
  }
  return mcp::toolReply(id,J::write(result),false);
 }catch(const std::exception&){
  return mcp::failure(id,published?-32603:-32602,published
   ?"Output was published; inspect the requested path before retrying."
   :"Invalid conversion request");
 }
}
}
