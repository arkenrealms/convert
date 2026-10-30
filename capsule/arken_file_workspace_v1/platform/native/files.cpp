#include "arken/files/workspace/v1/native_files.hpp"
#include "arken/files/workspace/v1/policy.hpp"
#include "filesystem.hpp"
#include <array>
#include <iostream>
#include <sys/random.h>

namespace arken::files::workspace::v1 {
namespace {
using namespace native_detail;
std::vector<std::string> parts(std::string_view path) {
  require(!path.empty() && path.size() <= 4096, "SecurityError", "Invalid relative file path.");
  std::vector<std::string> result;
  for (std::size_t offset = 0;;) {
    const auto end = path.find('/', offset);
    result.emplace_back(path.substr(offset, end == path.npos ? path.size()-offset : end-offset));
    if (end == path.npos) break;
    offset = end+1;
  }
  require(valid_relative_path(result), "SecurityError", "Invalid relative file path.");
  return result;
}
struct Stage {
  int parent;
  std::string name;
  struct stat owned{};
  bool admitted=false;
  Fd file;
  bool matches(const struct stat& info) const noexcept {
    return admitted && info.st_dev==owned.st_dev && info.st_ino==owned.st_ino;
  }
  ~Stage() {
    if (name.empty() || !admitted) return;
    struct stat info{};
    if (::fstatat(parent,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)==0 && matches(info))
      if (::unlinkat(parent,name.c_str(),0)!=0) std::clog << "[workspace-files] owned stage retained\n";
  }
};
}
struct NativeFiles::Impl {
  std::filesystem::path path;
  native_detail::Fd root;
  std::string root_identity;
};
NativeFiles::NativeFiles(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
NativeFiles::~NativeFiles() = default;
std::unique_ptr<NativeFiles> NativeFiles::open(const std::filesystem::path& path,std::string& error) noexcept {
  try {
    error.clear(); auto state=std::make_unique<Impl>();
    state->root=native_detail::open_root(path); state->path=path;
    state->root_identity=native_detail::identity(native_detail::stat_fd(state->root.value));
    return std::unique_ptr<NativeFiles>(new NativeFiles(std::move(state)));
  } catch(const std::exception&) { error="Choose an existing physical folder."; return {}; }
}
NativeFileRead NativeFiles::read(std::string_view path,std::size_t maximum_bytes) noexcept {
  try {
    require(maximum_bytes>0 && maximum_bytes<=maximum_file_bytes,"RangeError","Invalid read budget.");
    const auto names=parts(path);
    auto parent=directory(impl_->path,impl_->root_identity,{names.begin(),names.end()-1});
    Fd file(::openat(parent.value,names.back().c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    io(file.value>=0);const auto before=stat_fd(file.value);
    require(S_ISREG(before.st_mode),"TypeMismatchError","Choose a regular file.");
    require(before.st_size>0 && static_cast<std::uint64_t>(before.st_size)<=maximum_bytes,
        "RangeError","Input file exceeds the admitted byte limit.");
    NativeFileRead result;result.bytes.resize(static_cast<std::size_t>(before.st_size));
    for(std::size_t done=0;done<result.bytes.size();) {
      const auto count=::pread(file.value,result.bytes.data()+done,result.bytes.size()-done,static_cast<off_t>(done));
      if(count<0&&errno==EINTR)continue;
      io(count>0);done+=static_cast<std::size_t>(count);
    }
    require(version(stat_fd(file.value))==version(before),"InvalidModificationError","Input changed during reading.");
    (void)directory(impl_->path,impl_->root_identity,{});
    return result;
  } catch(const Failure& failure) { return {{},failure.name}; }
    catch(const std::exception&) { return {{},"FileReadFailed"}; }
}
NativeFileCreate NativeFiles::create(std::string_view path,std::span<const std::byte> bytes) noexcept {
  NativeFileCreate result;
  try {
    require(!bytes.empty()&&bytes.size()<=maximum_file_bytes,"RangeError","Invalid output size.");
    const auto names=parts(path);const std::vector<std::string> parents(names.begin(),names.end()-1);
    auto parent=directory(impl_->path,impl_->root_identity,parents);
    const auto parent_identity=identity(stat_fd(parent.value));
    std::array<unsigned char,16> random{};io(::getentropy(random.data(),random.size())==0);
    Stage stage{parent.value,".arken-write-",{},false,Fd{}};
    constexpr char hex[]="0123456789abcdef";
    for(const auto value:random){stage.name+=hex[value>>4];stage.name+=hex[value&15];}
    stage.file=Fd(::openat(parent.value,stage.name.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
    io(stage.file.value>=0);stage.owned=stat_fd(stage.file.value);stage.admitted=true;
    write_at(stage.file.value,{reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},0);
    io(::fsync(stage.file.value)==0);
    auto current=directory(impl_->path,impl_->root_identity,parents);
    require(identity(stat_fd(current.value))==parent_identity,"InvalidModificationError","Output parent changed.");
    struct stat staged{};io(::fstatat(parent.value,stage.name.c_str(),&staged,AT_SYMLINK_NOFOLLOW)==0);
    require(stage.matches(staged),"InvalidModificationError","Output staging file changed.");
    // linkat is exclusive: an existing regular file, directory or symlink is
    // never replaced. The fully written inode becomes visible in one step.
    io(::linkat(parent.value,stage.name.c_str(),parent.value,names.back().c_str(),0)==0);
    result.published=true;
    struct stat output{};io(::fstatat(parent.value,names.back().c_str(),&output,AT_SYMLINK_NOFOLLOW)==0);
    require(stage.matches(output),"InvalidModificationError","Published output identity changed.");
    io(::fstatat(parent.value,stage.name.c_str(),&staged,AT_SYMLINK_NOFOLLOW)==0);
    require(stage.matches(staged),"InvalidModificationError","Output staging identity changed before cleanup.");
    io(::unlinkat(parent.value,stage.name.c_str(),0)==0);stage.name.clear();
    io(::fsync(parent.value)==0);
    auto final_parent=directory(impl_->path,impl_->root_identity,parents);
    require(identity(stat_fd(final_parent.value))==parent_identity,"InvalidModificationError","Published output parent moved.");
    result.durable=true;
  } catch(const Failure& failure) { result.error=failure.name; }
    catch(const std::exception&) { result.error="FileCreateFailed"; }
  return result;
}
} // namespace arken::files::workspace::v1
