#include "arken_mcp_protocol_v1/stdio.hpp"
#include <istream>
#include <ostream>
namespace arken::mcp::v1 {
namespace {
int run(std::istream& input,std::ostream& output,std::ostream& diagnostics,
 const StdioLimits& limits,const std::function<Json(std::string_view)>& dispatch){
 if(!dispatch||!limits.requestBytes||limits.requestBytes>maximumConfiguredRequestBytes||
    !limits.responseBytes||limits.responseBytes>maximumConfiguredCatalogBytes){
  diagnostics<<"mcp_stdio_limits_invalid\n";return 2;
 }
 std::string frame;bool overflow=false;char value;
 while(input.get(value)){
  if(value!='\n'){
   if(frame.size()<limits.requestBytes&&!overflow)frame.push_back(value);
   else{frame.clear();overflow=true;}
   continue;
  }
  try{
   const auto response=overflow?failure(nullptr,-32600,"Request exceeds the configured limit"):dispatch(frame);
   frame.clear();overflow=false;
   const auto& body=response.at("body").get_ref<const std::string&>();
   if(body.empty())continue;
   if(body.size()>limits.responseBytes||body.find_first_of("\r\n")!=std::string::npos){
    diagnostics<<"mcp_stdio_response_invalid\n";return 2;
   }
   const auto wire=::arken::schema::cpp::v1::tool_json::parse(body,limits.responseBytes);
   if(!wire.is_object()||wire.value("jsonrpc",std::string{})!="2.0"||!wire.contains("id")||
      !(wire.at("id").is_null()||wire.at("id").is_string()||wire.at("id").is_number_integer())||
      wire.contains("result")==wire.contains("error")){
    diagnostics<<"mcp_stdio_response_invalid\n";return 2;
   }
   output.write(body.data(),static_cast<std::streamsize>(body.size()));output.put('\n');output.flush();
   if(!output){diagnostics<<"mcp_stdio_write_failed\n";return 2;}
  }catch(const std::exception&){diagnostics<<"mcp_stdio_dispatch_failed\n";return 2;}
 }
 if(input.bad()||!input.eof()){diagnostics<<"mcp_stdio_read_failed\n";return 2;}
 if(overflow||!frame.empty()){diagnostics<<"mcp_stdio_incomplete_frame\n";return 2;}
 return 0;
}
}
int serveStdio(std::istream& input,std::ostream& output,std::ostream& diagnostics,
 const StdioLimits& limits,const std::function<Json(std::string_view)>& dispatch) noexcept{
 try{return run(input,output,diagnostics,limits,dispatch);}
 catch(...){
  try{diagnostics<<"mcp_stdio_io_failed\n";}catch(...){return 2;}
  return 2;
 }
}
}
