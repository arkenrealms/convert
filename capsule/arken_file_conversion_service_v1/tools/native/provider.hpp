#pragma once
#include "platform.hpp"
#include "arken_file_conversion_service_v1/provider_native.hpp"
namespace arken::conversion::cloudflare::provider {
using arken::file_conversion::service::v1::payment::CdpOperation;
using arken::file_conversion::service::v1::payment::SignCdpRequest;
Json Observe(const Transport&,const fs::path& credential,std::uint64_t now,std::string_view nonce);
void Tests(const fs::path&);
}
