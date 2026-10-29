#pragma once
#include "arken_cloudflare_client_v1/worker.hpp"

namespace arken::paid::cloudflare::v1 {
namespace C=::arken::cloudflare::v1;
// Immutable local conformance package. No deployment or predecessor authority.
// source-inputs.txt records the files actually read, for native build discovery.
C::Json PrepareSourcePackageFiles(std::string_view family, std::string_view mainModule,
    const C::fs::path& source, const std::vector<C::WorkerModule>& modules,
    const C::fs::path& newOutput);
}
