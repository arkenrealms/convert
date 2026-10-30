#include "arken/files/workspace/v1/native_files.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
using arken::files::workspace::v1::NativeFiles;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){
  std::cout<<"arken-native-files-tests-r6262\n";return 0;
 }
 try{
  check(argc==2,"expected owned fixture parent");
  const auto root=fs::path(argv[1])/std::to_string(::getpid());check(!fs::exists(root),"fixture already exists");
  fs::create_directories(root/"files/nested");fs::create_directory(root/"outside");
  std::ofstream(root/"files/input.bin")<<"original";std::ofstream(root/"outside/private.bin")<<"outside";
  fs::create_symlink(root/"outside/private.bin",root/"files/escape");
  fs::create_directory_symlink(root/"outside",root/"files/link");
  check(::mkfifo((root/"files/pipe").c_str(),0600)==0,"fifo fixture");
  std::string error;auto files=NativeFiles::open(root/"files",error);check(bool(files),error.c_str());
  check(!NativeFiles::open("relative",error)&&!NativeFiles::open(root/"files/link",error)
   &&!NativeFiles::open("/",error),"unselected/linked root accepted");
  const auto input=files->read("input.bin",8);check(bool(input)&&input.bytes.size()==8,"bounded read");
  check(!files->read("input.bin",7),"read limit ignored");
  for(const auto* path:{"../outside/private.bin","/etc/passwd","escape","link/private.bin","nested","pipe","nested/../input.bin","input.bin/"})
   check(!files->read(path,1024),"unsafe read accepted");
  const auto created=files->create("nested/output.bin",input.bytes);
  check(bool(created)&&created.published&&created.durable&&read(root/"files/nested/output.bin")=="original","atomic create");
  for(const auto* path:{"input.bin","nested/output.bin","escape","link/output.bin","../outside/output.bin","/tmp/unselected.bin","absent/output.bin","nested"}) {
   const auto denied=files->create(path,input.bytes);check(!denied&&!denied.published,"unsafe/existing output accepted");
  }
  check(read(root/"files/input.bin")=="original"&&read(root/"outside/private.bin")=="outside"
   &&!fs::exists(root/"outside/output.bin"),"existing/external data changed");
  std::vector<std::byte> limit(8u*1024u*1024u,std::byte{17});check(bool(files->create("limit.bin",limit)),"large fixture");
  check(files->read("limit.bin",limit.size()).bytes==limit&&!files->read("limit.bin",limit.size()-1),"exact byte boundary");
  for(const auto& item:fs::recursive_directory_iterator(root/"files"))
   check(!item.path().filename().string().starts_with(".arken-write-"),"temporary output retained");
  fs::rename(root/"files",root/"moved");fs::create_directory(root/"files");
  check(!files->read("input.bin",8)&&!files->create("wrong.bin",input.bytes).published,"replaced root accepted");
  check(!fs::exists(root/"files/wrong.bin"),"root replacement wrote output");
  files.reset();fs::remove_all(root);std::cout<<"PASS scoped reads, limits, exclusive outputs, root replacement and no symlink escape\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
