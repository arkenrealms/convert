#include "arken_file_conversion_commands_v1/api.hpp"
#include "arken_mcp_protocol_v1/stdio.hpp"
#include <iostream>
#include <string_view>
int main(int argc,char** argv) {
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
  std::cout<<"arken-convert-commands r6262\n";return 0;
 }
 try {
  namespace C=arken::file_conversion::commands::v1;
  if(argc==2&&std::string_view(argv[1])=="--catalog"){std::cout<<C::catalog()<<'\n';return 0;}
  if(argc!=4||std::string_view(argv[1])!="--stdio"||std::string_view(argv[2])!="--root"){
   std::cerr<<"Use --stdio --root /absolute/physical/folder, --catalog or --version.\n";return 2;
  }
  std::string error;auto session=C::Session::open(argv[3],error);
  if(!session){std::cerr<<error<<'\n';return 2;}
  return arken::mcp::v1::serveStdio(std::cin,std::cout,std::cerr,{},
    [&](std::string_view request){return session->request(request);});
 }catch(const std::exception&){std::cerr<<"Conversion server failed.\n";return 2;}
}
