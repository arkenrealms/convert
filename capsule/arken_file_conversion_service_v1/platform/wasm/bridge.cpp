#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_service_v1/payment_packet.hpp"
#include "arken_file_conversion_service_v1/x402.hpp"
#include "arken_file_conversion_service_v1/paid_conversion.hpp"
#include "arken_file_conversion_service_v1/settlement_reconciliation.hpp"
#include "arken_file_conversion_service_v1/public_admission.hpp"
#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include "arken_file_conversion_service_v1/production_startup.hpp"
#include "arken_file_conversion_service_v1/endpoint.hpp"
#include <cstdint>
#include <string>
#define EXPORT __attribute__((used,visibility("default")))
namespace { std::string input, output;
char* Prepare(std::uint32_t bytes,std::uint32_t limit) noexcept {
    if(bytes>limit)return nullptr;
    try{input.assign(bytes,'\0');output.clear();return input.data();}
    catch(...){return nullptr;}
}
}
extern "C" EXPORT char* arken_mcp_prepare(std::uint32_t bytes) noexcept {return Prepare(bytes,16384);}
extern "C" EXPORT char* arken_settlement_prepare(std::uint32_t bytes) noexcept {return Prepare(bytes,1048576);}
extern "C" EXPORT void arken_mcp_plan() noexcept {output=arken::file_conversion::service::v1::PlanRequest(input);}
extern "C" EXPORT void arken_mcp_sandbox_plan() noexcept {output=arken::file_conversion::service::v1::PlanRequest(input,arken::file_conversion::service::v1::ServerMode::sandbox);}
extern "C" EXPORT void arken_mcp_production_plan() noexcept {output=arken::file_conversion::service::v1::PlanRequest(input,arken::file_conversion::service::v1::ServerMode::production);}
extern "C" EXPORT void arken_mcp_call_summary(int status) noexcept {output=arken::file_conversion::service::v1::CallSummary(input,status);}
extern "C" EXPORT void arken_server_surface() noexcept {output=arken::file_conversion::service::v1::SurfacePacket(input);}
extern "C" EXPORT void arken_conversion_endpoint() noexcept {output=arken::file_conversion::service::v1::EndpointPacket(input);}
extern "C" EXPORT void arken_conversion_target() noexcept {output=arken::file_conversion::service::v1::ConversionTargetPacket(input);}
extern "C" EXPORT void arken_production_startup() noexcept {output=arken::file_conversion::service::v1::ProductionStartupPacket(input);}
extern "C" EXPORT void arken_production_qualification() noexcept {output=arken::file_conversion::service::v1::ProductionQualificationPacket(input);}
extern "C" EXPORT void arken_paid_conversion_packet() noexcept {output=arken::file_conversion::service::v1::payment::PaidConversionPacket(input);}
extern "C" EXPORT void arken_settlement_reconciliation_packet() noexcept {output=arken::file_conversion::service::v1::payment::SettlementReconciliationPacket(input);}
extern "C" EXPORT void arken_public_admission_packet() noexcept {output=arken::file_conversion::service::v1::PublicAdmissionPacket(input);}
extern "C" EXPORT void arken_cdp_provider_packet() noexcept {output=arken::file_conversion::service::v1::payment::CdpProviderPacket(input);}
// Erases the explicit ABI scratch, not allocator copies or JS secret strings.
extern "C" EXPORT void arken_mcp_clear() noexcept {
    for(auto* text:{&input,&output}){volatile char* p=text->data();for(std::size_t i=0;i<text->size();++i)p[i]=0;text->clear();}
}
extern "C" EXPORT void arken_payment_advance() noexcept {output=arken::file_conversion::service::v1::payment::AdvancePacket(input);}
extern "C" EXPORT void arken_payment_storage_lookup() noexcept {output=arken::file_conversion::service::v1::payment::StorageLookupPacket(input);}
extern "C" EXPORT void arken_payment_storage_advance() noexcept {output=arken::file_conversion::service::v1::payment::StorageAdvancePacket(input);}
extern "C" EXPORT void arken_x402_packet() noexcept {output=arken::file_conversion::service::v1::payment::X402Packet(input);}
extern "C" EXPORT void arken_payment_storage_dispatch() noexcept {output=arken::file_conversion::service::v1::payment::StorageDispatchPacket(input);}
extern "C" EXPORT void arken_payment_storage_recover() noexcept {output=arken::file_conversion::service::v1::payment::StorageRecoveryPacket(input);}
extern "C" EXPORT const char* arken_mcp_output() noexcept{return output.data();}
extern "C" EXPORT std::uint32_t arken_mcp_output_size() noexcept{return static_cast<std::uint32_t>(output.size());}
