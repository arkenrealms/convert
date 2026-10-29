#include "arken_cloudflare_client_v1/api.hpp"
#include "arken/digest/v1/digest.hpp"
#include <array>
#include <span>

namespace arken::cloudflare::v1 {
std::string Hash(std::string_view bytes) {
  const auto digest=arken::digest::v1::sha256(std::as_bytes(std::span(bytes.data(),bytes.size())));
  Require(digest.has_value(),"hash_failed");
  std::array<char,71> out{};
  Require(arken::digest::v1::format_sha256(*digest,out),"hash_format_failed");
  return std::string(out.data()+7,64);
}
}
