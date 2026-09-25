#include "arken_file_conversion_service_v1/provider_authorization.hpp"
#include <initializer_list>
namespace arken::file_conversion::service::v1::payment {
std::optional<CdpEndpoint> CdpEndpointFor(CdpOperation operation) noexcept {
  try {
    switch (operation) {
    case CdpOperation::supported:
      return CdpEndpoint{
          "GET", "https://api.cdp.coinbase.com/platform/v2/x402/supported"};
    case CdpOperation::verify:
      return CdpEndpoint{
          "POST", "https://api.cdp.coinbase.com/platform/v2/x402/verify"};
    case CdpOperation::settle:
      return CdpEndpoint{
          "POST", "https://api.cdp.coinbase.com/platform/v2/x402/settle"};
    default:
      return std::nullopt;
    }
  } catch (...) {
    return std::nullopt;
  }
}
std::optional<CdpOperation> CdpOperationFor(std::string_view method,
                                            std::string_view url) noexcept {
  for (auto operation :
       {CdpOperation::supported, CdpOperation::verify, CdpOperation::settle}) {
    const auto endpoint = CdpEndpointFor(operation);
    if (endpoint && endpoint->method == method && endpoint->url == url)
      return operation;
  }
  return std::nullopt;
}
} // namespace arken::file_conversion::service::v1::payment
