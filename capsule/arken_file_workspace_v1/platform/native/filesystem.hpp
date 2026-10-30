#pragma once
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace arken::files::workspace::v1::native_detail {
struct Failure : std::runtime_error {
  std::string name;
  Failure(std::string n, std::string message)
      : std::runtime_error(std::move(message)), name(std::move(n)) {}
};
inline void require(bool value, const char* name, const char* message) {
  if (!value) throw Failure(name, message);
}
inline void io(bool value) {
  if (value) return;
  const int code = errno;
  throw Failure(code == ENOENT ? "NotFoundError"
      : code == ENOTDIR || code == EISDIR ? "TypeMismatchError"
      : code == EEXIST || code == EWOULDBLOCK ? "InvalidModificationError" : "NotAllowedError",
      "The workspace filesystem operation was rejected.");
}
struct Fd {
  int value = -1;
  explicit Fd(int v = -1) : value(v) {}
  ~Fd() { if (value >= 0) ::close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) { if (value >= 0) ::close(value); value = std::exchange(other.value, -1); }
    return *this;
  }
};
inline struct stat stat_fd(int fd) { struct stat info{}; io(::fstat(fd, &info) == 0); return info; }
inline std::string identity(const struct stat& info) {
  return std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino);
}
inline std::string version(const struct stat& info) {
#if defined(__APPLE__)
  const auto modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
  const auto modified = info.st_mtim, changed = info.st_ctim;
#endif
  return identity(info) + ":" + std::to_string(info.st_size) + ":"
      + std::to_string(modified.tv_sec) + ":" + std::to_string(modified.tv_nsec) + ":"
      + std::to_string(changed.tv_sec) + ":" + std::to_string(changed.tv_nsec);
}
inline Fd physical_directory(const std::filesystem::path& path) {
  require(path.is_absolute() && path.lexically_normal() == path, "SecurityError", "A physical absolute root is required.");
  Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  io(current.value >= 0);
  for (const auto& part : path.relative_path()) {
    Fd child(::openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    io(child.value >= 0); current = std::move(child);
  }
  return current;
}
inline Fd open_root(const std::filesystem::path& path) {
  require(path.is_absolute() && path != path.root_path() && path == std::filesystem::canonical(path),
      "SecurityError", "Select an authorized physical workspace root.");
  return physical_directory(path);
}
inline Fd directory(const std::filesystem::path& root, const std::string& root_identity,
                    const std::vector<std::string>& parts) {
  auto current = physical_directory(root);
  require(identity(stat_fd(current.value)) == root_identity, "NotAllowedError",
      "The selected root changed; reconnect the folder.");
  for (const auto& part : parts) {
    Fd child(::openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    io(child.value >= 0); current = std::move(child);
  }
  return current;
}
inline void write_at(int fd, std::span<const std::uint8_t> bytes, std::uint64_t offset) {
  for (std::size_t done = 0; done < bytes.size();) {
    const auto count = ::pwrite(fd, bytes.data() + done, bytes.size() - done, static_cast<off_t>(offset + done));
    if (count < 0 && errno == EINTR) continue;
    io(count > 0); done += static_cast<std::size_t>(count);
  }
}
} // namespace arken::files::workspace::v1::native_detail
