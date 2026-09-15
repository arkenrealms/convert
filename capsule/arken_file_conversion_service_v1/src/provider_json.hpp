#pragma once
#include "arken_mcp_protocol_v1/api.hpp"
namespace arken::file_conversion::service::v1::payment::provider_json {
using Json=arken::mcp::v1::Json;
inline Json Parse(std::string_view text,std::size_t maximumBytes=16384){return arken::mcp::v1::parseBoundedJson(text,maximumBytes);}
inline void Fields(const Json& value,std::initializer_list<std::string_view> names){
    arken::schema::cpp::v1::tool_json::require(value.is_object()&&value.size()==names.size(),"provider fields");
    for(auto name:names)arken::schema::cpp::v1::tool_json::require(value.contains(name),"provider field");
}
}
