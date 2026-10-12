#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace arken::paid_operation::v1 {
// Pure policy only. The native caller parses bounded packets, validates its
// configuration and computes both observed SHA-256 values; never accept those
// observations from a request. No token store, clock or platform dependency.
struct ReportReadGrant {
  std::string schema, id, credentialDigest, configurationDigest;
  std::uint64_t notBefore = 0, expires = 0;
};
struct ReportReadRequest {
  std::string method, path, origin, contentType, authorization;
  bool hasOrigin = false;
};
inline bool ReportIdentifier(std::string_view value) noexcept {
  return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}
inline bool AdmitReportRead(const ReportReadGrant& grant, const ReportReadRequest& request,
    std::string_view schema, std::string_view path, std::string_view origin,
    std::string_view configurationDigest, std::string_view credentialDigest,
    std::uint64_t now) noexcept {
  if (grant.schema != schema || !ReportIdentifier(grant.id) ||
      !ReportIdentifier(grant.configurationDigest) || grant.configurationDigest != configurationDigest ||
      !ReportIdentifier(grant.credentialDigest) || !ReportIdentifier(credentialDigest) ||
      grant.notBefore == 0 || grant.notBefore >= grant.expires || grant.expires > 4102444800ULL ||
      grant.expires - grant.notBefore > 31u * 86400u || now < grant.notBefore || now >= grant.expires ||
      !origin.starts_with("https://") || request.origin != origin || request.path != path ||
      request.method != "POST" || request.hasOrigin || request.contentType != "application/json" ||
      request.authorization.size() != 71 || !request.authorization.starts_with("Bearer ") ||
      !ReportIdentifier(std::string_view(request.authorization).substr(7))) return false;
  unsigned difference = 0;
  for (std::size_t i = 0; i < 64; ++i)
    difference |= static_cast<unsigned char>(grant.credentialDigest[i] ^ credentialDigest[i]);
  return difference == 0;
}
} // namespace arken::paid_operation::v1
