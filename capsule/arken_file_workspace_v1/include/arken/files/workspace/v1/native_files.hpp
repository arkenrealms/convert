#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arken::files::workspace::v1 {
struct NativeFileRead {
  std::vector<std::byte> bytes;
  std::string error;
  explicit operator bool() const noexcept { return error.empty(); }
};
struct NativeFileCreate {
  bool published = false;
  bool durable = false;
  std::string error;
  explicit operator bool() const noexcept { return published && durable && error.empty(); }
};
// Typed native access to an independently authorized physical root. No picker,
// process, network, browser connection or ambient/current-directory authority.
// Serialize calls; retain this capability only for its owning session.
class NativeFiles final {
public:
  static std::unique_ptr<NativeFiles> open(const std::filesystem::path&, std::string& error) noexcept;
  ~NativeFiles();
  NativeFiles(const NativeFiles&) = delete;
  NativeFiles& operator=(const NativeFiles&) = delete;
  NativeFileRead read(std::string_view relative_path, std::size_t maximum_bytes) noexcept;
  // Existing parents only. Never overwrite any destination. A failure after
  // publication retains published=true: inspect the output before retrying.
  NativeFileCreate create(std::string_view relative_path, std::span<const std::byte>) noexcept;
private:
  struct Impl;
  explicit NativeFiles(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace arken::files::workspace::v1
