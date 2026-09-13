#pragma once
#include "arken_file_conversion_service_v1/payment_profile.hpp"
#include <string>
namespace arken::file_conversion::service::v1::payment {
struct ReconciliationEndpoint {std::string url,error;};
ReconciliationEndpoint ResolveReconciliationEndpoint(const PaymentProfile&,std::optional<std::string_view> clientKey);
}
