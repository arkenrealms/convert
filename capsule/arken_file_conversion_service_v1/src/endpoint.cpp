#include "arken_file_conversion_service_v1/endpoint.hpp"
#include "arken_mcp_protocol_v1/api.hpp"
namespace arken::file_conversion::service::v1 {
namespace {
namespace M=arken::mcp::v1;using Json=M::Json;
bool origin(std::string_view s){return s.starts_with("https://")&&s.size()>8&&s.size()<=261&&s.substr(8).find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")==s.npos;}
Json deny(int status,const char* code){return {{"allowed",false},{"status",status},{"code",code},{"kind","denied"}};}
Json decide(const Json& p){
 const auto& c=p.at("configuration");const auto& r=p.at("request");
 if(!c.is_object()||c.size()!=3||!r.is_object()||r.size()!=8||p.size()!=2)return deny(503,"endpoint_configuration_invalid");
 const auto canonical=c.at("origin").is_null()?std::string{}:c.at("origin").get<std::string>();
 const auto prior=c.at("priorArtifactOrigin").is_null()?std::string{}:c.at("priorArtifactOrigin").get<std::string>();
 const auto endpoint=c.at("path").get<std::string>();
 if((!canonical.empty()&&!origin(canonical))||(!prior.empty()&&(!origin(prior)||canonical.empty()||prior==canonical))||(endpoint!="/"&&endpoint!="/mcp"))return deny(503,"endpoint_configuration_invalid");
 const auto host=r.at("origin").get<std::string>(),path=r.at("path").get<std::string>();
 if(r.at("hasOrigin").get<bool>()&&r.at("originHeader")!=host)return deny(403,"origin_denied");
 const bool file=path.starts_with("/files/"),retained=file&&!prior.empty()&&host==prior;
 if(!canonical.empty()&&host!=canonical&&!retained)return deny(421,"canonical_endpoint_required");
 if(path=="/health")return {{"allowed",true},{"kind","health"}};
 if(path=="/_arken/production/qualify"||path=="/_arken/production/readiness")return {{"allowed",true},{"kind","admin"}};
 if(file)return {{"allowed",true},{"kind","file"}};
 const auto admission=M::admitHttp({r.at("method"),path,host,r.at("originHeader"),r.at("contentType"),r.at("accept"),r.at("version"),r.at("hasOrigin")},endpoint);
 if(!admission.ok())return {{"allowed",false},{"kind","denied"},{"status",admission.status},{"code",admission.code}};
 return {{"allowed",true},{"kind","mcp"}};
}
}
std::string EndpointPacket(std::string_view input) noexcept {try{return decide(M::parseRequestJson(input)).dump();}catch(...){return deny(503,"endpoint_configuration_invalid").dump();}}
}
