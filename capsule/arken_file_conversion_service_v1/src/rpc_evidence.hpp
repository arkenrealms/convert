#pragma once
#include "payment_json.hpp"
#include <map>
namespace arken::file_conversion::service::v1::payment::rpc {
std::string Data(const detail::Json&,std::size_t bytes);
std::uint64_t Quantity(const detail::Json&);
std::string Quantity(std::uint64_t);
std::string Hash(const detail::Json&);
struct Block {std::uint64_t number,time;std::string hash;bool operator==(const Block&)const=default;};
Block Header(const detail::Json&);
std::map<unsigned,detail::Json> Replies(const detail::Json&,std::initializer_list<unsigned>);
}
