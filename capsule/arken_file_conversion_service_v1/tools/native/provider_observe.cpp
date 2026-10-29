#include "provider.hpp"
#include <openssl/crypto.h>
#include <set>
namespace arken::conversion::cloudflare::provider {
namespace {
struct CleansedText {std::string value;~CleansedText(){OPENSSL_cleanse(value.data(),value.size());}};
std::string CredentialHeader(const fs::path& path,std::uint64_t now,std::string_view nonce){
    CleansedText bytes{Read(path,4096,true)};std::set<std::string> fields;
    auto j=Json::parse(bytes.value,[&](int depth,Json::parse_event_t event,Json& value){
        Require(depth<=1,"cdp_credential_nesting_invalid");
        if(event==Json::parse_event_t::key)Require(fields.insert(value.get<std::string>()).second,"cdp_credential_duplicate_field");return true;
    });
    Require(j.is_object()&&j.size()==2&&j.contains("keyId")&&j.contains("keySecret")&&j.at("keyId").is_string()&&j.at("keySecret").is_string(),"cdp_credential_schema_invalid");
    auto& secret=j.at("keySecret").get_ref<std::string&>();
    struct Cleanup {std::string& value;~Cleanup(){OPENSSL_cleanse(value.data(),value.size());}} cleanup{secret};
    return "Authorization: Bearer "+SignCdpRequest(j.at("keyId").get<std::string>(),secret,CdpOperation::supported,now,nonce);
}
Json ObserveOne(const Transport& send,std::string_view name,const std::string& url,const std::string& authorization){
    Json row={{"provider",name},{"endpoint",url},{"authenticationRequested",!authorization.empty()},{"authenticationAccepted",false},
        {"capabilitiesAccepted",false},{"baseMainnet",false},{"baseSepolia",false}};
    Response response;
    try{response=send({"GET",url,"","application/json",authorization.empty()?std::vector<std::string>{}:std::vector<std::string>{authorization}});}
    catch(const std::exception&){row["status"]="transport_unavailable";return row;}
    row["httpStatus"]=response.status;
    if(response.status!=200){
        row["status"]=response.status==401?"authentication_required_or_rejected":response.status==403?"forbidden":response.status==429?"rate_limited":"http_unavailable";return row;
    }
    const auto caps=arken::file_conversion::service::v1::payment::ReadProviderCapabilities(response.body);
    if(!caps){row["status"]="invalid_capability_response";return row;}
    row["status"]="capabilities_observed";row["authenticationAccepted"]=!authorization.empty();row["capabilitiesAccepted"]=true;
    row["baseMainnet"]=caps->baseMainnet;row["baseSepolia"]=caps->baseSepolia;row["kindCount"]=caps->kindCount;
    row["extensionCount"]=caps->extensionCount;row["signerGroupCount"]=caps->signerGroupCount;row["responseSha256"]=Hash(response.body);return row;
}
}
Json Observe(const Transport& send,const fs::path& credential,std::uint64_t now,std::string_view nonce){
    Require(now>0&&now<=9007199254740871ULL,"provider_clock_invalid");
    // Validate selected credentials before ANY request. Authentication failures
    // never downgrade this run to public mode or change the selected provider.
    CleansedText authorization{credential.empty()?"":CredentialHeader(credential,now,nonce)};
    const auto endpoint="https://api.cdp.coinbase.com/platform/v2/x402/supported";
    // Negative control is deliberate and occurs before authenticated delivery,
    // never as a retry/fallback after its rejection.
    const auto anonymous=ObserveOne(send,"coinbase-cdp",endpoint,"");
    const auto production=credential.empty()?anonymous:ObserveOne(send,"coinbase-cdp",endpoint,authorization.value);
    const auto test=ObserveOne(send,"x402-test","https://x402.org/facilitator/supported","");
    return {{"status","observed"},{"observerVersion","r4351"},{"observedAt",now},{"providers",Json::array({production,test})},
        {"productionAnonymousHttpStatus",anonymous.value("httpStatus",0)},
        {"productionCredentialAccepted",production.at("authenticationAccepted")==true&&anonymous.value("httpStatus",0)==401},{"productionReady",false},{"runtimeProviderIntegrated",false},
        {"verificationCalls",0},{"settlementCalls",0},{"cloudMutations",0}};
}
}
