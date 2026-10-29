#include "arken_cloudflare_client_v1/api.hpp"
#include "private_file_internal.hpp"
#include <cerrno>
#include <fcntl.h>
namespace arken::cloudflare::v1 {
using namespace private_file_detail;
std::string ReadPrivateFile(const fs::path& path, std::size_t limit) {
  Require(path.is_absolute() && path == path.lexically_normal() && !path.filename().empty()
    && path.native().find('\0') == std::string::npos && limit > 0 && limit <= privateFileByteLimit,
    "private_file_path_invalid");
  Descriptor directory(open("/", O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  Require(directory.fd >= 0, "private_file_parent_unavailable");
  const auto parent = path.parent_path().relative_path();
  for (auto const& component : parent) {
    Require(component != "." && component != ".." && !component.empty(), "private_file_path_invalid");
    const int next = openat(directory.fd, component.c_str(), O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    Require(next >= 0, "private_file_parent_unavailable");
    directory.reset(next);
  }
  struct stat parent_stat{};
  Require(fstat(directory.fd, &parent_stat) == 0 && parent_stat.st_uid == geteuid()
    && (parent_stat.st_mode & 077) == 0, "private_file_parent_permissions");
  Descriptor file(openat(directory.fd, path.filename().c_str(), O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));
  Require(file.fd >= 0, "private_file_unavailable");
  struct stat before{};
  Require(fstat(file.fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_uid == geteuid()
    && (before.st_mode & 077) == 0 && before.st_nlink == 1 && before.st_size > 0
    && static_cast<std::uint64_t>(before.st_size) <= limit, "private_file_identity_or_permissions");
  PrivateText text{std::string(static_cast<std::size_t>(before.st_size), '\0')};
  std::size_t offset = 0;
  while (offset < text.value.size()) {
    const auto n = read(file.fd, text.value.data()+offset, text.value.size()-offset);
    if (n < 0 && errno == EINTR) continue;
    Require(n > 0, "private_file_read_failed");
    offset += static_cast<std::size_t>(n);
  }
  char extra = 0;
  ssize_t tail;
  do { tail = read(file.fd, &extra, 1); } while (tail < 0 && errno == EINTR);
  struct stat after{}, selected{};
  Require(tail == 0 && fstat(file.fd, &after) == 0
    && fstatat(directory.fd, path.filename().c_str(), &selected, AT_SYMLINK_NOFOLLOW) == 0
    && after.st_dev == selected.st_dev && after.st_ino == selected.st_ino
    && after.st_size == before.st_size && after.st_uid == before.st_uid
    && after.st_mode == before.st_mode && after.st_nlink == 1
    && unchanged_time(before, after),
    "private_file_changed");
  // Copy into the caller's owned result; the read buffer is wiped on all exits.
  return text.value;
}
}
