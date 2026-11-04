#include "arken_file_conversion_v1/api.hpp"
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_image_codecs_v1/test_support/texture_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/gif_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/ico_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/tiff_fixtures.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
namespace fs=std::filesystem;
namespace C=arken::file_conversion::v1;
namespace I=arken::image::v1;
namespace A=arken::audio::v1;
namespace D=arken::document::v1;
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
std::string imageMedia(I::Format format){
 switch(format){case I::Format::png:return "image/png";case I::Format::jpeg:return "image/jpeg";case I::Format::bmp:return "image/bmp";
  case I::Format::tga:return "image/x-tga";case I::Format::ico:return "image/vnd.microsoft.icon";case I::Format::tiff:return "image/tiff";
  case I::Format::webp:return "image/webp";default:throw std::runtime_error("media");}
}
std::string text(const std::vector<std::byte>& data){return {reinterpret_cast<const char*>(data.data()),data.size()};}
void prepare(const fs::path& root){
 check(!fs::exists(root),"fresh proof root required");fs::create_directories(root/"files/inputs");fs::create_directory(root/"files/outputs");
 fs::create_directory(root/"expected");fs::create_directory(root/"outside");
 const std::array<std::uint8_t,16> pixels{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
 std::map<std::string,std::vector<std::byte>> sources;
 for(auto [name,format]:std::initializer_list<std::pair<std::string,I::Format>>{{"png",I::Format::png},{"jpeg",I::Format::jpeg},{"bmp",I::Format::bmp},{"tga",I::Format::tga},{"webp",I::Format::webp}}){
  auto encoded=I::EncodeImage(format,2,2,pixels);check(bool(encoded),"source fixture encoding");sources[name]=std::move(encoded.bytes);
 }
 sources["dds"]=texture_fixture::Dds(8,4,1,1);
 // Independently authored TIFF, ICO and GIF fixtures, not this writer's own output.
 sources["tiff"]=tiff_fixture::Make().bytes;
 sources["ico"]=ico_fixture::Container({ico_fixture::Dib(32,true,9,7).entry,ico_fixture::Png(32,16)});
 sources["gif"]=raster_fixture::AnimatedGif2x1();
 for(const auto& [name,data]:sources)write(root/"files/inputs"/(name+".bin"),data);
 auto alpha=pixels;alpha[3]=0;const auto transparent=I::EncodeImage(I::Format::png,2,2,alpha);check(bool(transparent),"alpha fixture");
 write(root/"files/inputs/alpha.png",transparent.bytes);write(root/"outside/private.png",sources.at("png"));
 fs::create_symlink(root/"outside/private.png",root/"files/escape.png");fs::create_directory_symlink(root/"outside",root/"files/link");
 std::ofstream requests(root/"requests.jsonl");Json expected=Json::array();std::uint64_t next=9007199254740993ULL;
 std::size_t conversions=0;
 const auto send=[&](Json request,std::string kind,Json data=Json::object()) {
  request["jsonrpc"]="2.0";request["id"]=next++;requests<<request.dump()<<'\n';
  if(kind=="converted")++conversions;
  expected.push_back({{"id",request["id"]},{"kind",kind},{"data",std::move(data)}});
 };
 const auto converted=[&](const char* name,Json args,const std::string& output,const std::vector<std::byte>& reference,Json fields){
  const std::string expectedName=std::string(name)+"-"+std::to_string(conversions)+".bin";write(root/"expected"/expectedName,reference);
  args["output_path"]=output;
  send({{"method","tools/call"},{"params",{{"name",name},{"arguments",std::move(args)}}}},"converted",
   {{"output",output},{"expected",expectedName},{"sha256",digest(reference)},{"fields",std::move(fields)}});
 };
 send({{"method","initialize"},{"params",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},{"clientInfo",{{"name","convert-native-proof"},{"version","1"}}}}}},"initialize");
 requests<<"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
 send({{"method","tools/list"}},"catalogue");
 // Images: every image tool once with its default options.
 struct Case{const char* name;const char* from;I::Format input,output;};
 const Case cases[]={{"convert_image","png",I::Format::png,I::Format::tga},
  {"png_to_jpeg","png",I::Format::png,I::Format::jpeg},{"jpeg_to_png","jpeg",I::Format::jpeg,I::Format::png},
  {"bmp_to_png","bmp",I::Format::bmp,I::Format::png},{"bmp_to_jpeg","bmp",I::Format::bmp,I::Format::jpeg},
  {"tga_to_png","tga",I::Format::tga,I::Format::png},{"dds_to_png","dds",I::Format::dds,I::Format::png},
  {"webp_to_png","webp",I::Format::webp,I::Format::png},{"webp_to_jpeg","webp",I::Format::webp,I::Format::jpeg},
  {"png_to_webp","png",I::Format::png,I::Format::webp},{"gif_to_png","gif",I::Format::gif,I::Format::png},
  {"tiff_to_png","tiff",I::Format::tiff,I::Format::png},{"png_to_tiff","png",I::Format::png,I::Format::tiff},
  {"ico_to_png","ico",I::Format::ico,I::Format::png},{"png_to_ico","png",I::Format::png,I::Format::ico}};
 for(const auto& test:cases){
  const auto result=C::ConvertStandaloneImage(sources.at(test.from),{test.input,test.output});check(bool(result),"reference conversion");
  Json args{{"source_path","inputs/"+std::string(test.from)+".bin"}};
  if(std::string_view(test.name)=="convert_image"){args["input_format"]="png";args["output_format"]="tga";}
  converted(test.name,args,"outputs/"+std::string(test.name)+".bin",result.bytes,
   {{"media_type",imageMedia(test.output)},{"width",result.width},{"height",result.height},{"alpha_flattened",false},
    {"source_frames",result.sourceFrames},{"source_bits_per_channel",result.sourceBitsPerChannel}});
 }
 check(C::ConvertStandaloneImage(sources.at("gif"),{I::Format::gif,I::Format::png}).sourceFrames==2
  &&C::ConvertStandaloneImage(sources.at("ico"),{I::Format::ico,I::Format::png}).sourceFrames==2,"multi-image fixtures");
 // Codec options: lossy WebP at an explicit quality and Deflate TIFF.
 const auto optioned=[&](const char* name,const char* output,C::ImageOptions options,Json args){
  const auto result=C::ConvertStandaloneImage(sources.at("png"),options);check(bool(result),"optioned reference");
  args["source_path"]="inputs/png.bin";
  converted(name,args,std::string("outputs/")+output,result.bytes,
   {{"media_type",imageMedia(options.output)},{"width",2},{"height",2},{"source_frames",1}});
 };
 C::ImageOptions lossy{I::Format::png,I::Format::webp};lossy.webpLossless=false;lossy.webpQuality=60;
 optioned("png_to_webp","lossy.webp",lossy,{{"webp_lossless",false},{"webp_quality",60}});
 optioned("convert_image","generic-lossy.webp",lossy,{{"input_format","png"},{"output_format","webp"},{"webp_lossless",false},{"webp_quality",60}});
 C::ImageOptions deflate{I::Format::png,I::Format::tiff};deflate.tiffDeflate=true;
 optioned("png_to_tiff","deflate.tif",deflate,{{"tiff_deflate",true}});
 check(C::ConvertStandaloneImage(sources.at("png"),lossy).bytes!=C::ConvertStandaloneImage(sources.at("png"),{I::Format::png,I::Format::webp}).bytes,
  "lossy and lossless WebP references are identical");
 // Audio: 16-bit stereo WAV with extrema, and 24-bit 5.1 FLAC with a declared channel mask.
 A::Audio stereo{44100,2,16,std::nullopt,2048,{}};
 for(std::uint32_t i=0;i<2048*2;++i)stereo.samples.push_back(std::int32_t((i*7919u)%65536u)-32768);
 stereo.samples[10]=32767;stereo.samples[11]=-32768;stereo.samples[0]=0;
 A::Audio surround{48000,6,24,0x3fu,480,{}};
 for(std::uint32_t i=0;i<480*6;++i)surround.samples.push_back(std::int32_t((i*104729u)%16777216u)-8388608);
 const auto wav=A::EncodeWav(stereo);const auto flac=A::EncodeFlac(surround);check(wav&&flac,"audio fixtures");
 write(root/"files/inputs/stereo.wav",wav.bytes);write(root/"files/inputs/surround.flac",flac.bytes);
 struct AudioCase{const char* name;const char* source;A::Container input,output;const char* outputPath;Json args;};
 const AudioCase audioCases[]={
  {"wav_to_flac","inputs/stereo.wav",A::Container::wav,A::Container::flac,"outputs/stereo.flac",Json::object()},
  {"flac_to_wav","inputs/surround.flac",A::Container::flac,A::Container::wav,"outputs/surround.wav",Json::object()},
  {"convert_audio","inputs/surround.flac",A::Container::flac,A::Container::wav,"outputs/generic.wav",{{"input_format","flac"},{"output_format","wav"}}}};
 for(const auto& test:audioCases){
  const auto result=C::ConvertStandaloneAudio(test.input,test.output,test.input==A::Container::wav?wav.bytes:flac.bytes);
  check(bool(result),"audio reference");
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",test.output==A::Container::flac?"audio/flac":"audio/wav"},{"sample_rate",result.sampleRate},
    {"channels",result.channels},{"bits_per_sample",result.bitsPerSample},{"frames",result.frames},
    {"skipped_metadata",result.skippedMetadata},{"comments_dropped",result.ignoredComments}});
 }
 const auto lossless=A::DecodeFlac(C::ConvertStandaloneAudio(A::Container::wav,A::Container::flac,wav.bytes).bytes);
 check(lossless&&lossless.audio.samples==stereo.samples,"reference FLAC is not lossless");
 const auto restored=A::DecodeWav(C::ConvertStandaloneAudio(A::Container::flac,A::Container::wav,flac.bytes).bytes);
 check(restored&&restored.audio.samples==surround.samples&&restored.audio.channelMask==surround.channelMask,"reference WAV lost samples or mask");
 // Documents: a stored-ZIP DOCX and an RTF string with known expected text.
 const auto docx=docx_fixture::Bytes(docx_fixture::Docx(
  R"(<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Plan</w:t></w:r></w:p>)"
  R"(<w:p><w:r><w:t xml:space="preserve">Ship </w:t></w:r><w:r><w:rPr><w:i/></w:rPr><w:t>today</w:t></w:r></w:p>)"
  R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>one_two</w:t></w:r></w:p>)"));
 const auto rtf=docx_fixture::Bytes(R"({\rtf1\ansi{\fonttbl{\f0 Arial;}}{\footnote Hidden\par}\pard\outlinelevel0 Title\par\pard Body {\b strong}\par})");
 write(root/"files/inputs/plan.docx",docx);write(root/"files/inputs/note.rtf",rtf);
 struct DocumentCase{const char* name;const char* source;D::SourceFormat input;D::OutputFormat output;const char* outputPath;std::string_view text;Json args;};
 const DocumentCase documentCases[]={
  {"docx_to_txt","inputs/plan.docx",D::SourceFormat::docx,D::OutputFormat::text,"outputs/plan.txt","Plan\n\nShip today\n\n- one_two\n",Json::object()},
  {"docx_to_markdown","inputs/plan.docx",D::SourceFormat::docx,D::OutputFormat::markdown,"outputs/plan.md","# Plan\n\nShip *today*\n\n- one\\_two\n",Json::object()},
  {"rtf_to_txt","inputs/note.rtf",D::SourceFormat::rtf,D::OutputFormat::text,"outputs/note.txt","Title\n\nBody strong\n",Json::object()},
  {"rtf_to_markdown","inputs/note.rtf",D::SourceFormat::rtf,D::OutputFormat::markdown,"outputs/note.md","# Title\n\nBody **strong**\n",Json::object()},
  {"convert_document","inputs/plan.docx",D::SourceFormat::docx,D::OutputFormat::markdown,"outputs/generic.md","# Plan\n\nShip *today*\n\n- one\\_two\n",{{"input_format","docx"},{"output_format","markdown"}}}};
 for(const auto& test:documentCases){
  const auto result=C::ConvertStandaloneDocument(test.input,test.output,test.input==D::SourceFormat::docx?docx:rtf);
  check(bool(result)&&text(result.bytes)==test.text,std::string(test.name)+": unexpected extracted text: "+text(result.bytes));
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",std::string(D::MediaType(test.output))},{"paragraphs",result.paragraphs},{"headings",result.headings},
    {"list_items",result.listItems},{"tables",result.tables},{"table_rows",result.tableRows}});
 }
 const auto call=[&](const char* name,Json args,std::string kind="toolError",std::string reason={}){
  send({{"method","tools/call"},{"params",{{"name",name},{"arguments",std::move(args)}}}},kind,
   reason.empty()?Json::object():Json{{"reason",reason}});
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
 // Option misuse is a protocol rejection, never silently ignored.
 call("png_to_jpeg",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt1.jpg"},{"webp_quality",80}},"rpcError");
 call("png_to_webp",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt2.webp"},{"webp_quality",80}},"rpcError");
 call("png_to_webp",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt3.webp"},{"webp_lossless",false},{"webp_quality",0}},"rpcError");
 call("png_to_webp",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt4.webp"},{"tiff_deflate",true}},"rpcError");
 call("png_to_tiff",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt5.tif"},{"webp_lossless",true}},"rpcError");
 call("convert_image",{{"source_path","inputs/png.bin"},{"output_path","outputs/opt6.png"},{"input_format","png"},{"output_format","png"},{"tiff_deflate",true}},"rpcError");
 call("convert_audio",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/x.mp3"},{"input_format","wav"},{"output_format","mp3"}},"rpcError");
 call("convert_document",{{"source_path","inputs/plan.docx"},{"output_path","outputs/x.html"},{"input_format","docx"},{"output_format","html"}},"rpcError");
 call("wav_to_flac",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/x.flac"},{"jpeg_quality",90}},"rpcError");
 // Rejections the codec owner names: an icon source above 256 pixels and an animated WebP without frames.
 const auto wide=I::EncodeImage(I::Format::png,257,1,std::vector<std::uint8_t>(257*4,255));check(bool(wide),"wide fixture");
 write(root/"files/inputs/wide.png",wide.bytes);
 call("png_to_ico",{{"source_path","inputs/wide.png"},{"output_path","outputs/wide.ico"}},"toolError",std::string(I::EncodeErrorMessage(I::EncodeError::invalid_dimensions)));
 const std::vector<std::byte> animatedWebp{std::byte{'R'},std::byte{'I'},std::byte{'F'},std::byte{'F'},
  std::byte{22},std::byte{0},std::byte{0},std::byte{0},std::byte{'W'},std::byte{'E'},std::byte{'B'},std::byte{'P'},
  std::byte{'V'},std::byte{'P'},std::byte{'8'},std::byte{'X'},std::byte{10},std::byte{0},std::byte{0},std::byte{0},
  std::byte{0x02},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
 write(root/"files/inputs/animated.webp",animatedWebp);
 call("webp_to_png",{{"source_path","inputs/animated.webp"},{"output_path","outputs/animated.png"}},"toolError",std::string(I::ErrorMessage(I::DecodeError::unsupported_content)));
 // Float WAV, an encrypted DOCX and mislabelled inputs carry the audio/document owner's reason.
 std::vector<std::byte> floatWav(48);
 const char floatHeader[]="RIFF\x28\0\0\0WAVEfmt \x10\0\0\0\x03\0\x01\0\x40\x1f\0\0\0\x7d\0\0\x04\0\x20\0data\x04\0\0\0";
 std::memcpy(floatWav.data(),floatHeader,44);write(root/"files/inputs/float.wav",floatWav);
 check(C::ConvertStandaloneAudio(A::Container::wav,A::Container::flac,floatWav).codecError==A::Error::unsupported_float,"float fixture");
 call("wav_to_flac",{{"source_path","inputs/float.wav"},{"output_path","outputs/float.flac"}},"toolError",std::string(A::ErrorMessage(A::Error::unsupported_float)));
 write(root/"files/inputs/encrypted.docx",docx_fixture::Bytes(docx_fixture::EncryptedDocx()));
 call("docx_to_txt",{{"source_path","inputs/encrypted.docx"},{"output_path","outputs/encrypted.txt"}},"toolError",std::string(D::ErrorMessage(D::Error::docx_encrypted)));
 call("flac_to_wav",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/notflac.wav"}},"toolError",std::string(A::ErrorMessage(A::Error::wrong_magic)));
 call("rtf_to_txt",{{"source_path","inputs/plan.docx"},{"output_path","outputs/notrtf.txt"}},"toolError",std::string(D::ErrorMessage(D::Error::rtf_not_rtf)));
 // Existing outputs are never replaced, and sources outside the folder are never read, for every family.
 call("wav_to_flac",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/stereo.flac"}});
 call("docx_to_markdown",{{"source_path","inputs/plan.docx"},{"output_path","outputs/plan.md"}});
 call("docx_to_markdown",{{"source_path","../outside/private.png"},{"output_path","outputs/escape.md"}});
 call("convert_image",{{"source_path","inputs/png.bin"},{"output_path","outputs/unlisted.dds"},{"input_format","png"},{"output_format","dds"}},"rpcError");
 call("convert_image",{{"source_path","inputs/png.bin"},{"output_path","outputs/matte.webp"},{"input_format","png"},{"output_format","webp"},{"alpha_policy","flatten_white"}},"rpcError");
 send({{"method","tools/call"},{"params",{{"name","png_to_jpeg"},{"arguments",{{"source_path","inputs/png.bin"},{"output_path","outputs/paid.jpg"}}},{"_meta",{{"x402/payment",Json::object()}}}}}},"rpcError");
 C::ImageOptions options{I::Format::png,I::Format::jpeg,95,I::AlphaPolicy::flatten_white};
 const auto flattened=C::ConvertStandaloneImage(transparent.bytes,options);check(bool(flattened),"flattening reference");
 converted("png_to_jpeg",{{"source_path","inputs/alpha.png"},{"alpha_policy","flatten_white"}},"outputs/flatten.jpg",flattened.bytes,
  {{"media_type","image/jpeg"},{"width",2},{"height",2},{"alpha_flattened",true},{"source_frames",1}});
 requests<<"{bad json}\n";expected.push_back({{"id",nullptr},{"kind","rpcError"},{"data",Json::object()}});
 send({{"method","ping"}},"ping");
 std::ofstream(root/"expected.json")<<Json{{"conversions",conversions},{"responses",expected}}.dump(2)<<'\n';
 check(bool(requests),"requests not written");std::cout<<"Prepared "<<expected.size()<<" real MCP responses\n";
}
void verify(const fs::path& root){
 std::ifstream manifest(root/"expected.json");const auto plan=Json::parse(manifest);const auto& expected=plan.at("responses");
 const auto conversions=plan.at("conversions").get<std::size_t>();std::ifstream actual(root/"actual.jsonl");
 std::string line;std::size_t index=0,converted=0;
 while(std::getline(actual,line)){
  check(index<expected.size(),"extra MCP response (notification?)");const auto response=Json::parse(line);const auto& test=expected.at(index++);
  check(response.at("jsonrpc")=="2.0"&&response.at("id")==test.at("id"),"JSON-RPC identity changed");
  const auto kind=test.at("kind").get<std::string>();
  if(kind=="rpcError"){check(response.contains("error"),"expected protocol rejection at response "+std::to_string(index));continue;}
  const auto& result=response.at("result");
  if(kind=="toolError"){
   check(result.value("isError",false),"expected operation rejection at response "+std::to_string(index));
   if(test.at("data").contains("reason")){
    const auto detail=Json::parse(result.at("content").at(0).at("text").get<std::string>());
    check(detail.at("code")=="conversionFailed"&&detail.at("message").get<std::string>().find(test.at("data").at("reason").get<std::string>())!=std::string::npos,
     "rejection does not carry the codec owner's reason: "+detail.dump());
   }
   continue;
  }
  check(!result.value("isError",false),"unexpected tool error: "+result.dump());
  if(kind=="initialize")check(result.at("serverInfo").at("name")=="Arken Convert","server identity");
  if(kind=="catalogue"){
   std::set<std::string> names;for(const auto& tool:result.at("tools")){
    const std::string name=tool.at("name");names.insert(name);const auto& properties=tool.at("inputSchema").at("properties");
    check(properties.contains("source_path"),"path schema absent");
    check(!properties.contains("file_token"),"hosted capability leaked");
    check(tool.at("annotations").at("idempotentHint")==false,"unsafe idempotence hint");
    const bool imageOptions=name=="convert_image"||name=="png_to_jpeg"||name=="bmp_to_jpeg"||name=="webp_to_jpeg"||name=="png_to_webp"||name=="png_to_tiff";
    check(properties.contains("webp_quality")==imageOptions&&properties.contains("tiff_deflate")==imageOptions,name+": option schema");
    const auto dump=tool.dump();
    for(const auto* term:{"\"heif\"","\"heic\"","\"avif\"","heic","avif"})
     check(dump.find(term)==std::string::npos,"HEIF/AVIF must not be admitted: "+name);
   }
   check(names==std::set<std::string>{"convert_image","png_to_jpeg","jpeg_to_png","bmp_to_png","bmp_to_jpeg","tga_to_png","dds_to_png",
    "webp_to_png","webp_to_jpeg","png_to_webp","gif_to_png","tiff_to_png","png_to_tiff","ico_to_png","png_to_ico",
    "convert_audio","wav_to_flac","flac_to_wav","convert_document","docx_to_txt","docx_to_markdown","rtf_to_txt","rtf_to_markdown"},
    "exactly twenty-three tools required");
  }
  if(kind=="converted"){
   const auto& data=test.at("data");const auto& value=result.at("structuredContent");const auto output=data.at("output").get<std::string>();
   const auto result_bytes=bytes(root/"files"/output);
   check(result_bytes==bytes(root/"expected"/data.at("expected").get<std::string>()),"actual output differs from the conversion owner: "+output);
   check(value.at("output_path")==output&&value.at("sha256")==data.at("sha256")&&value.at("sha256")==digest(result_bytes)
    &&value.at("output_bytes")==result_bytes.size()&&value.at("durable")==true,"conversion metadata mismatch: "+output);
   for(const auto& [field,want]:data.at("fields").items())
    check(value.at(field)==want,"result field "+field+" mismatch for "+output+": "+value.dump());
   ++converted;
  }
 }
 check(index==expected.size()&&converted==conversions,"responses/conversions incomplete");
 check(bytes(root/"files/inputs/png.bin")==bytes(root/"outside/private.png"),"source or outside data overwritten");
 check(!fs::exists(root/"outside/output.jpg"),"output escaped root");
 std::size_t outputs=0;for(const auto& entry:fs::directory_iterator(root/"files/outputs")){(void)entry;++outputs;}
 check(outputs==conversions,"failed request created an output");
 for(const auto& entry:fs::recursive_directory_iterator(root/"files"))check(!entry.path().filename().string().starts_with(".arken-write-"),"temporary stage leaked");
 std::cout<<"PASS "<<index<<" real stdio responses; all 23 tools, "<<converted
  <<" conversions with exact files, hashes and reported metadata; codec-owner reasons; no overwrite or escape\n";
}
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-convert-command-tests-r6262\n";return 0;}
 try{check(argc==3,"expected --prepare or --verify and fresh proof path");const std::string_view mode=argv[1];
  if(mode=="--prepare")prepare(argv[2]);else if(mode=="--verify")verify(argv[2]);else throw std::runtime_error("unknown proof operation");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
