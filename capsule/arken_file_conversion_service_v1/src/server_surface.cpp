#include "arken_file_conversion_service_v1/server_surface.hpp"
#include "payment_json.hpp"
namespace arken::file_conversion::service::v1 {
ServerSurface Surface(ServerMode mode){switch(mode){
  case ServerMode::preview:return {mode,"","authenticated-preview","preview","","Authenticated conversion preview. Prepare an upload, PUT the file bytes, then convert. No live charges are enabled.",900000,false,false};
  case ServerMode::sandbox:return {mode,"base-sepolia-v1","x402-sandbox","sandbox","","Public x402 sandbox. Prepare an upload, keep file_token private, PUT bytes to upload_url, then convert using file_id and file_token. Public preparations are limited to 64 files and 64 MiB per UTC day. Payments use test USDC on Base Sepolia; mainnet payments are disabled.",900000,true,true};
  case ServerMode::production:return {mode,"base-mainnet-v1","x402-production","production","production-v1:","Arken Convert production endpoint. New uploads and payment offers require enabled, validated provider configuration. Discovery is available independently. Prepare an upload, keep file_token private, PUT bytes to upload_url, then convert with file_id and file_token. Payment offers use USDC on Base mainnet. Public preparations are limited to 64 files and 64 MiB per UTC day. Existing paid results remain accessible while new admission is disabled.",3600000,true,false};
  default:throw std::invalid_argument("unknown server mode");
}}
std::string SurfacePacket(std::string_view text) noexcept {try{
  const auto j=payment::detail::Parse(text);payment::detail::Fields(j,{"profileId","ready"});
  ServerMode mode;if(j.at("profileId").is_null())mode=ServerMode::preview;else if(j.at("profileId")=="base-sepolia-v1")mode=ServerMode::sandbox;else if(j.at("profileId")=="base-mainnet-v1")mode=ServerMode::production;else throw std::invalid_argument("unknown profile");
  if(!j.at("ready").is_boolean())throw std::invalid_argument("readiness must be boolean");const auto surface=Surface(mode);
  return payment::detail::Json{{"mode",surface.displayMode},{"artifactPrefix",surface.artifactPrefix},{"downloadDomain",surface.downloadDomain},{"accessMilliseconds",surface.accessMilliseconds},
    {"publicAccess",surface.publicAccess},{"testnetPayments",surface.testnet},{"charging",mode==ServerMode::production&&j.at("ready")==true}}.dump();
}catch(...){return "{\"packetError\":\"invalid_server_surface\"}";}}
}
