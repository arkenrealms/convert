#include "arken_cloudflare_client_v1/api.hpp"
#include <stdexcept>
namespace arken::cloudflare::v1 {
void Require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
}
