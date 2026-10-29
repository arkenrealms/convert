#include "arken_cloudflare_client_v1/api.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace arken::cloudflare::v1 {
void PrivateDirectory(const fs::path& path) {
  // Publish each new directory with its final private mode. Creating with
  // the process umask then chmod leaves a window where a concurrent caller
  // correctly rejects the same directory as public.
  if(mkdir(path.c_str(),0700)!=0){
    const int error=errno;
    if(error==ENOENT){
      const auto parent=path.parent_path();Require(!parent.empty()&&parent!=path,"directory_parent_required");
      if(!fs::exists(parent))PrivateDirectory(parent);
      if(mkdir(path.c_str(),0700)!=0)Require(errno==EEXIST,"directory_creation_failed");
    }else Require(error==EEXIST,"directory_creation_failed");
  }
  struct stat st{};Require(lstat(path.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&(st.st_mode&077)==0,"private_directory_required");
}
std::string Read(const fs::path& path,std::size_t limit,bool secret) {
  const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);Require(fd>=0,"file_open_failed");
  struct stat st{};
  const bool valid=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0&&static_cast<std::uint64_t>(st.st_size)<=limit
    &&(!secret||(st.st_uid==geteuid()&&(st.st_mode&077)==0&&st.st_nlink==1));
  if(!valid){close(fd);throw std::runtime_error("file_identity_or_permissions_invalid");}
  std::string value(static_cast<std::size_t>(st.st_size),'\0');std::size_t at=0;
  while(at<value.size()){const auto n=read(fd,value.data()+at,value.size()-at);if(n<0&&errno==EINTR)continue;
    if(n<=0){close(fd);throw std::runtime_error("file_read_failed");}at+=static_cast<std::size_t>(n);}
  close(fd);return value;
}
void Write(const fs::path& path,std::string_view value,bool exclusive) {
  const auto temp=path.string()+".stage-"+Random();
  const int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);Require(fd>=0,"file_stage_failed");
  std::size_t at=0;bool ok=true;
  while(at<value.size()){const auto n=write(fd,value.data()+at,value.size()-at);if(n<0&&errno==EINTR)continue;
    if(n<=0){ok=false;break;}at+=static_cast<std::size_t>(n);}
  ok=fsync(fd)==0&&ok;close(fd);
  if(ok)ok=exclusive?link(temp.c_str(),path.c_str())==0:rename(temp.c_str(),path.c_str())==0;
  unlink(temp.c_str());Require(ok,"file_commit_failed");
  const int parent=open(path.parent_path().c_str(),O_RDONLY|O_CLOEXEC);if(parent>=0){fsync(parent);close(parent);}
}
std::string Random(){std::array<unsigned char,32> bytes{};arc4random_buf(bytes.data(),bytes.size());
  std::string value;for(auto b:bytes){value+="0123456789abcdef"[b>>4];value+="0123456789abcdef"[b&15];}return value;}
Lock::Lock(const fs::path& path){fd=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);Require(fd>=0,"lock_open_failed");
  if(flock(fd,LOCK_EX|LOCK_NB)!=0){close(fd);fd=-1;throw std::runtime_error("operation_already_running");}}
Lock::~Lock(){if(fd>=0)close(fd);}
}
