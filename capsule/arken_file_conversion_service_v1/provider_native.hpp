#pragma once
#include "provider_authorization.hpp"
namespace arken::file_conversion::service::v1::payment {
// Native Ed25519 authentication for the fixed CDP endpoint catalog. This does
// not sign wallet transactions or payment authorizations. Fixed errors throw.
std::string SignCdpRequest(std::string_view keyId, std::string_view secret,
    CdpOperation operation, std::uint64_t now, std::string_view nonce);
}
