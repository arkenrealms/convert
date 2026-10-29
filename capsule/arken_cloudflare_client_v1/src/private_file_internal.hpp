#pragma once
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace arken::cloudflare::v1::private_file_detail {
struct Descriptor {
  int fd = -1;
  explicit Descriptor(int value) : fd(value) {}
  ~Descriptor() { if (fd >= 0) close(fd); }
  Descriptor(Descriptor const&) = delete;
  Descriptor& operator=(Descriptor const&) = delete;
  void reset(int value) { if (fd >= 0) close(fd); fd = value; }
};
struct PrivateText {
  std::string value;
  ~PrivateText() { volatile char* p = value.data(); for (std::size_t i=0;i<value.size();++i) p[i]=0; }
};
inline bool unchanged_time(struct stat const& before, struct stat const& after) {
#if defined(__APPLE__)
  return before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec
    && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec
    && before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec
    && before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
#else
  return before.st_mtim.tv_sec == after.st_mtim.tv_sec
    && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
    && before.st_ctim.tv_sec == after.st_ctim.tv_sec
    && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
#endif
}
}
