#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/x402.hpp"
#include <cstdint>
#include <string>
#define EXPORT __attribute__((used,visibility("default")))
namespace { std::string input, output; }
extern "C" EXPORT char* arken_mcp_prepare(std::uint32_t bytes) noexcept {
    if(bytes>16384)return nullptr;
    try{input.assign(bytes,'\0');output.clear();return input.data();}
    catch(...){return nullptr;}
}
extern "C" EXPORT void arken_mcp_plan() noexcept {output=arken::file_conversion::service::v1::PlanRequest(input);}
extern "C" EXPORT void arken_payment_advance() noexcept {output=arken::file_conversion::service::v1::payment::AdvancePacket(input);}
extern "C" EXPORT void arken_payment_storage_lookup() noexcept {output=arken::file_conversion::service::v1::payment::StorageLookupPacket(input);}
extern "C" EXPORT void arken_payment_storage_advance() noexcept {output=arken::file_conversion::service::v1::payment::StorageAdvancePacket(input);}
extern "C" EXPORT void arken_x402_packet() noexcept {output=arken::file_conversion::service::v1::payment::X402Packet(input);}
extern "C" EXPORT void arken_payment_storage_dispatch() noexcept {output=arken::file_conversion::service::v1::payment::StorageDispatchPacket(input);}
extern "C" EXPORT const char* arken_mcp_output() noexcept{return output.data();}
extern "C" EXPORT std::uint32_t arken_mcp_output_size() noexcept{return static_cast<std::uint32_t>(output.size());}
