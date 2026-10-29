#include "arken_cloudflare_client_v1/api.hpp"
#include "private_file_internal.hpp"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

namespace arken::cloudflare::v1 {
using namespace private_file_detail;
void WritePrivateFileExclusive(const fs::path& path, std::string_view bytes, std::size_t limit) {
  Require(path.is_absolute() && path == path.lexically_normal() && !path.filename().empty()
    && path.native().find('\0') == std::string::npos && limit>0 && limit<=privateFileByteLimit
    && !bytes.empty() && bytes.size() <= limit,
    "private_file_path_invalid");
  Descriptor directory(open("/", O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  Require(directory.fd >= 0, "private_file_parent_unavailable");
  for (auto const& component : path.parent_path().relative_path()) {
    Require(component != "." && component != ".." && !component.empty(), "private_file_path_invalid");
    const int next = openat(directory.fd, component.c_str(), O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    Require(next >= 0, "private_file_parent_unavailable");
    directory.reset(next);
  }
  struct stat parent{};
  Require(fstat(directory.fd, &parent) == 0 && parent.st_uid == geteuid()
    && (parent.st_mode & 077) == 0, "private_file_parent_permissions");
  const auto stage = ".private-stage-" + Random();
  Descriptor file(openat(directory.fd, stage.c_str(), O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600));
  Require(file.fd >= 0, "private_file_stage_failed");
  struct Stage {
    int parent; std::string const& name;
    ~Stage() { if (unlinkat(parent, name.c_str(), 0) != 0 && errno != ENOENT)
      std::cerr << "private_file_stage_cleanup_failed\n"; }
  } cleanup{directory.fd,stage};
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = write(file.fd, bytes.data()+offset, bytes.size()-offset);
    if (n < 0 && errno == EINTR) continue;
    Require(n > 0, "private_file_write_failed");
    offset += static_cast<std::size_t>(n);
  }
  Require(fsync(file.fd) == 0, "private_file_sync_failed");
  Require(linkat(directory.fd, stage.c_str(), directory.fd, path.filename().c_str(), 0) == 0,
    "private_file_exclusive_publish_failed");
  // The final name now exists. Never delete it on an uncertain completion.
  Require(unlinkat(directory.fd, stage.c_str(), 0) == 0 && fsync(directory.fd) == 0,
    "private_file_commit_uncertain");
}
PrivateFileCommit CompareExchangePrivateFile(const fs::path& path, std::string_view expected,
    std::string_view replacement, std::function<bool()> const& guard) {
  bool installed=false;
  try {
    Require(guard && !expected.empty() && !replacement.empty()
      && expected.size()<=1024*1024 && replacement.size()<=1024*1024,
      "private_file_exchange_invalid");
    // Also validates absolute normalized paths and custody before lock creation.
    PrivateText before{ReadPrivateFile(path,1024*1024)};
    if(before.value!=expected)return PrivateFileCommit::conflict;
    Descriptor directory(open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    Require(directory.fd>=0,"private_file_parent_unavailable");
    for(auto const& part:path.parent_path().relative_path()) {
      const int next=openat(directory.fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
      Require(next>=0,"private_file_parent_unavailable");directory.reset(next);
    }
    struct stat parent{};
    Require(fstat(directory.fd,&parent)==0 && parent.st_uid==geteuid()
      && (parent.st_mode&077)==0,"private_file_parent_permissions");
    const auto name=path.filename().string(),lock_name=".private-lock-"+name;
    Descriptor lock(openat(directory.fd,lock_name.c_str(),O_RDWR|O_CREAT|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC,0600));
    struct stat locked{};
    Require(lock.fd>=0 && fstat(lock.fd,&locked)==0 && S_ISREG(locked.st_mode)
      && locked.st_uid==geteuid() && (locked.st_mode&077)==0 && locked.st_nlink==1,
      "private_file_lock_invalid");
    if(flock(lock.fd,LOCK_EX|LOCK_NB)!=0) {
      if(errno==EWOULDBLOCK || errno==EAGAIN)return PrivateFileCommit::busy;
      return PrivateFileCommit::rejected;
    }
    const auto identities=[&] {
      struct stat p{},l{},lf{};
      return lstat(path.parent_path().c_str(),&p)==0 && S_ISDIR(p.st_mode)
        && p.st_dev==parent.st_dev && p.st_ino==parent.st_ino && p.st_uid==geteuid() && (p.st_mode&077)==0
        && fstatat(directory.fd,lock_name.c_str(),&l,AT_SYMLINK_NOFOLLOW)==0
        && fstat(lock.fd,&lf)==0 && l.st_dev==locked.st_dev && l.st_ino==locked.st_ino
        && lf.st_nlink==1 && lf.st_uid==geteuid() && (lf.st_mode&077)==0;
    };
    Descriptor old(openat(directory.fd,name.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));
    struct stat old_stat{};
    Require(old.fd>=0 && fstat(old.fd,&old_stat)==0,"private_file_changed");
    const auto current=[&] {
      PrivateText actual{ReadPrivateFile(path,1024*1024)};
      struct stat selected{};
      return identities() && actual.value==expected
        && fstatat(directory.fd,name.c_str(),&selected,AT_SYMLINK_NOFOLLOW)==0
        && selected.st_dev==old_stat.st_dev && selected.st_ino==old_stat.st_ino
        && selected.st_size==old_stat.st_size && unchanged_time(old_stat,selected);
    };
    if(!current())return PrivateFileCommit::conflict;
    if(expected==replacement) {
      if(!guard())return PrivateFileCommit::rejected;
      if(!current())return PrivateFileCommit::conflict;
      if(fsync(old.fd)!=0 || fsync(directory.fd)!=0)return PrivateFileCommit::uncertain;
      return current()?PrivateFileCommit::confirmed:PrivateFileCommit::uncertain;
    }
    const auto stage=".private-stage-"+Random();
    Descriptor staged(openat(directory.fd,stage.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    Require(staged.fd>=0,"private_file_stage_failed");
    struct Stage {
      int parent; std::string const& name;
      ~Stage(){if(unlinkat(parent,name.c_str(),0)!=0 && errno!=ENOENT)
        std::cerr<<"private_file_stage_cleanup_failed\n";}
    } cleanup{directory.fd,stage};
    for(std::size_t offset=0;offset<replacement.size();) {
      const auto n=write(staged.fd,replacement.data()+offset,replacement.size()-offset);
      if(n<0 && errno==EINTR)continue;
      Require(n>0,"private_file_write_failed");offset+=static_cast<std::size_t>(n);
    }
    Require(fsync(staged.fd)==0,"private_file_sync_failed");
    if(!guard())return PrivateFileCommit::rejected;
    if(!current())return PrivateFileCommit::conflict;
    struct stat stage_fd{},stage_path{};
    Require(fstat(staged.fd,&stage_fd)==0
      && fstatat(directory.fd,stage.c_str(),&stage_path,AT_SYMLINK_NOFOLLOW)==0
      && stage_fd.st_dev==stage_path.st_dev && stage_fd.st_ino==stage_path.st_ino
      && S_ISREG(stage_fd.st_mode) && stage_fd.st_uid==geteuid()
      && stage_fd.st_nlink==1 && (stage_fd.st_mode&077)==0
      && static_cast<std::uint64_t>(stage_fd.st_size)==replacement.size(),"private_file_stage_changed");
    Require(renameat(directory.fd,stage.c_str(),directory.fd,name.c_str())==0,"private_file_exchange_failed");
    installed=true;
    if(fsync(directory.fd)!=0 || !identities())return PrivateFileCommit::uncertain;
    PrivateText after{ReadPrivateFile(path,1024*1024)};
    return after.value==replacement?PrivateFileCommit::confirmed:PrivateFileCommit::uncertain;
  } catch(std::exception const&) {
    return installed?PrivateFileCommit::uncertain:PrivateFileCommit::rejected;
  }
}
}
