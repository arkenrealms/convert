#pragma once
#include <algorithm>
#include <array>
#include <type_traits>
#include "arken_schema_cpp_v1/tool_json.hpp"
#include "arken_schema_generated/conversion/v1.hpp"
namespace arken::schema::cpp::v1::tool_json {
inline constexpr std::string_view commandCatalog="{\n  \"commands\": [\n    {\n      \"commandId\": 2,\n      \"description\": \"Invoke conversion.v1.convert_image through its owning capsule.\",\n      \"errorSchemas\": [\n        {\n          \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n          \"additionalProperties\": false,\n          \"description\": \"\",\n          \"properties\": {\n            \"code\": {\n              \"type\": \"string\",\n              \"x-arken-normalization\": \"none\",\n              \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n            },\n            \"message\": {\n              \"type\": \"string\",\n              \"x-arken-normalization\": \"none\",\n              \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n            }\n          },\n          \"required\": [\n            \"code\",\n            \"message\"\n          ],\n          \"type\": \"object\",\n          \"x-arken-model\": \"conversion.v1.conversion_error\"\n        }\n      ],\n      \"idempotency\": \"idempotent\",\n      \"inputSchema\": {\n        \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n        \"additionalProperties\": false,\n        \"description\": \"\",\n        \"properties\": {\n          \"file_id\": {\n            \"pattern\": \"^[0-9a-f]{64}$\",\n            \"type\": \"string\",\n            \"x-arken-encoding\": \"lowercase-hex\",\n            \"x-arken-semantic-type\": \"conversion.v1.file_identifier\"\n          },\n          \"file_token\": {\n            \"pattern\": \"^[0-9a-f]{64}$\",\n            \"type\": \"string\",\n            \"x-arken-encoding\": \"lowercase-hex\",\n            \"x-arken-semantic-type\": \"conversion.v1.file_capability\"\n          }\n        },\n        \"required\": [\n          \"file_id\"\n        ],\n        \"type\": \"object\",\n        \"x-arken-model\": \"conversion.v1.file_target\"\n      },\n      \"name\": \"conversion.v1.convert_image\",\n      \"outputSchema\": {\n        \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n        \"additionalProperties\": false,\n        \"description\": \"\",\n        \"properties\": {\n          \"alpha_disposition\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"download_url\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"expires_at\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"file_id\": {\n            \"pattern\": \"^[0-9a-f]{64}$\",\n            \"type\": \"string\",\n            \"x-arken-encoding\": \"lowercase-hex\",\n            \"x-arken-semantic-type\": \"conversion.v1.file_identifier\"\n          },\n          \"height\": {\n            \"maximum\": 4294967295,\n            \"minimum\": 0,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          },\n          \"media_type\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"output_bytes\": {\n            \"maximum\": 4294967295,\n            \"minimum\": 0,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          },\n          \"sha256\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"width\": {\n            \"maximum\": 4294967295,\n            \"minimum\": 0,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          }\n        },\n        \"required\": [\n          \"file_id\",\n          \"download_url\",\n          \"expires_at\",\n          \"width\",\n          \"height\",\n          \"output_bytes\",\n          \"sha256\",\n          \"media_type\",\n          \"alpha_disposition\"\n        ],\n        \"type\": \"object\",\n        \"x-arken-model\": \"conversion.v1.conversion_result\"\n      },\n      \"ownerCapsule\": \"arken_file_conversion_service_v1\",\n      \"streaming\": \"unary\"\n    },\n    {\n      \"commandId\": 1,\n      \"description\": \"Invoke conversion.v1.prepare_upload through its owning capsule.\",\n      \"errorSchemas\": [\n        {\n          \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n          \"additionalProperties\": false,\n          \"description\": \"\",\n          \"properties\": {\n            \"code\": {\n              \"type\": \"string\",\n              \"x-arken-normalization\": \"none\",\n              \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n            },\n            \"message\": {\n              \"type\": \"string\",\n              \"x-arken-normalization\": \"none\",\n              \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n            }\n          },\n          \"required\": [\n            \"code\",\n            \"message\"\n          ],\n          \"type\": \"object\",\n          \"x-arken-model\": \"conversion.v1.conversion_error\"\n        }\n      ],\n      \"idempotency\": \"none\",\n      \"inputSchema\": {\n        \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n        \"additionalProperties\": false,\n        \"description\": \"\",\n        \"properties\": {\n          \"alpha_policy\": {\n            \"enum\": [\n              \"preserve\",\n              \"flatten_white\"\n            ],\n            \"type\": \"string\",\n            \"x-arken-semantic-type\": \"conversion.v1.alpha_policy\"\n          },\n          \"input_format\": {\n            \"enum\": [\n              \"dds\",\n              \"tga\",\n              \"png\",\n              \"bmp\",\n              \"jpeg\"\n            ],\n            \"type\": \"string\",\n            \"x-arken-semantic-type\": \"conversion.v1.input_format\"\n          },\n          \"jpeg_quality\": {\n            \"maximum\": 100,\n            \"minimum\": 1,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          },\n          \"output_format\": {\n            \"enum\": [\n              \"tga\",\n              \"png\",\n              \"bmp\",\n              \"jpeg\"\n            ],\n            \"type\": \"string\",\n            \"x-arken-semantic-type\": \"conversion.v1.output_format\"\n          },\n          \"source_bytes\": {\n            \"maximum\": 8388608,\n            \"minimum\": 1,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          }\n        },\n        \"required\": [\n          \"source_bytes\",\n          \"input_format\",\n          \"output_format\"\n        ],\n        \"type\": \"object\",\n        \"x-arken-model\": \"conversion.v1.upload_request\"\n      },\n      \"name\": \"conversion.v1.prepare_upload\",\n      \"outputSchema\": {\n        \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n        \"additionalProperties\": false,\n        \"description\": \"\",\n        \"properties\": {\n          \"expires_at\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          },\n          \"file_id\": {\n            \"pattern\": \"^[0-9a-f]{64}$\",\n            \"type\": \"string\",\n            \"x-arken-encoding\": \"lowercase-hex\",\n            \"x-arken-semantic-type\": \"conversion.v1.file_identifier\"\n          },\n          \"file_token\": {\n            \"pattern\": \"^[0-9a-f]{64}$\",\n            \"type\": \"string\",\n            \"x-arken-encoding\": \"lowercase-hex\",\n            \"x-arken-semantic-type\": \"conversion.v1.file_capability\"\n          },\n          \"maximum_source_bytes\": {\n            \"maximum\": 4294967295,\n            \"minimum\": 0,\n            \"type\": \"integer\",\n            \"x-arken-semantic-type\": \"conversion.v1.count\"\n          },\n          \"upload_url\": {\n            \"type\": \"string\",\n            \"x-arken-normalization\": \"none\",\n            \"x-arken-semantic-type\": \"conversion.v1.text_value\"\n          }\n        },\n        \"required\": [\n          \"file_id\",\n          \"upload_url\",\n          \"expires_at\",\n          \"maximum_source_bytes\"\n        ],\n        \"type\": \"object\",\n        \"x-arken-model\": \"conversion.v1.upload_result\"\n      },\n      \"ownerCapsule\": \"arken_file_conversion_service_v1\",\n      \"streaming\": \"unary\"\n    }\n  ],\n  \"jsonProfile\": \"arken.schema.tool-json.v1\",\n  \"schema\": \"arken.schema.command-catalog.v1\",\n  \"semanticFingerprint\": \"7690c2276735e98462c7d9233ee3ccba96d15c1184b2b7dbdc653a8387a4dba9\"\n}\n";
template<> struct Codec<::arken::schema::generated::conversion::v1::input_kind> { using T=::arken::schema::generated::conversion::v1::input_kind;
static T read(const Json& input){
require(input.is_string(),"enum name required");const auto& value=input.get_ref<const std::string&>();
if(value=="dds")return T::dds;
if(value=="tga")return T::tga;
if(value=="png")return T::png;
if(value=="bmp")return T::bmp;
if(value=="jpeg")return T::jpeg;
throw std::invalid_argument("unknown enum value");}
static Json write(T value){switch(value){
case T::dds:return "dds";
case T::tga:return "tga";
case T::png:return "png";
case T::bmp:return "bmp";
case T::jpeg:return "jpeg";
}throw std::invalid_argument("unknown enum value");}};
template<> struct Codec<::arken::schema::generated::conversion::v1::output_kind> { using T=::arken::schema::generated::conversion::v1::output_kind;
static T read(const Json& input){
require(input.is_string(),"enum name required");const auto& value=input.get_ref<const std::string&>();
if(value=="tga")return T::tga;
if(value=="png")return T::png;
if(value=="bmp")return T::bmp;
if(value=="jpeg")return T::jpeg;
throw std::invalid_argument("unknown enum value");}
static Json write(T value){switch(value){
case T::tga:return "tga";
case T::png:return "png";
case T::bmp:return "bmp";
case T::jpeg:return "jpeg";
}throw std::invalid_argument("unknown enum value");}};
template<> struct Codec<::arken::schema::generated::conversion::v1::alpha_kind> { using T=::arken::schema::generated::conversion::v1::alpha_kind;
static T read(const Json& input){
require(input.is_string(),"enum name required");const auto& value=input.get_ref<const std::string&>();
if(value=="preserve")return T::preserve;
if(value=="flatten_white")return T::flatten_white;
throw std::invalid_argument("unknown enum value");}
static Json write(T value){switch(value){
case T::preserve:return "preserve";
case T::flatten_white:return "flatten_white";
}throw std::invalid_argument("unknown enum value");}};
template<> struct Codec<::arken::schema::generated::conversion::v1::upload_request> {
using T=::arken::schema::generated::conversion::v1::upload_request;
static T read(const Json& input){ require(input.is_object(),"object required");
constexpr std::array<std::string_view,5> keys{"source_bytes","input_format","output_format","jpeg_quality","alpha_policy",};
for(auto it=input.begin();it!=input.end();++it)require(std::find(keys.begin(),keys.end(),it.key())!=keys.end(),"unknown field"); T value;
if(input.contains("source_bytes")){
value.source_bytes=Codec<decltype(value.source_bytes)>::read(input.at("source_bytes"));
}else throw std::invalid_argument("required field missing");
if(input.contains("input_format")){
value.input_format=Codec<decltype(value.input_format)>::read(input.at("input_format"));
}else throw std::invalid_argument("required field missing");
if(input.contains("output_format")){
value.output_format=Codec<decltype(value.output_format)>::read(input.at("output_format"));
}else throw std::invalid_argument("required field missing");
if(input.contains("jpeg_quality")){
value.jpeg_quality=Codec<typename decltype(value.jpeg_quality)::value_type>::read(input.at("jpeg_quality"));
}
if(input.contains("alpha_policy")){
value.alpha_policy=Codec<typename decltype(value.alpha_policy)::value_type>::read(input.at("alpha_policy"));
}
require(::arken::schema::generated::conversion::v1::upload_request_binding::validate(value).ok(),"model validation failed");return value;}
static Json write(const T& value){require(::arken::schema::generated::conversion::v1::upload_request_binding::validate(value).ok(),"model validation failed");Json out=Json::object();
out["source_bytes"]=tool_json::write(value.source_bytes);
out["input_format"]=tool_json::write(value.input_format);
out["output_format"]=tool_json::write(value.output_format);
if(value.jpeg_quality)out["jpeg_quality"]=tool_json::write(*value.jpeg_quality);
if(value.alpha_policy)out["alpha_policy"]=tool_json::write(*value.alpha_policy);
return out;}
};
template<> struct Codec<::arken::schema::generated::conversion::v1::file_target> {
using T=::arken::schema::generated::conversion::v1::file_target;
static T read(const Json& input){ require(input.is_object(),"object required");
constexpr std::array<std::string_view,2> keys{"file_id","file_token",};
for(auto it=input.begin();it!=input.end();++it)require(std::find(keys.begin(),keys.end(),it.key())!=keys.end(),"unknown field"); T value;
if(input.contains("file_id")){
value.file_id=Codec<decltype(value.file_id)>::read(input.at("file_id"));
}else throw std::invalid_argument("required field missing");
if(input.contains("file_token")){
value.file_token=Codec<typename decltype(value.file_token)::value_type>::read(input.at("file_token"));
}
require(::arken::schema::generated::conversion::v1::file_target_binding::validate(value).ok(),"model validation failed");return value;}
static Json write(const T& value){require(::arken::schema::generated::conversion::v1::file_target_binding::validate(value).ok(),"model validation failed");Json out=Json::object();
out["file_id"]=tool_json::write(value.file_id);
if(value.file_token)out["file_token"]=tool_json::write(*value.file_token);
return out;}
};
template<> struct Codec<::arken::schema::generated::conversion::v1::upload_result> {
using T=::arken::schema::generated::conversion::v1::upload_result;
static T read(const Json& input){ require(input.is_object(),"object required");
constexpr std::array<std::string_view,5> keys{"file_id","upload_url","expires_at","maximum_source_bytes","file_token",};
for(auto it=input.begin();it!=input.end();++it)require(std::find(keys.begin(),keys.end(),it.key())!=keys.end(),"unknown field"); T value;
if(input.contains("file_id")){
value.file_id=Codec<decltype(value.file_id)>::read(input.at("file_id"));
}else throw std::invalid_argument("required field missing");
if(input.contains("upload_url")){
value.upload_url=Codec<decltype(value.upload_url)>::read(input.at("upload_url"));
}else throw std::invalid_argument("required field missing");
if(input.contains("expires_at")){
value.expires_at=Codec<decltype(value.expires_at)>::read(input.at("expires_at"));
}else throw std::invalid_argument("required field missing");
if(input.contains("maximum_source_bytes")){
value.maximum_source_bytes=Codec<decltype(value.maximum_source_bytes)>::read(input.at("maximum_source_bytes"));
}else throw std::invalid_argument("required field missing");
if(input.contains("file_token")){
value.file_token=Codec<typename decltype(value.file_token)::value_type>::read(input.at("file_token"));
}
require(::arken::schema::generated::conversion::v1::upload_result_binding::validate(value).ok(),"model validation failed");return value;}
static Json write(const T& value){require(::arken::schema::generated::conversion::v1::upload_result_binding::validate(value).ok(),"model validation failed");Json out=Json::object();
out["file_id"]=tool_json::write(value.file_id);
out["upload_url"]=tool_json::write(value.upload_url);
out["expires_at"]=tool_json::write(value.expires_at);
out["maximum_source_bytes"]=tool_json::write(value.maximum_source_bytes);
if(value.file_token)out["file_token"]=tool_json::write(*value.file_token);
return out;}
};
template<> struct Codec<::arken::schema::generated::conversion::v1::conversion_result> {
using T=::arken::schema::generated::conversion::v1::conversion_result;
static T read(const Json& input){ require(input.is_object(),"object required");
constexpr std::array<std::string_view,9> keys{"file_id","download_url","expires_at","width","height","output_bytes","sha256","media_type","alpha_disposition",};
for(auto it=input.begin();it!=input.end();++it)require(std::find(keys.begin(),keys.end(),it.key())!=keys.end(),"unknown field"); T value;
if(input.contains("file_id")){
value.file_id=Codec<decltype(value.file_id)>::read(input.at("file_id"));
}else throw std::invalid_argument("required field missing");
if(input.contains("download_url")){
value.download_url=Codec<decltype(value.download_url)>::read(input.at("download_url"));
}else throw std::invalid_argument("required field missing");
if(input.contains("expires_at")){
value.expires_at=Codec<decltype(value.expires_at)>::read(input.at("expires_at"));
}else throw std::invalid_argument("required field missing");
if(input.contains("width")){
value.width=Codec<decltype(value.width)>::read(input.at("width"));
}else throw std::invalid_argument("required field missing");
if(input.contains("height")){
value.height=Codec<decltype(value.height)>::read(input.at("height"));
}else throw std::invalid_argument("required field missing");
if(input.contains("output_bytes")){
value.output_bytes=Codec<decltype(value.output_bytes)>::read(input.at("output_bytes"));
}else throw std::invalid_argument("required field missing");
if(input.contains("sha256")){
value.sha256=Codec<decltype(value.sha256)>::read(input.at("sha256"));
}else throw std::invalid_argument("required field missing");
if(input.contains("media_type")){
value.media_type=Codec<decltype(value.media_type)>::read(input.at("media_type"));
}else throw std::invalid_argument("required field missing");
if(input.contains("alpha_disposition")){
value.alpha_disposition=Codec<decltype(value.alpha_disposition)>::read(input.at("alpha_disposition"));
}else throw std::invalid_argument("required field missing");
require(::arken::schema::generated::conversion::v1::conversion_result_binding::validate(value).ok(),"model validation failed");return value;}
static Json write(const T& value){require(::arken::schema::generated::conversion::v1::conversion_result_binding::validate(value).ok(),"model validation failed");Json out=Json::object();
out["file_id"]=tool_json::write(value.file_id);
out["download_url"]=tool_json::write(value.download_url);
out["expires_at"]=tool_json::write(value.expires_at);
out["width"]=tool_json::write(value.width);
out["height"]=tool_json::write(value.height);
out["output_bytes"]=tool_json::write(value.output_bytes);
out["sha256"]=tool_json::write(value.sha256);
out["media_type"]=tool_json::write(value.media_type);
out["alpha_disposition"]=tool_json::write(value.alpha_disposition);
return out;}
};
template<> struct Codec<::arken::schema::generated::conversion::v1::conversion_error> {
using T=::arken::schema::generated::conversion::v1::conversion_error;
static T read(const Json& input){ require(input.is_object(),"object required");
constexpr std::array<std::string_view,2> keys{"code","message",};
for(auto it=input.begin();it!=input.end();++it)require(std::find(keys.begin(),keys.end(),it.key())!=keys.end(),"unknown field"); T value;
if(input.contains("code")){
value.code=Codec<decltype(value.code)>::read(input.at("code"));
}else throw std::invalid_argument("required field missing");
if(input.contains("message")){
value.message=Codec<decltype(value.message)>::read(input.at("message"));
}else throw std::invalid_argument("required field missing");
require(::arken::schema::generated::conversion::v1::conversion_error_binding::validate(value).ok(),"model validation failed");return value;}
static Json write(const T& value){require(::arken::schema::generated::conversion::v1::conversion_error_binding::validate(value).ok(),"model validation failed");Json out=Json::object();
out["code"]=tool_json::write(value.code);
out["message"]=tool_json::write(value.message);
return out;}
};
}
