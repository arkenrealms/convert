#include "arken_file_conversion_commands_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
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
namespace A=arken::audio::v1;
namespace D=arken::document::v1;
using Json=mcp::Json;
namespace {
// Each family is admitted by its own conversion-owner profile with its own limits; the
// descriptions below state what that profile actually does, including what it leaves out.
struct Pair {std::uint32_t id;I::Format input,output;const char* description;};
constexpr Pair pairs[]{
 {3,I::Format::png,I::Format::jpeg,"Convert PNG to JPEG. sRGB, iCCP and cHRM/gAMA colour information is applied; 16-bit PNG is rounded to 8 bits per channel. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {4,I::Format::jpeg,I::Format::png,"Convert JPEG to PNG. Exif orientation and an embedded ICC profile are applied; Adobe CMYK/YCCK is converted to RGB. Arithmetic-coded, lossless and 12-bit JPEG are rejected. Detail already lost to JPEG compression is not restored. Uses relative source_path and a new output_path."},
 {5,I::Format::bmp,I::Format::png,"Convert BMP to PNG. Reads OS/2 and Windows V1-V5 headers, 1-32 bit pixels, bitfields and RLE4/RLE8; calibrated and embedded colour spaces are applied, linked profiles are rejected. Alpha is preserved. Uses relative source_path and a new output_path."},
 {6,I::Format::bmp,I::Format::jpeg,"Convert BMP to JPEG. Reads OS/2 and Windows V1-V5 headers, 1-32 bit pixels, bitfields and RLE4/RLE8. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {7,I::Format::tga,I::Format::png,"Convert TGA to PNG. Preserves admitted transparency. Uses relative source_path and a new output_path."},
 {8,I::Format::dds,I::Format::png,"Convert a supported DDS texture to PNG for inspection and editing. Unsupported DDS profiles are rejected. Uses relative source_path and a new output_path."},
 {9,I::Format::webp,I::Format::png,"Convert WebP to PNG. Accepts lossy and lossless stills. An animation converts its first frame only; source_frames reports the frame count. An embedded ICC profile is applied; Exif and XMP are ignored. Transparency is preserved. Uses relative source_path and a new output_path."},
 {10,I::Format::webp,I::Format::jpeg,"Convert WebP to JPEG. An animation converts its first frame only; source_frames reports the frame count. An embedded ICC profile is applied; Exif and XMP are ignored. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {11,I::Format::png,I::Format::webp,"Convert PNG to WebP. Lossless by default: exact RGBA, including the colour of transparent pixels. webp_lossless=false writes lossy VP8 at webp_quality (1-100, default 90) with exact alpha. Metadata is not written. Uses relative source_path and a new output_path."},
 {12,I::Format::gif,I::Format::png,"Convert the first frame of a GIF to PNG. An animated GIF yields one frame, never a sequence; source_frames reports how many frames the file holds. Uses relative source_path and a new output_path."},
 {13,I::Format::tiff,I::Format::png,"Convert TIFF to PNG using the first page; source_frames reports the page count. Classic and BigTIFF; strips or tiles; chunky or planar; none, LZW, Deflate, PackBits or JPEG compression; orientation applied. Gray, palette, RGB, CMYK and YCbCr with ICC profiles applied; 16-bit samples are rounded to 8 bits. CCITT, old-style JPEG, floating-point and Lab TIFF are rejected. Uses relative source_path and a new output_path."},
 {14,I::Format::png,I::Format::tiff,"Convert PNG to an RGBA TIFF. Uncompressed by default; tiff_deflate=true writes Deflate with a horizontal predictor. Transparency is preserved. Uses relative source_path and a new output_path."},
 {15,I::Format::ico,I::Format::png,"Convert a Windows ICO or CUR to PNG using its largest rendition (largest area, then deepest colour); source_frames reports the rendition count. AND masks become transparency; cursor hotspots are ignored. Uses relative source_path and a new output_path."},
 {16,I::Format::png,I::Format::ico,"Convert a PNG of at most 256x256 pixels to a single-image ICO. Larger images are rejected, never resized. Uses relative source_path and a new output_path."}
};
constexpr const char* imageDescription=
 "Convert one image. Inputs: PNG, JPEG, BMP, TGA, GIF, ICO/CUR, TIFF, WebP and supported DDS. Outputs: PNG, JPEG, BMP, TGA, ICO, TIFF and WebP. "
 "One image per call: the first GIF or WebP frame, the first TIFF page or the largest ICO/CUR rendition; source_frames reports how many the source holds. "
 "Embedded ICC profiles are applied; Exif and XMP are not copied. 16-bit PNG and TIFF are rounded to 8 bits; source_bits_per_channel reports 16. "
 "JPEG output: jpeg_quality 1-100 (default 95); transparency requires alpha_policy=flatten_white. "
 "WebP output: lossless by default; webp_lossless=false with webp_quality 1-100 (default 90) for lossy. TIFF output: uncompressed, or tiff_deflate=true. "
 "Options for another output are rejected. Limits: 8 MiB input and output, 1,048,576 pixels, 4096 per dimension, 256 per dimension for ICO output. Existing files are never overwritten.";

struct AudioPair {std::uint32_t id;A::Container input,output;const char* description;};
constexpr AudioPair audioPairs[]{
 {18,A::Container::wav,A::Container::flac,"Convert WAV to FLAC losslessly. Integer PCM samples (4-32 bit), sample rate, channel order and any channel mask are preserved exactly. Float, A-law, mu-law, ADPCM and RF64/Wave64 WAV are rejected, never approximated. LIST and other extra chunks are not carried; skipped_metadata counts them. FLAC level 5, verified while encoding, deterministic. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {19,A::Container::flac,A::Container::wav,"Convert FLAC to WAV losslessly. Integer samples, sample rate, channel order and any WAVEFORMATEXTENSIBLE_CHANNEL_MASK are preserved exactly; frame CRCs and the stream MD5 are checked. Tags, pictures, cue sheets and seek tables are not carried; skipped_metadata counts them. Ogg FLAC is rejected. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."}
};
constexpr const char* audioDescription=
 "Convert audio between WAV and FLAC (input_format and output_format: wav or flac). Lossless: integer PCM samples, sample rate, channel count, channel order and any channel mask are preserved exactly. "
 "Admits 4-32 bit integer PCM, 1-8 channels, up to 384 kHz. Float, A-law, mu-law and ADPCM WAV, RF64/Wave64 and Ogg FLAC are rejected, never approximated. "
 "Tags, pictures, cue sheets and LIST chunks are not carried; skipped_metadata counts them and comments_dropped reports dropped FLAC comments. "
 "Limits: 64 MiB input and output, 33,554,432 samples (frames x channels). Existing files are never overwritten.";

struct DocumentPair {std::uint32_t id;D::SourceFormat input;D::OutputFormat output;const char* description;};
constexpr DocumentPair documentPairs[]{
 {21,D::SourceFormat::docx,D::OutputFormat::text,"Extract the main story of a Word .docx to UTF-8 plain text with LF line ends: paragraphs, headings, lists, tab-separated table rows and link URLs in parentheses. Headers, footers, footnotes, endnotes, comments, text boxes and images are excluded; tracked changes appear accepted. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {22,D::SourceFormat::docx,D::OutputFormat::markdown,"Extract the main story of a Word .docx to Markdown: ATX headings, nested lists, pipe tables when rectangular, bold and italic from direct formatting, links for http, https and mailto. Literal Markdown characters are escaped and no HTML is emitted. Headers, footers, notes, comments, text boxes and images are excluded. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {23,D::SourceFormat::rtf,D::OutputFormat::text,"Extract RTF text to UTF-8 plain text with LF line ends. Unicode escapes and code pages 1250-1254 and 1257 are decoded; text in other code pages is rejected. Headers, footers, footnotes, comments, pictures and objects are dropped; lists become bullets; nested tables are flattened. Uses relative source_path and a new output_path."},
 {24,D::SourceFormat::rtf,D::OutputFormat::markdown,"Extract RTF text to Markdown: headings, bulleted lists, pipe tables when rectangular, bold, italic and HYPERLINK fields. Literal Markdown characters are escaped and no HTML is emitted. Headers, footers, footnotes, comments, pictures and objects are dropped. Uses relative source_path and a new output_path."}
};
constexpr const char* documentDescription=
 "Extract text from DOCX or RTF (input_format: docx or rtf) to UTF-8 plain text or Markdown (output_format: txt or markdown). "
 "Main story only: paragraphs, headings, lists, tables and hyperlinks. Headers, footers, footnotes, endnotes, comments, text boxes and images are excluded; "
 "tracked changes appear accepted; fields keep their stored result. Encrypted, macro-enabled and malformed files are rejected. "
 "The result reports paragraph, heading, list item, table and table row counts. "
 "Limits: 32 MiB input, 16 MiB per expanded DOCX part, 32 MiB output. Existing files are never overwritten.";

std::vector<mcp::ToolPresentation> presentations() {
 std::vector<mcp::ToolPresentation> result{{2,false,false,false,imageDescription}};
 for(const auto& p:pairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({17,false,false,false,audioDescription});
 for(const auto& p:audioPairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({20,false,false,false,documentDescription});
 for(const auto& p:documentPairs)result.push_back({p.id,false,false,false,p.description});
 return result;
}
const mcp::Server& server() {
 static const auto value=[] {
  mcp::ServerDescription d{"Arken Convert","r6262",
   "Convert images, WAV/FLAC audio and DOCX/RTF documents inside the launch-selected folder. Paths are relative; parents must exist. "
   "Output files are created exclusively, never overwritten; sources are never modified. Limits differ by family: 8 MiB images, 64 MiB audio, 32 MiB documents. "
   "Results state what a conversion did not carry: source_frames for multi-frame images, skipped_metadata for audio. "
   "Discovery is free of filesystem effects. No upload, account, network or payment is required.",
   S::commandCatalog,presentations()};
  d.toolNameStyle=mcp::ToolNameStyle::familyLocal;return mcp::Server(std::move(d));
 }();return value;
}
template<class Request> void paths(const Request& v) {
 J::require(!v.source_path.value.empty()&&v.source_path.value.size()<=4096
  &&!v.output_path.value.empty()&&v.output_path.value.size()<=4096,"path bounds");
 J::require(v.source_path!=v.output_path,"output must be a new path");
}
struct ImageInput {std::string source,output;C::ImageOptions options;};
template<class Request> ImageInput imageInput(const Request& v,I::Format from,I::Format to) {
 paths(v);
 C::ImageOptions options{from,to};
 if constexpr(requires {v.jpeg_quality;v.alpha_policy;}) {
  J::require(to==I::Format::jpeg||(!v.jpeg_quality&&!v.alpha_policy),"JPEG options require JPEG output");
  options.jpegQuality=v.jpeg_quality?v.jpeg_quality->value:95u;
  options.alphaPolicy=v.alpha_policy?static_cast<I::AlphaPolicy>(static_cast<std::uint32_t>(*v.alpha_policy)-1u):I::AlphaPolicy::preserve;
 }
 if constexpr(requires {v.webp_lossless;v.webp_quality;v.tiff_deflate;}) {
  // Presence matters: an option for another output, or a quality without lossy mode,
  // is rejected rather than ignored.
  J::require(to==I::Format::webp||(!v.webp_lossless&&!v.webp_quality),"WebP options require WebP output");
  J::require(!v.webp_quality||(v.webp_lossless&&!v.webp_lossless->value),"webp_quality requires webp_lossless=false");
  J::require(to==I::Format::tiff||!v.tiff_deflate,"tiff_deflate requires TIFF output");
  options.webpLossless=v.webp_lossless?v.webp_lossless->value:true;
  options.webpQuality=v.webp_quality?v.webp_quality->value:90u;
  options.tiffDeflate=v.tiff_deflate?v.tiff_deflate->value:false;
 }
 return {v.source_path.value,v.output_path.value,options};
}
std::string media(I::Format format) {
 switch(format){case I::Format::png:return "image/png";case I::Format::jpeg:return "image/jpeg";
  case I::Format::bmp:return "image/bmp";case I::Format::tga:return "image/x-tga";
  case I::Format::ico:return "image/vnd.microsoft.icon";case I::Format::tiff:return "image/tiff";
  case I::Format::webp:return "image/webp";default:throw std::invalid_argument("output format");}
}
Json error(const Json& id,std::string_view code,std::string_view message) {
 return mcp::toolReply(id,{{"code",code},{"message",message}},true);
}
// The codec owner names the exact rejection (animation, metadata, dimensions, matte) so a
// client can act on it without guessing which limit the shared profile message refers to.
std::string conversionMessage(const C::ImageResult& converted) {
 std::string message{C::ErrorMessage(converted.error)};
 if(converted.error==C::Error::decode_failed&&converted.decodeError!=I::DecodeError::none)
  message.append(": ").append(I::ErrorMessage(converted.decodeError));
 if(converted.error==C::Error::encode_failed&&converted.encodeError!=I::EncodeError::none)
  message.append(": ").append(I::EncodeErrorMessage(converted.encodeError));
 return message;
}
std::string conversionMessage(const C::AudioResult& converted) {
 std::string message{C::AudioErrorMessage(converted.error)};
 if(converted.codecError!=A::Error::none)message.append(": ").append(A::ErrorMessage(converted.codecError));
 return message;
}
std::string conversionMessage(const C::DocumentResult& converted) {
 std::string message{C::DocumentErrorMessage(converted.error)};
 if(converted.documentError!=D::Error::none)message.append(": ").append(D::ErrorMessage(converted.documentError));
 return message;
}
std::string digest(std::span<const std::byte> bytes) {
 const auto value=arken::digest::v1::sha256(bytes);std::array<char,71> hash{};
 J::require(value&&arken::digest::v1::format_sha256(*value,hash),"output digest");
 return {hash.data(),hash.size()};
}
}
struct Session::Impl {
 std::unique_ptr<arken::files::workspace::v1::NativeFiles> files;
 // Writes the output exclusively and replies with `result`; every family shares this.
 template<class Result> Json publish(const Json& id,const std::string& output,std::span<const std::byte> bytes,
                                     Result result,bool& published) {
  result.output_path.value=output;result.output_bytes.value=static_cast<std::uint32_t>(bytes.size());
  result.sha256.value=digest(bytes);
  const auto written=files->create(output,bytes);published=written.published;result.durable.value=written.durable;
  if(!written) {
   if(written.published){auto detail=J::write(result);detail["code"]="outputPublished";
    detail["message"]="Output was published but finalization failed. Inspect this path and digest before retrying.";
    detail["published"]=true;return mcp::toolReply(id,detail,true);}
   return error(id,written.error,"Output was not created. Choose a new relative path with an existing parent; existing files are never overwritten.");
  }
  return mcp::toolReply(id,J::write(result),false);
 }
 Json image(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  ImageInput value;
  if(command==2){const auto v=J::read<S::conversion_request>(arguments);
   value=imageInput(v,static_cast<I::Format>(v.input_format),static_cast<I::Format>(v.output_format));}
  else for(const auto& p:pairs)if(p.id==command) {
   const bool options=p.output==I::Format::jpeg||p.output==I::Format::webp||p.output==I::Format::tiff;
   value=options?imageInput(J::read<S::pair_request>(arguments),p.input,p.output)
    :imageInput(J::read<S::file_request>(arguments),p.input,p.output);
  }
  const auto source=files->read(value.source,C::maximumSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 8 MiB.");
  const auto converted=C::ConvertStandaloneImage(source.bytes,value.options);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::conversion_result result;result.media_type.value=media(value.options.output);
  result.width.value=converted.width;result.height.value=converted.height;
  result.alpha_flattened.value=converted.alphaFlattened;
  result.source_frames.value=converted.sourceFrames;result.source_bits_per_channel.value=converted.sourceBitsPerChannel;
  return publish(id,value.output,converted.bytes,std::move(result),published);
 }
 Json audio(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;A::Container from=A::Container::wav,to=A::Container::flac;
  if(command==17){const auto v=J::read<S::audio_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   from=static_cast<A::Container>(v.input_format);to=static_cast<A::Container>(v.output_format);}
  else for(const auto& p:audioPairs)if(p.id==command){const auto v=J::read<S::file_request>(arguments);paths(v);
   sourcePath=v.source_path.value;outputPath=v.output_path.value;from=p.input;to=p.output;}
  const auto source=files->read(sourcePath,C::maximumAudioSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 64 MiB.");
  const auto converted=C::ConvertStandaloneAudio(from,to,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::audio_result result;result.media_type.value=to==A::Container::flac?"audio/flac":"audio/wav";
  result.sample_rate.value=converted.sampleRate;result.channels.value=converted.channels;
  result.bits_per_sample.value=converted.bitsPerSample;
  // The profile's sample budget (2^25 frames x channels) keeps frames within u32.
  J::require(converted.frames<=0xffffffffull,"frame count");result.frames.value=static_cast<std::uint32_t>(converted.frames);
  result.skipped_metadata.value=converted.skippedMetadata;result.comments_dropped.value=converted.ignoredComments;
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
 Json document(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;D::SourceFormat from=D::SourceFormat::docx;D::OutputFormat to=D::OutputFormat::text;
  if(command==20){const auto v=J::read<S::document_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   from=static_cast<D::SourceFormat>(v.input_format);to=static_cast<D::OutputFormat>(v.output_format);}
  else for(const auto& p:documentPairs)if(p.id==command){const auto v=J::read<S::file_request>(arguments);paths(v);
   sourcePath=v.source_path.value;outputPath=v.output_path.value;from=p.input;to=p.output;}
  const auto source=files->read(sourcePath,C::maximumDocumentSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 32 MiB.");
  const auto converted=C::ConvertStandaloneDocument(from,to,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::document_result result;result.media_type.value=std::string(D::MediaType(to));
  result.paragraphs.value=converted.paragraphs;result.headings.value=converted.headings;
  result.list_items.value=converted.listItems;result.tables.value=converted.tables;result.table_rows.value=converted.tableRows;
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
};
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
  const auto command=plan.commandId;
  if(command>=2&&command<=16)return impl_->image(id,command,plan.arguments,published);
  if(command>=17&&command<=19)return impl_->audio(id,command,plan.arguments,published);
  if(command>=20&&command<=24)return impl_->document(id,command,plan.arguments,published);
  throw std::invalid_argument("Unknown conversion");
 }catch(const std::exception&){
  return mcp::failure(id,published?-32603:-32602,published
   ?"Output was published; inspect the requested path before retrying."
   :"Invalid conversion request");
 }
}
}
