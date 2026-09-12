#pragma once
#include "arken_file_conversion_service_v1/payment.hpp"
#include <nlohmann/json.hpp>
#include <string_view>
namespace arken::file_conversion::service::v1::payment::detail {
using Json=nlohmann::json;
Json Parse(std::string_view);
void Fields(const Json&,std::initializer_list<std::string_view>);
Quote ReadQuote(const Json&);
Operation ReadOperation(const Json&);
Output ReadOutput(const Json&);
Json WriteQuote(const Quote&);
Json WriteEvent(const Event&);
Digest PayloadDigest(std::string_view);
}
