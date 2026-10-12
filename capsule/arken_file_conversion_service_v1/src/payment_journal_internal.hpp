#pragma once
#include <algorithm>
#include <string_view>
namespace arken::file_conversion::service::v1::payment::detail {
inline bool JournalOperationKey(std::string_view key) {
  return key.size() == 74 && key.starts_with("operation/") &&
    std::all_of(key.begin() + 10, key.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}
