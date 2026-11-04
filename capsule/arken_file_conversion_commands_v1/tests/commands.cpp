#include "arken_file_conversion_v1/api.hpp"
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
#include "arken_file_conversion_v1/markup.hpp"
#include "arken_file_conversion_v1/tables.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_image_codecs_v1/test_support/texture_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/gif_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/ico_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/tiff_fixtures.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include "arken_document_extraction_v1/test_support/package_fixtures.hpp"
#include <array>
#include <cmath>
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
namespace T=arken::document::v1::tabular;
namespace M=arken::markup::v1;
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
 // Presentations, OpenDocument text and EPUB through the same document owner.
 namespace P=package_fixture;
 const auto pptx=docx_fixture::Bytes(P::Pptx({{P::PptxShape("<p:ph type=\"title\"/>",P::PptxPara("Plan"))
  +P::PptxShape("<p:ph idx=\"1\"/>",P::PptxPara("First")+P::PptxPara("Sub","<a:pPr lvl=\"1\"/>"))
  +"<p:pic><p:nvPicPr><p:cNvPr id=\"5\" name=\"p\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill/><p:spPr/></p:pic>",P::PptxPara("Say hi")}}));
 const auto odt=docx_fixture::Bytes(P::Odt("<text:h text:outline-level=\"1\">Title</text:h>"
  "<text:p>Body <text:a xlink:href=\"https://e.test/\">link</text:a><text:note text:note-class=\"footnote\"><text:note-citation>1</text:note-citation>"
  "<text:note-body><text:p>Foot</text:p></text:note-body></text:note></text:p>"));
 const auto odp=docx_fixture::Bytes(P::Odp("<draw:page draw:name=\"p1\"><draw:frame presentation:class=\"title\"><draw:text-box><text:p>Hello</text:p>"
  "</draw:text-box></draw:frame><presentation:notes><draw:frame presentation:class=\"notes\"><draw:text-box><text:p>Psst</text:p>"
  "</draw:text-box></draw:frame></presentation:notes></draw:page>"));
 const auto epub=docx_fixture::Bytes(P::Epub({"<h1>Intro</h1><blockquote><p>Q</p></blockquote><pre><code class=\"language-sh\">ls -l</code></pre>"
  "<hr/><p>End <img src=\"a.png\" alt=\"x\"/></p><script>x()</script>","<p>Second</p>"}));
 write(root/"files/inputs/deck.pptx",pptx);write(root/"files/inputs/body.odt",odt);write(root/"files/inputs/deck.odp",odp);write(root/"files/inputs/book.epub",epub);
 struct TextDocumentCase{const char* name;const char* source;C::DocumentInput input;D::OutputFormat output;bool notes;const char* outputPath;std::string_view text;Json args;};
 const TextDocumentCase textDocumentCases[]={
  {"pptx_to_txt","inputs/deck.pptx",C::DocumentInput::pptx,D::OutputFormat::text,false,"outputs/deck.txt","Slide 1\n\nPlan\n\n- First\n  - Sub\n",Json::object()},
  {"pptx_to_markdown","inputs/deck.pptx",C::DocumentInput::pptx,D::OutputFormat::markdown,true,"outputs/deck.md",
   "# Slide 1\n\n## Plan\n\n- First\n  - Sub\n\n**Notes:** Say hi\n",{{"include_notes",true}}},
  {"odt_to_txt","inputs/body.odt",C::DocumentInput::odt,D::OutputFormat::text,false,"outputs/body.txt","Title\n\nBody link (https://e.test/)\n",Json::object()},
  {"odt_to_markdown","inputs/body.odt",C::DocumentInput::odt,D::OutputFormat::markdown,false,"outputs/body.md","# Title\n\nBody [link](https://e.test/)\n",Json::object()},
  {"odp_to_txt","inputs/deck.odp",C::DocumentInput::odp,D::OutputFormat::text,true,"outputs/odp.txt","Slide 1\n\nHello\n\nNotes: Psst\n",{{"include_notes",true}}},
  {"odp_to_markdown","inputs/deck.odp",C::DocumentInput::odp,D::OutputFormat::markdown,false,"outputs/odp.md","# Slide 1\n\n## Hello\n",{{"include_notes",false}}},
  {"epub_to_txt","inputs/book.epub",C::DocumentInput::epub,D::OutputFormat::text,false,"outputs/book.txt","Intro\n\nQ\n\nls -l\n\n\nEnd\n\nSecond\n",Json::object()},
  {"epub_to_markdown","inputs/book.epub",C::DocumentInput::epub,D::OutputFormat::markdown,false,"outputs/book.md",
   "# Intro\n\n> Q\n\n```sh\nls -l\n```\n\n---\n\nEnd\n\nSecond\n",Json::object()},
  {"convert_document","inputs/deck.pptx",C::DocumentInput::pptx,D::OutputFormat::text,true,"outputs/generic-deck.txt",
   "Slide 1\n\nPlan\n\n- First\n  - Sub\n\nNotes: Say hi\n",{{"input_format","pptx"},{"output_format","txt"},{"include_notes",true}}}};
 for(const auto& test:textDocumentCases){
  const auto& source=test.input==C::DocumentInput::pptx?pptx:test.input==C::DocumentInput::odt?odt:test.input==C::DocumentInput::odp?odp:epub;
  const auto result=C::ConvertStandaloneDocument({test.input,test.output,test.notes},source);
  check(bool(result)&&text(result.bytes)==test.text,std::string(test.name)+": unexpected extracted text: "+text(result.bytes));
  const auto& r=result.report;const auto& x=r.excluded;
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",std::string(D::MediaType(test.output))},{"paragraphs",result.paragraphs},{"headings",result.headings},
    {"list_items",result.listItems},{"block_quotes",result.blockQuotes},{"code_blocks",result.codeBlocks},{"thematic_breaks",result.thematicBreaks},
    {"slides",r.slides},{"chapters",r.chapters},{"notes_included",r.notesIncluded},{"excluded_images",x.images},{"excluded_notes",x.notes},
    {"excluded_scripts",x.scripts},{"excluded_graphics",x.graphics}});
 }
 check(C::ConvertStandaloneDocument({C::DocumentInput::pptx,D::OutputFormat::text,false},pptx).report.excluded.notes==1
  &&C::ConvertStandaloneDocument({C::DocumentInput::epub,D::OutputFormat::text,false},epub).report.chapters==2,"document reports");
 // Tables: a two-sheet XLSX (hidden first sheet), an ODS, CSV and JSON.
 const auto xlsx=docx_fixture::Bytes(P::Xlsx({{"Hidden","hidden","<sheetData><row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>secret</t></is></c></row></sheetData>"},
  {"Data","","<sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
   "<row r=\"2\"><c r=\"A2\"><v>1.5</v></c><c r=\"B2\"><f>A2*2</f><v>3</v></c></row><row r=\"3\"><c r=\"A3\" t=\"inlineStr\"><is><t>a;b</t></is></c></row></sheetData>"
   "<mergeCells count=\"1\"><mergeCell ref=\"A1:B1\"/></mergeCells>"}},{"x","y"}));
 const auto ods=docx_fixture::Bytes(P::Ods("<table:table table:name=\"S\"><table:table-row><table:table-cell office:value-type=\"string\"><text:p>k</text:p>"
  "</table:table-cell><table:table-cell office:value-type=\"float\" office:value=\"7\"><text:p>7</text:p></table:table-cell></table:table-row></table:table>"));
 const auto csv=docx_fixture::Bytes("name,qty\nwidget,2\n\"a,b\",3\nshort\n");
 const auto json=docx_fixture::Bytes("[{\"name\":\"widget\",\"qty\":2},{\"name\":\"a;b\",\"qty\":3,\"note\":\"x\"}]");
 write(root/"files/inputs/book.xlsx",xlsx);write(root/"files/inputs/sheet.ods",ods);write(root/"files/inputs/items.csv",csv);write(root/"files/inputs/items.json",json);
 const auto tableOptions=[](T::TableFormat input,T::TableFormat output){C::TableOptions o;o.input=input;o.output=output;return o;};
 struct TableCase{const char* name;const char* source;C::TableOptions options;const char* outputPath;std::string_view text;Json args;};
 auto hiddenSemicolon=tableOptions(T::TableFormat::xlsx,T::TableFormat::csv);hiddenSemicolon.sheet={T::SheetSelection::index,0,{}};hiddenSemicolon.delimiter=';';
 auto dataObjects=tableOptions(T::TableFormat::xlsx,T::TableFormat::json);dataObjects.json=T::JsonShape::objects;dataObjects.sheet={T::SheetSelection::name,0,"Data"};
 auto csvObjects=tableOptions(T::TableFormat::csv,T::TableFormat::json);csvObjects.json=T::JsonShape::objects;
 auto jsonSemicolon=tableOptions(T::TableFormat::json,T::TableFormat::csv);jsonSemicolon.delimiter=';';
 const TableCase tableCases[]={
  {"xlsx_to_csv","inputs/book.xlsx",tableOptions(T::TableFormat::xlsx,T::TableFormat::csv),"outputs/book.csv","x,y\n1.5,3\na;b,\n",Json::object()},
  {"xlsx_to_csv","inputs/book.xlsx",hiddenSemicolon,"outputs/hidden.csv","secret\n",{{"sheet_index",0},{"delimiter","semicolon"}}},
  {"xlsx_to_json","inputs/book.xlsx",dataObjects,"outputs/book.json","[\n{\"x\":1.5,\"y\":3},\n{\"x\":\"a;b\",\"y\":\"\"}\n]\n",{{"sheet_name","Data"},{"json_shape","objects"}}},
  {"ods_to_csv","inputs/sheet.ods",tableOptions(T::TableFormat::ods,T::TableFormat::csv),"outputs/sheet.csv","k,7\n",Json::object()},
  {"csv_to_json","inputs/items.csv",csvObjects,"outputs/items.json",
   "[\n{\"name\":\"widget\",\"qty\":2},\n{\"name\":\"a,b\",\"qty\":3},\n{\"name\":\"short\",\"qty\":\"\"}\n]\n",{{"json_shape","objects"}}},
  {"json_to_csv","inputs/items.json",jsonSemicolon,"outputs/from-json.csv","name;qty;note\nwidget;2;\n\"a;b\";3;x\n",{{"delimiter","semicolon"}}},
  {"csv_to_tsv","inputs/items.csv",tableOptions(T::TableFormat::csv,T::TableFormat::tsv),"outputs/items.tsv","name\tqty\nwidget\t2\na,b\t3\nshort\t\n",Json::object()},
  {"convert_table","inputs/book.xlsx",dataObjects,"outputs/generic.json","[\n{\"x\":1.5,\"y\":3},\n{\"x\":\"a;b\",\"y\":\"\"}\n]\n",
   {{"input_format","xlsx"},{"output_format","json"},{"sheet_name","Data"},{"json_shape","objects"}}}};
 for(const auto& test:tableCases){
  const auto& source=test.options.input==T::TableFormat::xlsx?xlsx:test.options.input==T::TableFormat::ods?ods:test.options.input==T::TableFormat::csv?csv:json;
  const auto result=C::ConvertStandaloneTable(test.options,source);
  check(bool(result)&&text(result.bytes)==test.text,std::string(test.name)+": unexpected table text: "+text(result.bytes));
  const auto& r=result.report;Json names=Json::array();for(const auto& s:r.sheets)names.push_back(s.name);
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",test.options.output==T::TableFormat::csv?"text/csv; charset=utf-8":test.options.output==T::TableFormat::tsv
     ?"text/tab-separated-values; charset=utf-8":"application/json"},{"rows",r.rows},{"columns",r.columns},{"sheet_count",r.sheets.size()},
    {"sheet_names",names},{"selected_sheet",r.selectedSheet},{"ragged_rows",r.raggedRows},{"merged_ranges",r.mergedRanges.size()},
    {"formulas",r.formulas},{"formulas_without_value",r.formulasWithoutValue},{"renamed_keys",result.renamedKeys}});
 }
 check(C::ConvertStandaloneTable(tableOptions(T::TableFormat::xlsx,T::TableFormat::csv),xlsx).report.formulas==1
  &&C::ConvertStandaloneTable(tableOptions(T::TableFormat::xlsx,T::TableFormat::csv),xlsx).report.mergedRanges.size()==1
  &&C::ConvertStandaloneTable(tableOptions(T::TableFormat::csv,T::TableFormat::tsv),csv).report.raggedRows==1,"table reports");
 // Markup: HTML with navigation and script, Markdown with an unsafe link, SRT and WebVTT.
 const auto html=docx_fixture::Bytes("<nav><a href=\"/\">Home</a></nav><h1>T</h1><p>a <b>b</b> <a href=\"https://e.test/\">e</a> <a href=\"javascript:x()\">j</a></p><script>s()</script>");
 const auto markdown=docx_fixture::Bytes("# T\n\n[ok](https://e.test/) [bad](javascript:x) <b>raw</b>\n");
 const auto srt=docx_fixture::Bytes("1\n00:00:01,000 --> 00:00:02,500\nHello, <b>world</b>.\n\n2\n00:00:03,000 --> 00:00:04,000 X1:10 X2:20 Y1:5 Y2:6\nTwo\n\n");
 const auto vtt=docx_fixture::Bytes("WEBVTT - demo\n\nNOTE hi\n\nintro\n00:00:01.000 --> 00:00:02.500 align:start\n<v Ann>Hello</v> &amp; bye\n\n");
 write(root/"files/inputs/page.html",html);write(root/"files/inputs/notes.md",markdown);write(root/"files/inputs/subs.srt",srt);write(root/"files/inputs/subs.vtt",vtt);
 const auto markupOptions=[](M::Format input,M::Format output){C::MarkupOptions o;o.input=input;o.output=output;return o;};
 auto noNav=markupOptions(M::Format::html,M::Format::markdown);noNav.dropNavigation=true;
 auto full=markupOptions(M::Format::markdown,M::Format::html);full.fullDocument=true;
 struct MarkupCase{const char* name;const char* source;C::MarkupOptions options;const char* outputPath;std::string_view text;Json args;};
 const MarkupCase markupCases[]={
  {"html_to_markdown","inputs/page.html",noNav,"outputs/page.md","# T\n\na **b** [e](https://e.test/) j\n",{{"drop_navigation",true}}},
  {"markdown_to_html","inputs/notes.md",markupOptions(M::Format::markdown,M::Format::html),"outputs/notes.html",
   "<h1>T</h1>\n<p><a href=\"https://e.test/\">ok</a> bad &lt;b&gt;raw&lt;/b&gt;</p>\n",Json::object()},
  {"srt_to_vtt","inputs/subs.srt",markupOptions(M::Format::srt,M::Format::vtt),"outputs/subs.vtt",
   "WEBVTT\n\n00:00:01.000 --> 00:00:02.500\nHello, <b>world</b>.\n\n00:00:03.000 --> 00:00:04.000\nTwo\n\n",Json::object()},
  {"vtt_to_srt","inputs/subs.vtt",markupOptions(M::Format::vtt,M::Format::srt),"outputs/subs.srt","1\n00:00:01,000 --> 00:00:02,500\nHello & bye\n\n",Json::object()},
  {"convert_markup","inputs/notes.md",full,"outputs/notes-full.html",{},{{"input_format","markdown"},{"output_format","html"},{"full_document",true}}}};
 for(const auto& test:markupCases){
  const auto& source=test.options.input==M::Format::html?html:test.options.input==M::Format::markdown?markdown:test.options.input==M::Format::srt?srt:vtt;
  const auto result=C::ConvertStandaloneMarkup(test.options,source);
  check(bool(result)&&(test.text.empty()?text(result.bytes).starts_with("<!DOCTYPE html>"):text(result.bytes)==test.text),
   std::string(test.name)+": unexpected markup text: "+text(result.bytes));
  const auto& h=result.html;const auto& m=result.markdown;const auto& s=result.subtitles;
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",std::string(M::MediaType(test.options.output))},
    {"dropped_elements",h.scriptStyleDropped+h.embeddedDropped+h.hiddenDropped+h.navigationDropped},{"links_as_text",h.linksAsText},
    {"links",m.links},{"unsafe_links",m.unsafeLinks},{"html_escaped",m.htmlEscaped},{"cues",s.cues},
    {"dropped_identifiers",s.identifiersDropped},{"dropped_cue_settings",s.settingsDropped},
    {"dropped_blocks",s.headerLinesDropped+s.notesDropped+s.styleBlocksDropped+s.regionBlocksDropped},{"stripped_tags",s.tagsStripped+s.overridesStripped}});
 }
 check(C::ConvertStandaloneMarkup(noNav,html).html.navigationDropped==1&&C::ConvertStandaloneMarkup(noNav,html).html.scriptStyleDropped==1
  &&C::ConvertStandaloneMarkup(markupOptions(M::Format::vtt,M::Format::srt),vtt).subtitles.settingsDropped==1
  &&C::ConvertStandaloneMarkup(markupOptions(M::Format::srt,M::Format::vtt),srt).subtitles.settingsDropped==1,"markup reports");
 // AIFF: written from the stereo WAV and the masked 5.1 FLAC, read back to WAV/FLAC.
 const auto aiff=A::EncodeAiff(stereo);check(bool(aiff),"AIFF fixture");write(root/"files/inputs/stereo.aiff",aiff.bytes);
 const AudioCase aiffCases[]={
  {"aiff_to_wav","inputs/stereo.aiff",A::Container::aiff,A::Container::wav,"outputs/from-aiff.wav",Json::object()},
  {"aiff_to_flac","inputs/stereo.aiff",A::Container::aiff,A::Container::flac,"outputs/from-aiff.flac",Json::object()},
  {"wav_to_aiff","inputs/stereo.wav",A::Container::wav,A::Container::aiff,"outputs/stereo.aiff",Json::object()},
  {"flac_to_aiff","inputs/surround.flac",A::Container::flac,A::Container::aiff,"outputs/surround.aiff",Json::object()},
  {"convert_audio","inputs/stereo.aiff",A::Container::aiff,A::Container::flac,"outputs/generic-aiff.flac",{{"input_format","aiff"},{"output_format","flac"}}}};
 for(const auto& test:aiffCases){
  const auto& source=test.input==A::Container::aiff?aiff.bytes:test.input==A::Container::wav?wav.bytes:flac.bytes;
  const auto result=C::ConvertStandaloneAudio(test.input,test.output,source);check(bool(result),"AIFF reference");
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",test.output==A::Container::aiff?"audio/aiff":test.output==A::Container::flac?"audio/flac":"audio/wav"},
    {"sample_rate",result.sampleRate},{"channels",result.channels},{"bits_per_sample",result.bitsPerSample},{"frames",result.frames},
    {"skipped_metadata",result.skippedMetadata},{"channel_mask_dropped",result.channelMaskDropped}});
 }
 check(C::ConvertStandaloneAudio(A::Container::flac,A::Container::aiff,flac.bytes).channelMaskDropped
  &&!C::ConvertStandaloneAudio(A::Container::wav,A::Container::aiff,wav.bytes).channelMaskDropped,"AIFF mask report");
 check(A::DecodeAiff(C::ConvertStandaloneAudio(A::Container::flac,A::Container::aiff,flac.bytes).bytes).audio.samples==surround.samples
  &&C::ConvertStandaloneAudio(A::Container::aiff,A::Container::wav,aiff.bytes).bytes==wav.bytes,"reference AIFF is not lossless");
 // Lossy audio: a 1 s two-tone programme at 44.1 kHz (CD rate: MP3 and Vorbis, rejected by Opus)
 // and at 48 kHz (every lossy format), plus Vorbis, Opus and MP3 sources from the codec owner.
 const auto tone=[](std::uint32_t rate,std::uint32_t channels,std::uint64_t frames){
  A::Audio value{rate,channels,16,std::nullopt,frames,{}};
  for(std::uint64_t i=0;i<frames;++i)for(std::uint32_t c=0;c<channels;++c){const double t=double(i)/rate;
   value.samples.push_back(std::int32_t(9000.0*std::sin(6.283185307179586*(440.0+110.0*c)*t)+3000.0*std::sin(6.283185307179586*1250.0*t)));}
  return value;
 };
 const auto cdWav=A::EncodeWav(tone(44100,2,44100));const auto wideWav=A::EncodeWav(tone(48000,2,48000));const auto wideFlac=A::EncodeFlac(tone(48000,2,48000));
 check(cdWav&&wideWav&&wideFlac,"lossy fixtures");
 write(root/"files/inputs/cd.wav",cdWav.bytes);write(root/"files/inputs/wide.wav",wideWav.bytes);write(root/"files/inputs/wide.flac",wideFlac.bytes);
 std::map<std::string,std::vector<std::byte>> lossySources{{"inputs/cd.wav",cdWav.bytes},{"inputs/wide.wav",wideWav.bytes},
  {"inputs/wide.flac",wideFlac.bytes},{"inputs/stereo.aiff",aiff.bytes}};
 for(const auto& [name,format]:std::initializer_list<std::pair<const char*,A::Container>>{{"inputs/wide.mp3",A::Container::mp3},
   {"inputs/wide.opus",A::Container::opus},{"inputs/wide.ogg",A::Container::vorbis}}){
  const auto encoded=C::ConvertStandaloneAudio(A::Container::wav,format,wideWav.bytes);check(bool(encoded),"lossy source fixture");
  write(root/"files"/name,encoded.bytes);lossySources[name]=encoded.bytes;
 }
 const auto audioMedia=[](A::Container format)->std::string{
  switch(format){case A::Container::wav:return "audio/wav";case A::Container::flac:return "audio/flac";case A::Container::aiff:return "audio/aiff";
   case A::Container::vorbis:return "audio/ogg; codecs=vorbis";case A::Container::opus:return "audio/ogg; codecs=opus";case A::Container::mp3:return "audio/mpeg";}
  throw std::runtime_error("audio media");};
 struct LossyCase{const char* name;const char* source;A::Container input,output;A::LossyOptions options;const char* outputPath;Json args;};
 const auto lossyOptions=[](auto set){A::LossyOptions o;set(o);return o;};
 const auto vbr0=lossyOptions([](auto& o){o.mp3RateControl=A::Mp3RateControl::vbr;o.mp3VbrQuality=0;});
 const auto cbr128=lossyOptions([](auto& o){o.mp3BitrateKbps=128;});
 // 64 kbps is the lowest CBR bitrate whose frames hold the gapless tag at 44.1 kHz stereo.
 const auto cbr64=lossyOptions([](auto& o){o.mp3BitrateKbps=64;});
 const auto opus64=lossyOptions([](auto& o){o.opusBitrateKbps=64;});
 const auto vorbis20=lossyOptions([](auto& o){o.vorbisQuality=20;});
 const LossyCase lossyCases[]={
  {"wav_to_mp3","inputs/cd.wav",A::Container::wav,A::Container::mp3,{},"outputs/cd.mp3",Json::object()},
  {"wav_to_mp3","inputs/cd.wav",A::Container::wav,A::Container::mp3,vbr0,"outputs/cd-v0.mp3",{{"mp3_rate_control","vbr"},{"mp3_vbr_quality",0}}},
  {"wav_to_mp3","inputs/cd.wav",A::Container::wav,A::Container::mp3,cbr64,"outputs/cd-64.mp3",{{"mp3_bitrate_kbps",64}}},
  {"wav_to_mp3","inputs/wide.wav",A::Container::wav,A::Container::mp3,cbr128,"outputs/wide-128.mp3",{{"mp3_rate_control","cbr"},{"mp3_bitrate_kbps",128}}},
  {"mp3_to_wav","inputs/wide.mp3",A::Container::mp3,A::Container::wav,{},"outputs/from-mp3.wav",Json::object()},
  {"wav_to_opus","inputs/wide.wav",A::Container::wav,A::Container::opus,{},"outputs/wide.opus",Json::object()},
  {"wav_to_opus","inputs/wide.wav",A::Container::wav,A::Container::opus,opus64,"outputs/wide-64.opus",{{"opus_bitrate_kbps",64}}},
  {"opus_to_wav","inputs/wide.opus",A::Container::opus,A::Container::wav,{},"outputs/from-opus.wav",Json::object()},
  {"wav_to_vorbis","inputs/cd.wav",A::Container::wav,A::Container::vorbis,{},"outputs/cd.ogg",Json::object()},
  {"wav_to_vorbis","inputs/wide.wav",A::Container::wav,A::Container::vorbis,vorbis20,"outputs/wide-q20.ogg",{{"vorbis_quality",20}}},
  {"vorbis_to_wav","inputs/wide.ogg",A::Container::vorbis,A::Container::wav,{},"outputs/from-vorbis.wav",Json::object()},
  {"flac_to_mp3","inputs/wide.flac",A::Container::flac,A::Container::mp3,{},"outputs/wide.mp3",Json::object()},
  {"flac_to_mp3","inputs/wide.flac",A::Container::flac,A::Container::mp3,vbr0,"outputs/wide-v0.mp3",{{"mp3_rate_control","vbr"},{"mp3_vbr_quality",0}}},
  // convert_audio: lossy to lossy (generation loss), lossy to lossless, lossless to lossy with controls.
  {"convert_audio","inputs/wide.opus",A::Container::opus,A::Container::mp3,{},"outputs/opus-to.mp3",{{"input_format","opus"},{"output_format","mp3"}}},
  {"convert_audio","inputs/wide.ogg",A::Container::vorbis,A::Container::opus,opus64,"outputs/vorbis-to.opus",{{"input_format","vorbis"},{"output_format","opus"},{"opus_bitrate_kbps",64}}},
  {"convert_audio","inputs/wide.mp3",A::Container::mp3,A::Container::vorbis,vorbis20,"outputs/mp3-to.ogg",{{"input_format","mp3"},{"output_format","vorbis"},{"vorbis_quality",20}}},
  {"convert_audio","inputs/wide.mp3",A::Container::mp3,A::Container::flac,{},"outputs/mp3-to.flac",{{"input_format","mp3"},{"output_format","flac"}}},
  {"convert_audio","inputs/wide.opus",A::Container::opus,A::Container::aiff,{},"outputs/opus-to.aiff",{{"input_format","opus"},{"output_format","aiff"}}},
  {"convert_audio","inputs/stereo.aiff",A::Container::aiff,A::Container::vorbis,{},"outputs/aiff-to.ogg",{{"input_format","aiff"},{"output_format","vorbis"}}},
  {"convert_audio","inputs/cd.wav",A::Container::wav,A::Container::mp3,vbr0,"outputs/generic-v0.mp3",{{"input_format","wav"},{"output_format","mp3"},{"mp3_rate_control","vbr"},{"mp3_vbr_quality",0}}}};
 for(const auto& test:lossyCases){
  const auto result=C::ConvertStandaloneAudio(test.input,test.output,lossySources.at(test.source),test.options);check(bool(result),std::string("lossy reference: ")+test.outputPath);
  Json args=test.args;args["source_path"]=test.source;
  converted(test.name,args,test.outputPath,result.bytes,
   {{"media_type",audioMedia(test.output)},{"sample_rate",result.sampleRate},{"channels",result.channels},{"bits_per_sample",result.bitsPerSample},
    {"frames",result.frames},{"lossy_source",result.lossySource},{"lossy_output",result.lossyOutput},{"generation_loss",result.generationLoss},
    {"encoder_delay_trimmed",result.encoderDelayTrimmed},{"encoder_delay_unknown",result.encoderDelayUnknown},
    {"trimmed_leading_frames",result.trimmedLeadingFrames},{"trimmed_trailing_frames",result.trimmedTrailingFrames},
    {"original_sample_rate",result.originalSampleRate},{"comments_dropped_count",result.commentsDropped},{"tags_skipped",result.tagsSkipped},
    {"streams_skipped",result.streamsSkipped},{"skipped_metadata",result.skippedMetadata},{"comments_dropped",result.ignoredComments}});
 }
 // The references themselves keep the contract: exact length back from each lossy format,
 // labels, the trimmed delay, Opus at 48 kHz, controls that change the bytes.
 const auto cdMp3=C::ConvertStandaloneAudio(A::Container::wav,A::Container::mp3,cdWav.bytes);
 const auto cdBack=C::ConvertStandaloneAudio(A::Container::mp3,A::Container::wav,cdMp3.bytes);
 check(cdMp3.lossyOutput&&!cdMp3.lossySource&&cdBack.lossySource&&!cdBack.lossyOutput&&cdBack.frames==44100&&cdBack.sampleRate==44100
  &&cdBack.encoderDelayTrimmed&&cdBack.trimmedLeadingFrames==1105&&A::DecodeWav(cdBack.bytes).audio.frames==44100,"wav_to_mp3 then mp3_to_wav must keep 44100 frames");
 for(const auto* name:{"inputs/wide.mp3","inputs/wide.opus","inputs/wide.ogg"}){
  const auto format=std::string_view(name).ends_with("mp3")?A::Container::mp3:std::string_view(name).ends_with("opus")?A::Container::opus:A::Container::vorbis;
  const auto back=C::ConvertStandaloneAudio(format,A::Container::wav,lossySources.at(name));
  check(back&&back.frames==48000&&back.sampleRate==48000&&back.bitsPerSample==16&&back.lossySource,"lossy source does not decode to 48000 frames");
  check(C::ConvertStandaloneAudio(format,A::Container::mp3,lossySources.at(name)).generationLoss,"generation loss not labelled");
 }
 check(C::ConvertStandaloneAudio(A::Container::opus,A::Container::wav,lossySources.at("inputs/wide.opus")).originalSampleRate==48000,"Opus original rate");
 check(C::ConvertStandaloneAudio(A::Container::wav,A::Container::mp3,cdWav.bytes,vbr0).bytes!=cdMp3.bytes
  &&C::ConvertStandaloneAudio(A::Container::wav,A::Container::opus,wideWav.bytes,opus64).bytes!=lossySources.at("inputs/wide.opus")
  &&C::ConvertStandaloneAudio(A::Container::wav,A::Container::vorbis,wideWav.bytes,vorbis20).bytes!=lossySources.at("inputs/wide.ogg"),"lossy controls do not change the output");
 // Chained Ogg (two complete Opus streams back to back) and a mislabelled Ogg file.
 auto chainedOpus=lossySources.at("inputs/wide.opus");chainedOpus.insert(chainedOpus.end(),lossySources.at("inputs/wide.opus").begin(),lossySources.at("inputs/wide.opus").end());
 write(root/"files/inputs/chained.opus",chainedOpus);
 check(C::ConvertStandaloneAudio(A::Container::opus,A::Container::wav,chainedOpus).codecError==A::Error::chained_stream,"chained fixture");
 check(C::ConvertStandaloneAudio(A::Container::wav,A::Container::opus,cdWav.bytes).codecError==A::Error::resampling_required,"44.1 kHz Opus fixture");
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
 call("convert_audio",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/x.m4a"},{"input_format","wav"},{"output_format","aac"}},"rpcError");
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
 // The new families' owners name their reasons: a password-protected ODT, a DRM EPUB, an ODS given
 // as ODT, non-table JSON, a missing sheet, a macro-enabled workbook, HTML declaring windows-1252, a
 // malformed SRT timestamp (with its line) and a float AIFF-C.
 write(root/"files/inputs/locked.odt",docx_fixture::Bytes(P::EncryptedOdt()));
 call("odt_to_txt",{{"source_path","inputs/locked.odt"},{"output_path","outputs/locked.txt"}},"toolError",std::string(D::ErrorMessage(D::TextDocumentError::odf_encrypted)));
 write(root/"files/inputs/drm.epub",docx_fixture::Bytes(P::DrmEpub()));
 call("epub_to_markdown",{{"source_path","inputs/drm.epub"},{"output_path","outputs/drm.md"}},"toolError",std::string(D::ErrorMessage(D::TextDocumentError::epub_drm)));
 call("odt_to_txt",{{"source_path","inputs/sheet.ods"},{"output_path","outputs/wrong-kind.txt"}},"toolError",std::string(D::ErrorMessage(D::TextDocumentError::odf_wrong_kind)));
 write(root/"files/inputs/object.json",docx_fixture::Bytes("{\"a\":[1]}"));
 call("json_to_csv",{{"source_path","inputs/object.json"},{"output_path","outputs/object.csv"}},"toolError",std::string(T::ErrorMessage(T::Error::json_not_table)));
 call("xlsx_to_csv",{{"source_path","inputs/book.xlsx"},{"output_path","outputs/nope.csv"},{"sheet_name","Nope"}},"toolError",std::string(T::ErrorMessage(T::Error::sheet_not_found)));
 write(root/"files/inputs/macro.xlsx",docx_fixture::Bytes(P::Xlsx({{"S","","<sheetData/>"}},{},"application/vnd.ms-excel.sheet.macroEnabled.main+xml")));
 call("xlsx_to_json",{{"source_path","inputs/macro.xlsx"},{"output_path","outputs/macro.json"}},"toolError",std::string(T::ErrorMessage(T::Error::package_macro_enabled)));
 write(root/"files/inputs/legacy.html",docx_fixture::Bytes("<meta charset=\"windows-1252\"><p>cafe</p>"));
 call("html_to_markdown",{{"source_path","inputs/legacy.html"},{"output_path","outputs/legacy.md"}},"toolError",std::string(M::ErrorMessage(M::Error::unsupported_charset)));
 write(root/"files/inputs/broken.srt",docx_fixture::Bytes("1\n00:00:01,000 --> nope\nx\n\n"));
 call("srt_to_vtt",{{"source_path","inputs/broken.srt"},{"output_path","outputs/broken.vtt"}},"toolError",
  std::string(M::ErrorMessage(M::Error::subtitle_malformed_timestamp))+" (line 2)");
 std::vector<std::byte> floatAifc;{
  std::string b;const auto be=[&](std::uint32_t v,int n){for(int i=n-1;i>=0;--i)b.push_back(char((v>>(8*i))&0xff));};
  b+="FORM";be(68,4);b+="AIFC";b+="FVER";be(4,4);be(0xA2805140u,4);b+="COMM";be(24,4);be(1,2);be(1,4);be(32,2);
  b+=std::string("\x40\x0b\xfa\x00\x00\x00\x00\x00\x00\x00",10);b+="fl32";be(0,2);b+="SSND";be(12,4);be(0,4);be(0,4);be(0x3f800000u,4);
  floatAifc=docx_fixture::Bytes(b);}
 write(root/"files/inputs/float.aifc",floatAifc);
 check(C::ConvertStandaloneAudio(A::Container::aiff,A::Container::wav,floatAifc).codecError==A::Error::unsupported_float,"float AIFF-C fixture");
 call("aiff_to_wav",{{"source_path","inputs/float.aifc"},{"output_path","outputs/float.wav"}},"toolError",std::string(A::ErrorMessage(A::Error::unsupported_float)));
 // Option validation for the new tools is a protocol rejection, never ignored.
 call("convert_table",{{"source_path","inputs/items.csv"},{"output_path","outputs/o1.csv"},{"input_format","csv"},{"output_format","csv"}},"rpcError");
 call("convert_table",{{"source_path","inputs/items.json"},{"output_path","outputs/o2.tsv"},{"input_format","json"},{"output_format","tsv"},{"delimiter","semicolon"}},"rpcError");
 call("convert_table",{{"source_path","inputs/items.csv"},{"output_path","outputs/o3.tsv"},{"input_format","csv"},{"output_format","tsv"},{"json_shape","objects"}},"rpcError");
 call("convert_table",{{"source_path","inputs/items.csv"},{"output_path","outputs/o4.json"},{"input_format","csv"},{"output_format","json"},{"sheet_index",0}},"rpcError");
 call("convert_table",{{"source_path","inputs/book.xlsx"},{"output_path","outputs/o5.csv"},{"input_format","xlsx"},{"output_format","xlsx"}},"rpcError");
 call("xlsx_to_csv",{{"source_path","inputs/book.xlsx"},{"output_path","outputs/o6.csv"},{"sheet_index",1},{"sheet_name","Data"}},"rpcError");
 call("xlsx_to_csv",{{"source_path","inputs/book.xlsx"},{"output_path","outputs/o7.csv"},{"sheet_name",""}},"rpcError");
 call("xlsx_to_json",{{"source_path","inputs/book.xlsx"},{"output_path","outputs/o8.json"},{"delimiter","comma"}},"rpcError");
 call("csv_to_tsv",{{"source_path","inputs/items.csv"},{"output_path","outputs/o9.tsv"},{"json_shape","arrays"}},"rpcError");
 call("csv_to_json",{{"source_path","inputs/items.csv"},{"output_path","outputs/o10.json"},{"delimiter","pipe"}},"rpcError");
 call("convert_markup",{{"source_path","inputs/page.html"},{"output_path","outputs/o11.srt"},{"input_format","html"},{"output_format","srt"}},"rpcError");
 call("convert_markup",{{"source_path","inputs/subs.srt"},{"output_path","outputs/o12.srt"},{"input_format","srt"},{"output_format","srt"}},"rpcError");
 call("convert_markup",{{"source_path","inputs/notes.md"},{"output_path","outputs/o13.html"},{"input_format","markdown"},{"output_format","html"},{"drop_navigation",true}},"rpcError");
 call("convert_markup",{{"source_path","inputs/page.html"},{"output_path","outputs/o14.md"},{"input_format","html"},{"output_format","markdown"},{"full_document",true}},"rpcError");
 call("html_to_markdown",{{"source_path","inputs/page.html"},{"output_path","outputs/o15.md"},{"full_document",true}},"rpcError");
 call("srt_to_vtt",{{"source_path","inputs/subs.srt"},{"output_path","outputs/o16.vtt"},{"drop_navigation",false}},"rpcError");
 call("convert_document",{{"source_path","inputs/body.odt"},{"output_path","outputs/o17.txt"},{"input_format","odt"},{"output_format","txt"},{"include_notes",false}},"rpcError");
 call("odt_to_txt",{{"source_path","inputs/body.odt"},{"output_path","outputs/o18.txt"},{"include_notes",true}},"rpcError");
 call("convert_audio",{{"source_path","inputs/stereo.aiff"},{"output_path","outputs/o19.ogg"},{"input_format","aiff"},{"output_format","ogg"}},"rpcError");
 call("wav_to_aiff",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/o20.aiff"},{"channel_mask_dropped",true}},"rpcError");
 // Lossy controls: out of the schema range, for another output, or for the other MP3 rate control
 // are protocol rejections; a decoder tool takes no control at all.
 call("wav_to_vorbis",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l1.ogg"},{"vorbis_quality",101}},"rpcError");
 call("wav_to_opus",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l2.opus"},{"opus_bitrate_kbps",5}},"rpcError");
 call("wav_to_opus",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l3.opus"},{"opus_bitrate_kbps",511}},"rpcError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/l4.mp3"},{"mp3_rate_control","vbr"},{"mp3_vbr_quality",10}},"rpcError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/l5.mp3"},{"mp3_rate_control","vbr"},{"mp3_bitrate_kbps",128}},"rpcError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/l6.mp3"},{"mp3_vbr_quality",4}},"rpcError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/l7.mp3"},{"mp3_rate_control","abr"}},"rpcError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/l8.mp3"},{"opus_bitrate_kbps",64}},"rpcError");
 call("flac_to_mp3",{{"source_path","inputs/wide.flac"},{"output_path","outputs/l9.mp3"},{"vorbis_quality",50}},"rpcError");
 call("mp3_to_wav",{{"source_path","inputs/wide.mp3"},{"output_path","outputs/l10.wav"},{"mp3_bitrate_kbps",128}},"rpcError");
 call("wav_to_flac",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l11.flac"},{"vorbis_quality",50}},"rpcError");
 call("convert_audio",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l12.flac"},{"input_format","wav"},{"output_format","flac"},{"vorbis_quality",50}},"rpcError");
 call("convert_audio",{{"source_path","inputs/wide.wav"},{"output_path","outputs/l13.opus"},{"input_format","wav"},{"output_format","opus"},{"mp3_bitrate_kbps",128}},"rpcError");
 call("convert_audio",{{"source_path","inputs/wide.mp3"},{"output_path","outputs/l14.ogg"},{"input_format","mp3"},{"output_format","vorbis"},{"opus_bitrate_kbps",64}},"rpcError");
 // Rejections the codec owner names: no resampling (44.1 kHz to Opus, 44.1 kHz to Opus through
 // convert_audio), an MP3 bitrate the rate does not define, CBR bitrates below the gapless-tag
 // floor (typed, naming this source's minimum; 56 kbps is just below it at 44.1 kHz stereo and
 // 64 converts above), more than two channels, a chained Ogg file and Ogg Vorbis passed as Opus.
 call("wav_to_opus",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd.opus"}},"toolError",std::string(A::ErrorMessage(A::Error::resampling_required)));
 call("convert_audio",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd-generic.opus"},{"input_format","wav"},{"output_format","opus"},{"opus_bitrate_kbps",96}},
  "toolError",std::string(A::ErrorMessage(A::Error::resampling_required)));
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd-200.mp3"},{"mp3_bitrate_kbps",200}},"toolError",std::string(A::ErrorMessage(A::Error::invalid_option)));
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd-56.mp3"},{"mp3_bitrate_kbps",56}},"toolError",
  std::string(A::ErrorMessage(A::Error::bitrate_too_low_for_gapless_tag))+". This source (44100 Hz, 2 channels) needs mp3_bitrate_kbps of at least 64 or mp3_rate_control=vbr");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd-32.mp3"},{"mp3_bitrate_kbps",32}},"toolError",
  std::string(A::ErrorMessage(A::Error::bitrate_too_low_for_gapless_tag))+". This source (44100 Hz, 2 channels) needs mp3_bitrate_kbps of at least 64 or mp3_rate_control=vbr");
 call("flac_to_mp3",{{"source_path","inputs/surround.flac"},{"output_path","outputs/surround.mp3"}},"toolError",std::string(A::ErrorMessage(A::Error::channel_limit)));
 call("opus_to_wav",{{"source_path","inputs/chained.opus"},{"output_path","outputs/chained.wav"}},"toolError",std::string(A::ErrorMessage(A::Error::chained_stream)));
 call("opus_to_wav",{{"source_path","inputs/wide.ogg"},{"output_path","outputs/vorbis-as-opus.wav"}},"toolError",std::string(A::ErrorMessage(A::Error::wrong_magic)));
 call("mp3_to_wav",{{"source_path","inputs/wide.wav"},{"output_path","outputs/wav-as-mp3.wav"}},"toolError");
 call("wav_to_mp3",{{"source_path","inputs/cd.wav"},{"output_path","outputs/cd.mp3"}});
 // Existing outputs are never replaced, and sources outside the folder are never read, for every family.
 call("wav_to_flac",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/stereo.flac"}});
 call("docx_to_markdown",{{"source_path","inputs/plan.docx"},{"output_path","outputs/plan.md"}});
 call("docx_to_markdown",{{"source_path","../outside/private.png"},{"output_path","outputs/escape.md"}});
 call("wav_to_aiff",{{"source_path","inputs/stereo.wav"},{"output_path","outputs/stereo.aiff"}});
 call("epub_to_txt",{{"source_path","inputs/book.epub"},{"output_path","outputs/book.txt"}});
 call("csv_to_json",{{"source_path","inputs/items.csv"},{"output_path","outputs/items.json"}});
 call("csv_to_json",{{"source_path","../outside/private.png"},{"output_path","outputs/escape.json"}});
 call("srt_to_vtt",{{"source_path","inputs/subs.srt"},{"output_path","outputs/subs.vtt"}});
 call("html_to_markdown",{{"source_path","link/private.png"},{"output_path","outputs/escape-link.md"}});
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
    // Each new option appears exactly on the tools it applies to.
    const auto only=[&](const char* option,std::set<std::string> tools){
     check(properties.contains(option)==tools.contains(name),name+": "+option+" schema");};
    only("include_notes",{"convert_document","pptx_to_txt","pptx_to_markdown","odp_to_txt","odp_to_markdown"});
    only("delimiter",{"convert_table","xlsx_to_csv","ods_to_csv","csv_to_json","json_to_csv","csv_to_tsv"});
    only("sheet_index",{"convert_table","xlsx_to_csv","xlsx_to_json","ods_to_csv"});
    only("sheet_name",{"convert_table","xlsx_to_csv","xlsx_to_json","ods_to_csv"});
    only("json_shape",{"convert_table","xlsx_to_json","csv_to_json"});
    only("drop_navigation",{"convert_markup","html_to_markdown"});
    only("full_document",{"convert_markup","markdown_to_html"});
    only("vorbis_quality",{"convert_audio","wav_to_vorbis"});
    only("opus_bitrate_kbps",{"convert_audio","wav_to_opus"});
    for(const auto* option:{"mp3_rate_control","mp3_bitrate_kbps","mp3_vbr_quality"})only(option,{"convert_audio","wav_to_mp3","flac_to_mp3"});
    // "Ogg" alone is never offered: Vorbis and Opus are named as codecs.
    if(name=="convert_audio")check(properties.at("output_format").at("enum")==Json{"wav","flac","aiff","vorbis","opus","mp3"},"audio kinds");
    // Every lossy tool states its quality contract.
    if(name.find("mp3")!=std::string::npos||name.find("opus")!=std::string::npos||name.find("vorbis")!=std::string::npos||name=="convert_audio"){
     const auto description=tool.at("description").get<std::string>();
     for(const auto* term:{"ossy","album art","6 min"})check(description.find(term)!=std::string::npos,name+": description lacks the lossy contract ("+term+")");
     if(name.starts_with("wav_to_")||name.starts_with("flac_to_"))
      for(const auto* term:{"never reversible","default","resampled","1 or 2 channels","duration","album art"})
       check(description.find(term)!=std::string::npos,name+": encoder description lacks "+term);
    }
    const auto dump=tool.dump();
    for(const auto* term:{"\"heif\"","\"heic\"","\"avif\"","heic","avif"})
     check(dump.find(term)==std::string::npos,"HEIF/AVIF must not be admitted: "+name);
   }
   check(names==std::set<std::string>{"convert_image","png_to_jpeg","jpeg_to_png","bmp_to_png","bmp_to_jpeg","tga_to_png","dds_to_png",
    "webp_to_png","webp_to_jpeg","png_to_webp","gif_to_png","tiff_to_png","png_to_tiff","ico_to_png","png_to_ico",
    "convert_audio","wav_to_flac","flac_to_wav","convert_document","docx_to_txt","docx_to_markdown","rtf_to_txt","rtf_to_markdown",
    "convert_table","xlsx_to_csv","xlsx_to_json","ods_to_csv","csv_to_json","json_to_csv","csv_to_tsv",
    "pptx_to_txt","pptx_to_markdown","odt_to_txt","odt_to_markdown","odp_to_txt","odp_to_markdown","epub_to_txt","epub_to_markdown",
    "convert_markup","html_to_markdown","markdown_to_html","srt_to_vtt","vtt_to_srt",
    "aiff_to_wav","aiff_to_flac","wav_to_aiff","flac_to_aiff",
    "wav_to_mp3","mp3_to_wav","wav_to_opus","opus_to_wav","wav_to_vorbis","vorbis_to_wav","flac_to_mp3"},
    "exactly fifty-four tools required");
   check(names.size()==54,"tool count");
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
 std::cout<<"PASS "<<index<<" real stdio responses; all 54 tools,"<<converted
  <<" conversions with exact files, hashes and reported metadata; codec-owner reasons; no overwrite or escape\n";
}
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-convert-command-tests-r6262\n";return 0;}
 try{check(argc==3,"expected --prepare or --verify and fresh proof path");const std::string_view mode=argv[1];
  if(mode=="--prepare")prepare(argv[2]);else if(mode=="--verify")verify(argv[2]);else throw std::runtime_error("unknown proof operation");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
