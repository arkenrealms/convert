#include "provider.hpp"
#include "arken_file_conversion_service_v1/provider_signing.hpp"
#include <openssl/evp.h>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <sys/stat.h>
namespace arken::conversion::cloudflare::provider {
namespace {
namespace A=arken::file_conversion::service::v1::payment;
const std::string id="11111111-2222-3333-4444-555555555555",nonce(32,'a');
std::size_t checks=0;
void Check(bool condition,std::string_view code){++checks;Require(condition,code);}
template<class F> void Reject(F call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}Check(failed,"provider_negative_accepted");}
std::string Unhex(std::string_view s){std::string result;for(std::size_t i=0;i<s.size();i+=2)result+=static_cast<char>(std::stoul(std::string(s.substr(i,2)),nullptr,16));return result;}
std::string Encode(std::string_view s){std::string out(4*((s.size()+2)/3)+1,'\0');const int n=EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),reinterpret_cast<const unsigned char*>(s.data()),static_cast<int>(s.size()));Require(n>=0,"test_encoding");out.resize(static_cast<std::size_t>(n));return out;}
std::string Decode(std::string text){for(auto& c:text){if(c=='-')c='+';else if(c=='_')c='/';}while(text.size()%4)text+='=';
    std::string out(text.size(),'\0');const int n=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),reinterpret_cast<const unsigned char*>(text.data()),static_cast<int>(text.size()));Require(n>=0,"test_decoding");
    std::size_t size=static_cast<std::size_t>(n);if(text.back()=='=')--size;if(text[text.size()-2]=='=')--size;out.resize(size);return out;
}
bool Verify(std::string_view publicKey,std::string_view message,std::string_view signature){
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(EVP_PKEY_new_raw_public_key_ex(nullptr,"ED25519",nullptr,reinterpret_cast<const unsigned char*>(publicKey.data()),publicKey.size()),EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    Require(bool(key)&&bool(ctx)&&EVP_DigestVerifyInit(ctx.get(),nullptr,nullptr,nullptr,key.get())==1,"test_verifier_init");
    return EVP_DigestVerify(ctx.get(),reinterpret_cast<const unsigned char*>(signature.data()),signature.size(),reinterpret_cast<const unsigned char*>(message.data()),message.size())==1;
}
Json Capabilities(){return {{"kinds",Json::array({{{"x402Version",2},{"scheme","exact"},{"network","eip155:8453"}},{{"x402Version",2},{"scheme","exact"},{"network","eip155:84532"}}})},{"extensions",Json::array({"bazaar"})},{"signers",{{"eip155:*",Json::array({"0x1111111111111111111111111111111111111111"})}}}};}
}
void Tests(const fs::path& root){
    PrivateDirectory(root);const auto dir=root/Random();PrivateDirectory(dir);checks=0;
    // RFC8032 section7.1 test1, public fixture only; no live credential/wallet.
    const auto seed=Unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    const auto publicKey=Unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto secret=Encode(seed+publicKey);
    Check(Verify(publicKey,"",Unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")),"rfc8032_verifier_vector");
    std::set<std::string> tokens;
    for(const auto op:{CdpOperation::supported,CdpOperation::verify,CdpOperation::settle}){
        const auto request=A::PrepareCdpAuthorization(id,op,1000,nonce);Check(request.has_value(),"cdp_request_missing");
        const std::string action=op==CdpOperation::supported?"supported":op==CdpOperation::verify?"verify":"settle";
        Check(request->url=="https://api.cdp.coinbase.com/platform/v2/x402/"+action&&request->method==(op==CdpOperation::supported?"GET":"POST"),"cdp_endpoint_scope");
        const auto token=SignCdpRequest(id,secret,op,1000,nonce);Check(tokens.insert(token).second,"jwt_operation_not_bound");
        Check(SignCdpRequest(id,secret,op,1000,nonce)==token,"ed25519_not_deterministic");
        const auto first=token.find('.'),last=token.rfind('.');Check(first!=last&&last!=std::string::npos,"jwt_shape");
        const auto header=Json::parse(Decode(token.substr(0,first)));const auto claims=Json::parse(Decode(token.substr(first+1,last-first-1)));
        Check(header==Json{{"alg","EdDSA"},{"typ","JWT"},{"kid",id},{"nonce",nonce}},"jwt_protected_header");
        Check(claims==Json{{"sub",id},{"iss","cdp"},{"iat",1000},{"nbf",1000},{"exp",1120},{"uris",Json::array({request->method+" api.cdp.coinbase.com/platform/v2/x402/"+action})}},"jwt_claim_scope");
        const auto signedBytes=token.substr(0,last),signature=Decode(token.substr(last+1));Check(Verify(publicKey,signedBytes,signature),"independent_jwt_verification");
        auto changed=signedBytes;changed[0]^=1;Check(!Verify(publicKey,changed,signature),"tampered_jwt_verified");
        Check(SignCdpRequest(id,secret,op,1001,nonce)!=token&&SignCdpRequest(id,secret,op,1000,std::string(32,'b'))!=token,"jwt_clock_nonce_unbound");
    }
    Check(A::PrepareCdpAuthorization("organizations/"+id+"/apiKeys/"+id,CdpOperation::supported,1000,nonce).has_value(),"organization_key_id_rejected");
    for(const auto& bad:{std::string(),id+"\n",std::string(500,'a'),"organizations/"+id+"/other/"+id})Check(!A::PrepareCdpAuthorization(bad,CdpOperation::supported,1000,nonce),"bad_id_allowed");
    Check(!A::PrepareCdpAuthorization(id,static_cast<CdpOperation>(99),1000,nonce),"unknown_operation_allowed");
    for(const auto now:{std::uint64_t(0),std::uint64_t(9007199254740872ULL),std::numeric_limits<std::uint64_t>::max()})Check(!A::PrepareCdpAuthorization(id,CdpOperation::supported,now,nonce),"bad_clock_allowed");
    for(const auto& bad:{std::string(),std::string(31,'a'),std::string(32,'G'),nonce+"\n"})Check(!A::PrepareCdpAuthorization(id,CdpOperation::supported,1000,bad),"bad_nonce_allowed");
    for(auto bad:{secret.substr(0,87),secret+"\n",std::string(88,'A'),std::string("-----BEGIN EC PRIVATE KEY-----"),Encode(seed+std::string(32,'x'))})Reject([&]{SignCdpRequest(id,bad,CdpOperation::supported,1000,nonce);});
    auto padding=secret;padding[85]='R';Reject([&]{SignCdpRequest(id,padding,CdpOperation::supported,1000,nonce);});
    const auto caps=Capabilities();Check(A::ReadProviderCapabilities(caps.dump())->baseMainnet&&A::ReadProviderCapabilities(caps.dump())->baseSepolia,"capability_profile_missing");
    for(unsigned scenario=0;scenario<14;++scenario){auto invalid=caps;
        if(scenario==0)invalid.erase("kinds");if(scenario==1)invalid["kinds"]="not-array";
        if(scenario==2)invalid["kinds"][0]["x402Version"]="2";if(scenario==3)invalid["kinds"][0]["network"]=nullptr;
        if(scenario==4)invalid["kinds"]=Json::array();
        if(scenario==5)invalid["extensions"].push_back("bazaar");if(scenario==6)invalid["signers"]["eip155:*"]="not-array";
        if(scenario==7)invalid["kinds"][0]["extra"]=std::string(65536,'x');
        if(scenario==8)invalid["kinds"][0]["scheme"]="ex\nact";
        if(scenario==9)for(unsigned i=0;i<129;++i)invalid["kinds"].push_back(caps["kinds"][0]);
        if(scenario==10)invalid["signers"]=nullptr;if(scenario==11)invalid["extensions"]=Json::array({12});
        if(scenario==12){invalid["kinds"][0]["x402Version"]=1;invalid["kinds"][1]["network"]="eip155:1";}
        if(scenario==13)invalid["kinds"][0]["x402Version"]=-2;
        const auto parsed=A::ReadProviderCapabilities(invalid.dump());
        if(scenario==4||scenario==12)Check(parsed&&!parsed->baseMainnet&&!parsed->baseSepolia,"unadvertised_profile_inferred");else Check(!parsed,"invalid_capability_accepted");
    }
    Check(!A::ReadProviderCapabilities("{\"kinds\":[],\"kinds\":[],\"extensions\":[],\"signers\":{}}"),"duplicate_capability_key");
    const auto credential=dir/"credential.json";const auto credentialBytes=Json{{"keyId",id},{"keySecret",secret}}.dump();Write(credential,credentialBytes,true);
    for(unsigned scenario=0;scenario<8;++scenario){unsigned requests=0,authed=0,production=0;std::string observedJwt;
        Transport send=[&](const Request& r){++requests;Check(r.method=="GET"&&r.body.empty(),"observer_not_read_only");
            if(r.url=="https://x402.org/facilitator/supported"){Check(r.headers.empty(),"credential_sent_to_test_provider");return Response{200,caps.dump()};}
            ++production;Check(r.url=="https://api.cdp.coinbase.com/platform/v2/x402/supported","observer_foreign_endpoint");
            if(r.headers.empty())return Response{scenario==7?200:401,scenario==7?caps.dump():"Authentication required"};
            ++authed;Check(r.headers.size()==1&&r.headers[0].starts_with("Authorization: Bearer "),"observer_auth_header_invalid");observedJwt=r.headers[0].substr(22);
            Check(observedJwt==SignCdpRequest(id,secret,CdpOperation::supported,1000,nonce),"observer_wrong_signed_request");
            if(scenario==1)return Response{401,r.headers[0]+secret};if(scenario==2)return Response{403,r.headers[0]};if(scenario==3)return Response{429,r.headers[0]};
            if(scenario==4)throw std::runtime_error(r.headers[0]);if(scenario==5)return Response{200,"malformed"};return Response{200,caps.dump()};
        };
        const bool authenticated=scenario!=6;const auto result=Observe(send,authenticated?credential:fs::path{},1000,nonce);
        Check(requests==(authenticated?3u:2u)&&production==(authenticated?2u:1u)&&authed==(authenticated?1u:0u),"observer_retry_or_fallback");
        Check(result.at("productionCredentialAccepted")==Json(scenario==0)&&result.at("productionReady")==false&&result.at("runtimeProviderIntegrated")==false,"observation_overclaimed_readiness");
        const auto text=result.dump();Check(text.find(secret)==std::string::npos&&(observedJwt.empty()||text.find(observedJwt)==std::string::npos),"provider_error_leaked_credentials");
        Check(result.at("settlementCalls")==0&&result.at("verificationCalls")==0,"observer_delivered_payment");
    }
    unsigned requests=0;Transport never=[&](const Request&){++requests;return Response{500,""};};
    for(unsigned scenario=0;scenario<8;++scenario){const auto path=dir/("invalid-"+std::to_string(scenario));
        if(scenario==0)Write(path,credentialBytes+std::string(4096,' '),true);
        if(scenario==1){Write(path,credentialBytes,true);Check(chmod(path.c_str(),0644)==0,"fixture_chmod");}
        if(scenario==2)fs::create_symlink(credential,path);
        if(scenario==3)fs::create_hard_link(credential,path);
        if(scenario==4)Write(path,"{\"keyId\":\"first\",\"keyId\":\""+id+"\",\"keySecret\":\""+secret+"\"}",true);
        if(scenario==5)Write(path,Json{{"keyId",id},{"keySecret",secret},{"extra",true}}.dump(),true);
        if(scenario==6)Write(path,Json{{"keyId",id},{"keySecret",Json::object()}}.dump(),true);
        Reject([&]{Observe(never,path,1000,nonce);});Check(requests==0,"invalid_credential_contacted_provider");
        if(scenario==3)fs::remove(path);
    }
    Json vectors=Json::array(),signingCases=Json::array();
    const auto vector=[&](const Json& input,bool valid){const auto result=Json::parse(A::CdpProviderPacket(input.dump()));
        Check(result.contains("packetError")!=valid,"provider_packet_expectation");vectors.push_back({{"input",input.dump()},{"expected",result}});return result;};
    for(const auto op:{CdpOperation::supported,CdpOperation::verify,CdpOperation::settle})for(const auto& keyId:{id,"organizations/"+id+"/apiKeys/"+id}){
        const auto request=A::PrepareCdpAuthorization(keyId,op,1000,nonce);const Json endpoint={{"method",request->method},{"url",request->url}};
        const Json prepare={{"kind","prepare"},{"request",endpoint},{"keyId",keyId},{"keySecret",secret},{"now","1000"},{"nonce",nonce}};
        const auto material=vector(prepare,true);const auto token=SignCdpRequest(keyId,secret,op,1000,nonce);const auto split=token.rfind('.');
        Check(material.at("signingInput")==token.substr(0,split),"native_signing_input_drift");
        const auto signatureText=Decode(token.substr(split+1));const std::vector<std::uint8_t> signature(signatureText.begin(),signatureText.end());
        Json complete=prepare;complete["kind"]="complete";complete.erase("keySecret");complete["signature"]=signature;
        Check(vector(complete,true).at("authorization")=="Bearer "+token,"native_complete_jwt_drift");
        signingCases.push_back({{"prepare",prepare},{"jwt",token}});
    }
    for(const auto op:{CdpOperation::supported,CdpOperation::verify,CdpOperation::settle}){
        const auto endpoint=*A::CdpEndpointFor(op);
        Json request={{"method",endpoint.method},{"url",endpoint.url},{"authentication","cdp"},{"body",op==CdpOperation::supported?"":"{}"}};
        const auto plan=vector({{"kind","transport"},{"request",request}},true);
        Check(plan.at("deadlineMs")==30000&&plan.at("responseLimit")==16384,"provider_transport_bounds");
        Check(op==CdpOperation::supported?plan.at("request").at("body").is_null():plan.at("request").at("body")=="{}","provider_body_canonicalization");
        for(unsigned scenario=0;scenario<7;++scenario){auto bad=request;
            if(scenario==0)bad["url"]=endpoint.url+"?x=1";
            if(scenario==1)bad["authentication"]="none";
            if(scenario==2)bad["headers"]={{"Authorization","foreign"}};
            if(scenario==3)bad["method"]="DELETE";
            if(scenario==4)bad["body"]=std::string(8193,'x');
            if(scenario==5)bad["body"]=Json::object();
            if(scenario==6)bad["url"]="https://foreign.invalid/settle";
            vector({{"kind","transport"},{"request",bad}},false);
        }
        if(op!=CdpOperation::supported){request["body"]=std::string(8192,'x');vector({{"kind","transport"},{"request",request}},true);request.erase("body");vector({{"kind","transport"},{"request",request}},false);}
    }
    const auto base=signingCases[0]["prepare"];
    for(unsigned scenario=0;scenario<14;++scenario){auto bad=base;
        if(scenario==0)bad["request"]["method"]="POST";
        if(scenario==1)bad["request"]["url"]="https://x402.org/facilitator/supported";
        if(scenario==2)bad["request"]["url"]="https://api.cdp.coinbase.com.evil.invalid/platform/v2/x402/supported";
        if(scenario==3)bad["request"]["url"]="https://api.cdp.coinbase.com/platform/v2/x402/supported?more=true";
        if(scenario==4)bad["keyId"]="bad";
        if(scenario==5)bad["now"]="9007199254740992";
        if(scenario==6)bad["nonce"]="bad";
        if(scenario==7)bad["keySecret"]="bad";
        if(scenario==8)bad["request"]["headers"]=Json::object();
        if(scenario==9)bad["keySecret"]=nullptr;
        if(scenario==10)bad["keySecret"]=std::string(4096,'A');
        if(scenario==11)bad["extra"]=1;
        if(scenario>=12){bad["kind"]="complete";bad.erase("keySecret");bad["signature"]=Json::array();for(unsigned i=0;i<(scenario==12?63u:64u);++i)bad["signature"].push_back(scenario==13?256:0);}
        vector(bad,false);
    }
    auto mismatch=base;mismatch["keySecret"]=Encode(seed+std::string(32,'x'));vector(mismatch,true);
    Reject([&]{SignCdpRequest(id,mismatch.at("keySecret").get<std::string>(),CdpOperation::supported,1000,nonce);});
    Write(root/"provider-fixtures.json",Json{{"credentialFixture","rfc8032-test1-not-live"},{"vectors",vectors},{"signingCases",signingCases},{"mismatchedKey",mismatch}}.dump(2)+"\n");
    Write(dir/"proof.json",Json{{"status","passed"},{"checks",checks},{"credentialFixture","rfc8032-test1-not-live"},{"paidProof",false}}.dump(2)+"\n",true);
    std::cout<<Json{{"status","passed"},{"checks",checks},{"packetVectors",vectors.size()},{"signingCases",signingCases.size()},{"fixturePath",(root/"provider-fixtures.json").string()},{"artifactPath",(dir/"proof.json").string()},{"actualProviderAuthenticationProved",false}}.dump()<<'\n';
}
}
