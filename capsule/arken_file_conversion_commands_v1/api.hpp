#pragma once
#include "arken_mcp_protocol_v1/api.hpp"
#include <filesystem>
#include <memory>

namespace arken::file_conversion::commands::v1 {
std::string catalog();
class Session final {
public:
  static std::unique_ptr<Session> open(const std::filesystem::path& authorized_root,std::string& error) noexcept;
  ~Session();
  Session(const Session&)=delete;
  Session& operator=(const Session&)=delete;
  arken::mcp::v1::Json request(std::string_view) noexcept;
private:
  struct Impl;
  explicit Session(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
};
}
