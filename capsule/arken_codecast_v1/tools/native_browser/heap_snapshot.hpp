#pragma once
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace arken::codecast::native_browser {
// Raw heap strings can include private runtime data. Stream only to a new,
// owner-readable file; never retain chunks in the diagnostic JSON response.
class HeapSnapshotRecorder {
  int file_ = -1;
  std::size_t bytes_ = 0, chunks_ = 0;
  const std::size_t limit_;

public:
  explicit HeapSnapshotRecorder(std::size_t limit = 2u * 1024u * 1024u * 1024u)
      : limit_(limit) {}
  ~HeapSnapshotRecorder() { Cancel(); }
  HeapSnapshotRecorder(HeapSnapshotRecorder const &) = delete;
  HeapSnapshotRecorder &operator=(HeapSnapshotRecorder const &) = delete;
  bool active() const { return file_ >= 0; }
  void Begin(std::filesystem::path const &directory) {
    if (active())
      throw std::runtime_error("Heap snapshot already active");
    file_ = ::open((directory / "page.heapsnapshot").c_str(),
                   O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file_ < 0)
      throw std::runtime_error("Cannot exclusively create heap snapshot");
    bytes_ = chunks_ = 0;
  }
  void Chunk(std::string_view text) {
    if (!active())
      throw std::runtime_error("No active heap snapshot");
    if (text.size() > limit_ - bytes_)
      throw std::runtime_error("Heap snapshot exceeds its byte budget");
    for (std::size_t offset = 0; offset < text.size();) {
      const auto count =
          ::write(file_, text.data() + offset, text.size() - offset);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        throw std::runtime_error("Heap snapshot write failed");
      offset += static_cast<std::size_t>(count);
    }
    bytes_ += text.size();
    ++chunks_;
  }
  nlohmann::json Finish() {
    if (!active() || !bytes_)
      throw std::runtime_error("Heap snapshot is empty or inactive");
    const auto file = file_;
    file_ = -1;
    if (::close(file) != 0)
      throw std::runtime_error("Heap snapshot close failed");
    return {{"file", "page.heapsnapshot"}, {"bytes", bytes_},
            {"chunks", chunks_},           {"maximumBytes", limit_},
            {"diagnosticOnly", true},      {"garbageCollectionMayOccur", true}};
  }
  void Cancel() noexcept {
    if (file_ >= 0) {
      ::close(file_);
      file_ = -1;
    }
  }
};
} // namespace arken::codecast::native_browser
