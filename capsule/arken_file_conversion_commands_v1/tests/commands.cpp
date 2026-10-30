#include "arken_file_conversion_v1/api.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_image_codecs_v1/test_support/texture_fixtures.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
namespace fs=std::filesystem;
namespace C=arken::file_conversion::v1;
namespace I=arken::image::v1;
using Json=arken::mcp::v1::Json;
void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
void write(const fs::path& path,std::span<const std::byte> bytes){
 fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
 out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(out),"fixture write");
}
std::vector<std::byte> bytes(const fs::path& path){
 std::ifstream in(path,std::ios::binary);check(bool(in),"output missing");const std::string text{std::istreambuf_iterator<char>(in),{}};
 return {reinterpret_cast<const std::byte*>(text.data()),reinterpret_cast<const std::byte*>(text.data()+text.size())};
}
std::string digest(std::span<const std::byte> data){const auto hash=arken::digest::v1::sha256(data);std::array<char,71> out{};
 check(hash&&arken::digest::v1::format_sha256(*hash,out),"digest");return {out.data(),out.size()};}
void prepare(const fs::path& root){
 check(!fs::exists(root),"fresh proof root required");fs::create_directories(root/"files/inputs");fs::create_directory(root/"files/outputs");
 fs::create_directory(root/"expected");fs::create_directory(root/"outside");
 const std::array<std::uint8_t,16> pixels{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
 std::map<std::string,std::vector<std::byte>> sources;
 for(auto [name,format]:std::initializer_list<std::pair<std::string,I::Format>>{{"png",I::Format::png},{"jpeg",I::Format::jpeg},{"bmp",I::Format::bmp},{"tga",I::Format::tga}}){
  auto encoded=I::EncodeImage(format,2,2,pixels);check(bool(encoded),"source fixture encoding");sources[name]=std::move(encoded.bytes);
 }
 sources["dds"]=texture_fixture::Dds(8,4,1,1);
 for(const auto& [name,data]:sources)write(root/"files/inputs"/(name+".bin"),data);
 auto alpha=pixels;alpha[3]=0;const auto transparent=I::EncodeImage(I::Format::png,2,2,alpha);check(bool(transparent),"alpha fixture");
 write(root/"files/inputs/alpha.png",transparent.bytes);write(root/"outside/private.png",sources.at("png"));
 fs::create_symlink(root/"outside/private.png",root/"files/escape.png");fs::create_directory_symlink(root/"outside",root/"files/link");
 std::ofstream requests(root/"requests.jsonl");Json expected=Json::array();std::uint64_t next=9007199254740993ULL;
 const auto send=[&](Json request,std::string kind,Json data=Json::object()) {
  request["jsonrpc"]="2.0";request["id"]=next++;requests<<request.dump()<<'\n';
  expected.push_back({{"id",request["id"]},{"kind",kind},{"data",std::move(data)}});
 };
 send({{"method","initialize"},{"params",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},{"clientInfo",{{"name","convert-native-proof"},{"version","1"}}}}}},"initialize");
 requests<<"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
 send({{"method","tools/list"}},"catalogue");
 struct Case{const char* name;const char* from;I::Format input,output;};
 const Case cases[]={{"convert_image","png",I::Format::png,I::Format::tga},
  {"png_to_jpeg","png",I::Format::png,I::Format::jpeg},{"jpeg_to_png","jpeg",I::Format::jpeg,I::Format::png},
  {"bmp_to_png","bmp",I::Format::bmp,I::Format::png},{"bmp_to_jpeg","bmp",I::Format::bmp,I::Format::jpeg},
  {"tga_to_png","tga",I::Format::tga,I::Format::png},{"dds_to_png","dds",I::Format::dds,I::Format::png}};
 for(const auto& test:cases){
  const std::string output="outputs/"+std::string(test.name)+".bin";
  const auto converted=C::ConvertImage(sources.at(test.from),{test.input,test.output});check(bool(converted),"reference conversion");
  write(root/"expected"/(std::string(test.name)+".bin"),converted.bytes);
  Json args{{"source_path","inputs/"+std::string(test.from)+".bin"},{"output_path",output}};
  if(std::string_view(test.name)=="convert_image"){args["input_format"]="png";args["output_format"]="tga";}
  send({{"method","tools/call"},{"params",{{"name",test.name},{"arguments",args}}}},"converted",
   {{"output",output},{"expected",std::string(test.name)+".bin"},{"width",converted.width},{"height",converted.height},{"sha256",digest(converted.bytes)}});
 }
 const auto call=[&](const char* name,Json args,std::string kind="toolError"){
  send({{"method","tools/call"},{"params",{{"name",name},{"arguments",std::move(args)}}}},kind);
 };
 call("png_to_jpeg",{{"source_path","inputs/png.bin"},{"output_path","outputs/png_to_jpeg.bin"}});
 for(const auto* source:{"../outside/private.png","/etc/passwd","escape.png","link/private.png"})
  call("png_to_jpeg",{{"source_path",source},{"output_path","outputs/forbidden.bin"}});
 for(const auto* output:{"../outside/output.jpg","/tmp/unselected.jpg","link/output.jpg","missing/output.jpg","inputs/jpeg.bin"})
  call("png_to_jpeg",{{"source_path","inputs/png.bin"},{"output_path",output}});
 call("png_to_jpeg",{{"source_path","inputs/png.bin"},{"output_path","inputs/png.bin"}},"rpcError");
 call("png_to_jpeg",{{"source_path","inputs/alpha.png"},{"output_path","outputs/unconsented.jpg"}});
 call("png_to_jpeg",{{"source_path","inputs/png.bin"},{"output_path","outputs/invalid.jpg"},{"jpeg_quality",0}},"rpcError");
 call("jpeg_to_png",{{"source_path","inputs/jpeg.bin"},{"output_path","outputs/invalid.png"},{"jpeg_quality",50}},"rpcError");
 call("prepare_upload",Json::object(),"rpcError");
 send({{"method","tools/call"},{"params",{{"name","png_to_jpeg"},{"arguments",{{"source_path","inputs/png.bin"},{"output_path","outputs/paid.jpg"}}},{"_meta",{{"x402/payment",Json::object()}}}}}},"rpcError");
 C::ImageOptions options{I::Format::png,I::Format::jpeg,95,I::AlphaPolicy::flatten_white};
 const auto flattened=C::ConvertImage(transparent.bytes,options);check(bool(flattened),"flattening reference");write(root/"expected/flatten.jpg",flattened.bytes);
 send({{"method","tools/call"},{"params",{{"name","png_to_jpeg"},{"arguments",{{"source_path","inputs/alpha.png"},{"output_path","outputs/flatten.jpg"},{"alpha_policy","flatten_white"}}}}}},"converted",
  {{"output","outputs/flatten.jpg"},{"expected","flatten.jpg"},{"width",2},{"height",2},{"sha256",digest(flattened.bytes)},{"flattened",true}});
 requests<<"{bad json}\n";expected.push_back({{"id",nullptr},{"kind","rpcError"},{"data",Json::object()}});
 send({{"method","ping"}},"ping");
 std::ofstream(root/"expected.json")<<expected.dump(2)<<'\n';
 check(bool(requests),"requests not written");std::cout<<"Prepared "<<expected.size()<<" real MCP responses\n";
}
void verify(const fs::path& root){
 std::ifstream manifest(root/"expected.json");const auto expected=Json::parse(manifest);std::ifstream actual(root/"actual.jsonl");
 std::string line;std::size_t index=0,converted=0;
 while(std::getline(actual,line)){
  check(index<expected.size(),"extra MCP response (notification?)");const auto response=Json::parse(line);const auto& test=expected.at(index++);
  check(response.at("jsonrpc")=="2.0"&&response.at("id")==test.at("id"),"JSON-RPC identity changed");
  const auto kind=test.at("kind").get<std::string>();
  if(kind=="rpcError"){check(response.contains("error"),"expected protocol rejection");continue;}
  const auto& result=response.at("result");
  if(kind=="toolError"){check(result.value("isError",false),"expected operation rejection");continue;}
  check(!result.value("isError",false),"unexpected tool error");
  if(kind=="initialize")check(result.at("serverInfo").at("name")=="Arken Convert","server identity");
  if(kind=="catalogue"){
   std::set<std::string> names;for(const auto& tool:result.at("tools")){
    names.insert(tool.at("name"));check(tool.at("inputSchema").at("properties").contains("source_path"),"path schema absent");
    check(!tool.at("inputSchema").at("properties").contains("file_token"),"hosted capability leaked");
    check(tool.at("annotations").at("idempotentHint")==false,"unsafe idempotence hint");
   }
   check(names==std::set<std::string>{"convert_image","png_to_jpeg","jpeg_to_png","bmp_to_png","bmp_to_jpeg","tga_to_png","dds_to_png"},"seven tools required");
  }
  if(kind=="converted"){
   const auto& data=test.at("data");const auto& value=result.at("structuredContent");const auto output=data.at("output").get<std::string>();
   const auto result_bytes=bytes(root/"files"/output);check(result_bytes==bytes(root/"expected"/data.at("expected").get<std::string>()),"actual output differs from codec");
   check(value.at("output_path")==output&&value.at("width")==data.at("width")&&value.at("height")==data.at("height")
    &&value.at("sha256")==data.at("sha256")&&value.at("sha256")==digest(result_bytes)
    &&value.at("output_bytes")==result_bytes.size()&&value.at("durable")==true
    &&value.at("alpha_flattened")==data.value("flattened",false),"conversion metadata mismatch");++converted;
  }
 }
 check(index==expected.size()&&converted==8,"responses/conversions incomplete");
 check(bytes(root/"files/inputs/png.bin")==bytes(root/"outside/private.png"),"source or outside data overwritten");
 check(!fs::exists(root/"outside/output.jpg"),"output escaped root");
 std::size_t outputs=0;for(const auto& entry:fs::directory_iterator(root/"files/outputs")){(void)entry;++outputs;}
 check(outputs==8,"failed request created an output");
 for(const auto& entry:fs::recursive_directory_iterator(root/"files"))check(!entry.path().filename().string().starts_with(".arken-write-"),"temporary stage leaked");
 std::cout<<"PASS "<<index<<" real stdio responses; all7tools + explicit alpha conversion, exact files/hashes, no overwrite or escape\n";
}
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-convert-command-tests-r6262\n";return 0;}
 try{check(argc==3,"expected --prepare or --verify and fresh proof path");const std::string_view mode=argv[1];
  if(mode=="--prepare")prepare(argv[2]);else if(mode=="--verify")verify(argv[2]);else throw std::runtime_error("unknown proof operation");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
