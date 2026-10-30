#pragma once
#include "arken_cloudflare_client_v1/qualification.hpp"
namespace arken::cloudflare::v1::resource_detail {
using Execute=std::function<void(const fs::path&,const std::vector<std::string>&,const fs::path&,unsigned)>;
Json Qualify(const WorkerResourceInput&,const Execute&);
}
