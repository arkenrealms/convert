#include "arken_mcp_protocol_v1/stdio.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace P=arken::mcp::v1;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct FailingBuffer:std::streambuf {int_type overflow(int_type) override{return traits_type::eof();}};
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
  std::cout<<"arken-mcp-stdio-tests-r6258\n";return 0;
 }
 try{
  unsigned calls=0;
  const auto dispatch=[&](std::string_view input){
   ++calls;auto request=P::Json::parse(input);
   if(request.value("method",std::string{})=="notifications/initialized")return P::Json{{"status",202},{"body",""}};
   return P::reply(request.at("id"),P::Json{{"value","π\ntext"}});
  };
  std::istringstream input("{\"id\":9007199254740993}\n{\"method\":\"notifications/initialized\"}\n{\"id\":\"π\"}\r\n");
  std::ostringstream output,errors;
  require(P::serveStdio(input,output,errors,{},dispatch)==0&&calls==3&&errors.str().empty(),"multi-frame session");
  std::istringstream frames(output.str());std::string line;
  std::getline(frames,line);require(P::Json::parse(line).at("id")==9007199254740993ULL,"exact numeric ID");
  std::getline(frames,line);require(P::Json::parse(line).at("id")=="π","Unicode ID");
  require(!std::getline(frames,line),"notification emitted a frame");
  calls=0;std::istringstream large(std::string(100,'x')+"\n{\"id\":1}\n");output.str("");errors.str("");
  require(P::serveStdio(large,output,errors,{16,4096},dispatch)==0&&calls==1,"overflow dispatched or lost next frame");
  frames.clear();frames.str(output.str());std::getline(frames,line);
  require(P::Json::parse(line).at("error").at("code")==-32600,"overflow response");
  std::getline(frames,line);require(P::Json::parse(line).at("id")==1,"resynchronized frame");
  calls=0;std::istringstream partial("{\"id\":1}");output.str("");errors.str("");
  require(P::serveStdio(partial,output,errors,{},dispatch)!=0&&calls==0&&output.str().empty(),"partial EOF dispatched");
  std::istringstream empty;output.str("");errors.str("");
  require(P::serveStdio(empty,output,errors,{},dispatch)==0&&output.str().empty(),"empty EOF");
  std::istringstream invalid("{}\n");output.str("");errors.str("");
  require(P::serveStdio(invalid,output,errors,{},[](auto){return P::Json{{"body","not-json"}};})!=0&&output.str().empty(),"invalid stdout body");
  std::istringstream multiline("{}\n");output.str("");errors.str("");
  require(P::serveStdio(multiline,output,errors,{},[](auto){return P::Json{{"body","{}\n{}"}};})!=0&&output.str().empty(),"multiline stdout body");
  std::istringstream tooSmall("{\"id\":1}\n");output.str("");errors.str("");
  require(P::serveStdio(tooSmall,output,errors,{16,1},dispatch)!=0&&output.str().empty(),"response budget");
  std::istringstream failing("{\"id\":1}\n");FailingBuffer buffer;std::ostream broken(&buffer);
  require(P::serveStdio(failing,broken,errors,{},dispatch)!=0,"output failure");
  std::istringstream bad;bad.setstate(std::ios::badbit);
  require(P::serveStdio(bad,output,errors,{},dispatch)!=0,"input failure");
  std::cout<<"PASS bounded stdio framing, notification silence, IDs, EOF and failures\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
