#include "workerd_profile.hpp"
#include <iostream>
#import <Foundation/Foundation.h>
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
  std::cout<<"arken-codecast-workerd-profile-r6252\n";return 0;
 }
 @autoreleasepool {
  try {
   if(argc==2&&std::string_view(argv[1])=="--workerd-signal-mask-probe")return WorkerdSignalMaskProbe();
   if(argc!=6||std::string_view(argv[1])!="--workerd-profile")throw std::runtime_error("workerd_profile_arguments_invalid");
   return WorkerdProfile(argc,argv);
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 }
}
