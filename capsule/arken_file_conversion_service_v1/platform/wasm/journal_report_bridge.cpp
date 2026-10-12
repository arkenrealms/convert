#include "arken_file_conversion_service_v1/payment_journal.hpp"
#include <algorithm>
#include <cstdint>

namespace {
namespace P = arken::file_conversion::service::v1::payment;
std::string input, output;
bool prepared = false, committed = false;
void Clear(std::string& value) {
  volatile char* bytes = value.data();
  for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = '\0';
  std::string{}.swap(value);
}
}
#define REPORT_EXPORT extern "C" __attribute__((used, visibility("default")))
REPORT_EXPORT void arken_convert_report_clear() noexcept {
  Clear(input); Clear(output); prepared = committed = false;
}
REPORT_EXPORT char* arken_convert_report_prepare(std::uint32_t size) noexcept {
  arken_convert_report_clear();
  if (!size || size > P::journalMaximumPacketBytes) return nullptr;
  try { input.resize(size); prepared = true; return input.data(); }
  catch (...) { return nullptr; }
}
REPORT_EXPORT bool arken_convert_report_commit(std::uint32_t size) noexcept {
  if (!prepared || committed || !size || size > input.size()) {
    arken_convert_report_clear(); return false;
  }
  input.resize(size); committed = true; return true;
}
REPORT_EXPORT std::uint32_t arken_convert_report_run(std::uint32_t mode) noexcept {
  if (!committed || (mode != 9 && mode != 10)) { arken_convert_report_clear(); return 0; }
  prepared = committed = false;
  output = mode == 9 ? P::JournalReadPlanPacket(input) : P::JournalProjectPacket(input);
  Clear(input);
  if (output.size() > P::journalMaximumPacketBytes) { arken_convert_report_clear(); return 0; }
  return static_cast<std::uint32_t>(output.size());
}
REPORT_EXPORT const char* arken_convert_report_result() noexcept { return output.data(); }
REPORT_EXPORT std::uint32_t arken_convert_report_limit() noexcept { return P::journalMaximumPacketBytes; }
#undef REPORT_EXPORT
