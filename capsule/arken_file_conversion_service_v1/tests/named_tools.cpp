#include "../tools/native/smoke_cases.hpp"
#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_service_v1/paid_conversion.hpp"
#include <set>

namespace arken::conversion::cloudflare {
void NamedToolsTest() {
  namespace Service=arken::file_conversion::service::v1;
  struct Pair {const char* name; const char* input; const char* output; unsigned id;};
  constexpr Pair pairs[]{
    {"png_to_jpeg","png","jpeg",3}, {"jpeg_to_png","jpeg","png",4},
    {"bmp_to_png","bmp","png",5}, {"bmp_to_jpeg","bmp","jpeg",6},
    {"tga_to_png","tga","png",7}, {"dds_to_png","dds","png",8}};
  const auto call=[](const char* name,const Json& args,const Json& metadata=Json::object()) {
    return Json{{"jsonrpc","2.0"},{"id",9007199254740993ULL},{"method","tools/call"},
      {"params",{{"name",name},{"arguments",args},{"_meta",metadata}}}}.dump();
  };
  const Json target={{"file_id",std::string(64,'1')},{"file_token",std::string(64,'2')}};
  const auto corpus=SmokeCases();
  for(const auto mode:{Service::ServerMode::preview,Service::ServerMode::sandbox,Service::ServerMode::production}) {
    const auto list=Json::parse(Json::parse(Service::PlanRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})",mode)).at("body").get<std::string>()).at("result").at("tools");
    Require(list.size()==8,"named_tools_count");
    std::set<std::string> names;
    for(const auto& tool:list) {
      names.insert(tool.at("name").get<std::string>());
      Require(tool.at("description").get<std::string>().size()>80,"named_tools_description");
    }
    const auto generic=Json::parse(Service::PlanRequest(call("convert_image",target),mode));
    for(const auto& pair:pairs) {
      Require(names.contains(pair.name),"named_tool_missing");
      const auto plan=Json::parse(Service::PlanRequest(call(pair.name,target),mode));
      Require(plan.at("commandId")==pair.id&&plan.at("arguments")==generic.at("arguments")
        &&plan.at("responsePrefix")==generic.at("responsePrefix"),"named_tool_changed_file_identity");
      Json upload={{"source_bytes",22},{"input_format",pair.input},{"output_format",pair.output}};
      const auto admit=[&](unsigned id,const Json& args) {
        return Json::parse(Service::ConversionTargetPacket(Json{{"commandId",id},{"arguments",args}}.dump()));
      };
      Require(admit(pair.id,upload).at("allowed")==true,"matching_named_tool_denied");
      auto mismatch=upload;mismatch["input_format"]=std::string(pair.input)=="png"?"bmp":"png";
      Require(admit(pair.id,mismatch).at("code")=="conversion_format_mismatch","wrong_source_pair_admitted");
      mismatch=upload;mismatch["output_format"]=std::string(pair.output)=="png"?"jpeg":"png";
      Require(admit(pair.id,mismatch).at("code")=="conversion_format_mismatch","wrong_target_pair_admitted");
      Require(admit(2,mismatch).at("allowed")==true,"generic_conversion_restricted_to_pair");
      Require(admit(1,upload).at("allowed")==false&&admit(99,upload).at("allowed")==false,"nonconversion_admitted");
      auto extra=target;extra["output_format"]=pair.output;
      Require(Json::parse(Service::PlanRequest(call(pair.name,extra),mode)).at("kind")=="response","pair_override_reached_executor");
      const Json metadata={{"x402/payment",Json::object()}};
      const auto paid=Json::parse(Service::PlanRequest(call(pair.name,target,metadata),mode));
      Require(paid.at("kind")== (mode==Service::ServerMode::preview?"response":"command"),"pair_payment_mode_changed");
    }
  }
  // Execute every named pair through the existing native codec and decode the
  // actual output. The format set is not proof of successful conversion alone.
  for(const auto& pair:pairs) {
    bool proved=false;
    for(const auto& sample:corpus) {
      if(sample.expectedError!=Conversion::Error::none||FormatName(sample.options.input)!=pair.input) continue;
      auto options=sample.options;
      options.output=std::string(pair.output)=="jpeg"?Conversion::image::Format::jpeg:Conversion::image::Format::png;
      options.alphaPolicy=options.output==Conversion::image::Format::jpeg
        ?Conversion::image::AlphaPolicy::flatten_white:Conversion::image::AlphaPolicy::preserve;
      const auto converted=Conversion::ConvertImage(sample.input,options);
      Require(bool(converted),"named_pair_native_conversion_failed");
      const auto decoded=Conversion::image::DecodeImage(options.output,converted.bytes);
      Require(bool(decoded)&&decoded.image.width==converted.width&&decoded.image.height==converted.height,"named_pair_native_output_invalid");
      proved=true;break;
    }
    Require(proved,"named_pair_has_no_native_fixture");
  }
}
}
