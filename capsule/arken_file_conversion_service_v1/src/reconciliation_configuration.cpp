#include "arken_file_conversion_service_v1/reconciliation_configuration.hpp"
namespace arken::file_conversion::service::v1::payment {
ReconciliationEndpoint ResolveReconciliationEndpoint(const PaymentProfile& profile,std::optional<std::string_view> key){
    if(profile.reconciliationProvider==ReconciliationProvider::cdpBaseMainnet){
        if(!key)return {{},"mainnet_reconciliation_not_configured"};
        if(key->empty()||key->size()>256||key->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string_view::npos)return {{},"mainnet_rpc_client_key_invalid"};
        return {"https://api.developer.coinbase.com/rpc/v1/base/"+std::string(*key),{}};
    }
    if(profile.reconciliationProvider!=ReconciliationProvider::publicBaseSepolia)return {{},"reconciliation_provider_invalid"};
    if(key)return {{},"testnet_rpc_configuration_rejected"};
    return {"https://sepolia.base.org",{}};
}
}
