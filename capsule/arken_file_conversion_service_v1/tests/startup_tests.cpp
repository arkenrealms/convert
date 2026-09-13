#include "arken_file_conversion_service_v1/production_startup.hpp"
#include "arken_file_conversion_service_v1/server_surface.hpp"
#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "../tools/native/platform.hpp"
#include <iostream>
namespace S=arken::file_conversion::service::v1;
namespace C=arken::conversion::cloudflare;using C::Json;
namespace {
unsigned checks=0;Json vectors=Json::array(),qualificationVectors=Json::array();
Json lastResult=Json::object();
void Check(bool ok,const char* text){++checks;C::Require(ok,text);}
Json Run(const Json& input){const auto result=Json::parse(S::ProductionStartupPacket(input.dump()));lastResult=result;vectors.push_back({{"input",input.dump()},{"expected",result}});return result;}
Json Reply(unsigned id,const Json& result){return {{"jsonrpc","2.0"},{"id",id},{"result",result}};}
void Tests(const C::fs::path& providerFile,const C::fs::path& output){
  const auto provider=Json::parse(C::Read(providerFile)).at("signingCases")[0].at("prepare");
  const Json configuration={{"enabled","true"},{"keyId",provider["keyId"]},{"keySecret",provider["keySecret"]},{"rpcClientKey","fixture_rpc_client_key"},
    {"releaseId",std::string(64,'7')},{"downloadSecret",std::string(64,'s')},{"hasFiles",true},{"hasLedger",true},{"bindingGeneration","1"}};
  Json input={{"configuration",configuration},{"cached",nullptr},{"anonymous",nullptr},{"supported",nullptr},{"rpc",nullptr},{"now","1000"}};
  const auto original=input;
  const auto first=Run(input);Check(first.at("slot")=="anonymous"&&first.at("request").at("authentication")=="none","anonymous readiness step");
  input["anonymous"]={{"status",401},{"body",""}};
  const auto second=Run(input);Check(second.at("slot")=="supported"&&second.at("request").at("authentication")=="cdp","authenticated readiness step");
  const Json capabilities={{"kinds",Json::array({{{"x402Version",2},{"scheme","exact"},{"network","eip155:8453"}}})},{"extensions",Json::array()},{"signers",Json::object()}};
  input["supported"]={{"status",200},{"body",capabilities.dump()}};
  const auto third=Run(input);Check(third.at("slot")=="rpc"&&third.at("request").at("url")=="https://api.developer.coinbase.com/rpc/v1/base/fixture_rpc_client_key","RPC scope");
  const auto calls=Json::parse(third.at("request").at("body").get<std::string>());
  Check(calls.size()==3&&calls[0]["method"]=="eth_chainId"&&calls[1]["method"]=="eth_getBlockByNumber"&&calls[2]["method"]=="eth_call"&&calls[2]["params"][0]["data"]=="0x313ce567","startup emitted wrong methods");
  const Json rpc=Json::array({Reply(3,"0x"+std::string(63,'0')+"6"),Reply(1,"0x2105"),Reply(2,Json{{"number","0x10"},{"timestamp","0x3e8"},{"hash","0x"+std::string(64,'a')}})});
  input["rpc"]={{"status",200},{"body",rpc.dump()}};const auto ready=Run(input);Check(ready.at("ready")==true&&ready.at("reused")==false,"ready not qualified");
  Check(ready.dump().find(provider.at("keySecret").get<std::string>())==std::string::npos&&ready.dump().find("fixture_rpc_client_key")==std::string::npos,"readiness leaked credentials");
  auto cached=original;cached["cached"]=ready.at("record");Check(Run(cached).at("reused")==true,"cache not reused");
  cached["now"]="1300";Check(Run(cached).at("kind")=="probe","expired cache accepted");cached["now"]="999";Check(Run(cached).at("kind")=="probe","reversed clock cache accepted");
  for(const auto name:{"releaseId","keyId","rpcClientKey","downloadSecret"}){auto changed=original;changed["cached"]=ready.at("record");
    changed["configuration"][name]=name==std::string("keyId")?Json("22222222-2222-3333-4444-555555555555"):Json(std::string(64,'b'));
    Check(Run(changed).at("kind")=="probe","changed configuration reused ready");}
  for(unsigned i=0;i<21;++i){auto bad=input;
    if(i==0)bad["configuration"]["enabled"]="false";if(i==1)bad["configuration"]["enabled"]=true;
    if(i==2)bad["configuration"]["keyId"]=nullptr;if(i==3)bad["configuration"]["keySecret"]="invalid";
    if(i==4)bad["configuration"]["releaseId"]=std::string(64,'0');if(i==5)bad["configuration"]["downloadSecret"]="short";
    if(i==6)bad["configuration"]["hasFiles"]=false;if(i==7)bad["configuration"]["hasLedger"]=false;
    if(i==8)bad["configuration"]["rpcClientKey"]="../wrong";if(i==9)bad["anonymous"]["status"]=200;
    if(i==10)bad["supported"]["status"]=403;
    if(i==11){auto c=capabilities;c["kinds"][0]["network"]="eip155:84532";bad["supported"]["body"]=c.dump();}
    if(i==12)bad["rpc"]["status"]=503;
    if(i>=13&&i<=17){auto r=rpc;if(i==13)r[1]["result"]="0x14a34";if(i==14)r[2]["result"]["hash"]="0x"+std::string(64,'0');if(i==15)r[0]["result"]="0x"+std::string(63,'0')+"9";if(i==16)r[0]["id"]=1;if(i==17)r[2]["result"]["timestamp"]="0x3e9";bad["rpc"]["body"]=r.dump();}
    if(i==18)bad["rpc"]["body"]=std::string(262145,' ');
    if(i==19)bad["configuration"]["bindingGeneration"]=1;
    if(i==20)bad["configuration"]["bindingGeneration"]="0";
    const auto rejected=Run(bad);Check(rejected.at("kind")=="unavailable"&&rejected.at("ready")==false,"unqualified readiness admitted");
  }
  const auto qualify=[&](const Json& request){auto result=Json::parse(S::ProductionQualificationPacket(request.dump()));qualificationVectors.push_back({{"input",request.dump()},{"expected",result}});return result;};
  const Json gate={{"kind","request"},{"administrator",true},{"method","POST"},{"hasBody",false},{"hasQuery",false}};
  Check(qualify(gate).at("allowed")==true,"private qualification gate refused");
  for(unsigned i=0;i<6;++i){auto bad=gate;if(i==0)bad["administrator"]=false;if(i==1)bad["method"]="GET";if(i==2)bad["hasBody"]=true;
    if(i==3)bad["hasQuery"]=true;if(i==4)bad["administrator"]="true";if(i==5)bad["ready"]=true;Check(qualify(bad).at("allowed")==false,"qualification gate widened");}
  auto disabled=input;disabled["configuration"]["enabled"]="false";
  auto qualified=qualify({{"kind","startup"},{"input",disabled}});Check(qualified.at("ready")==true,"disabled qualification failed");
  auto normal=disabled;normal["cached"]=qualified.at("record");Check(Run(normal).at("ready")==false,"qualification admitted purchase");
  auto enable=original;enable["cached"]=qualified.at("record");Check(Run(enable).at("kind")=="probe","qualification cache reused after enablement");
  disabled["cached"]=qualified.at("record");Check(qualify({{"kind","startup"},{"input",disabled}}).at("reused")==true,"qualification cache not reused");
  for(unsigned i=0;i<5;++i){auto bad=disabled;if(i==0)bad["configuration"]["enabled"]="true";if(i==1)bad["configuration"]["enabled"]=false;
    if(i==2)bad["configuration"]["keySecret"]="invalid";if(i==3)bad["configuration"]["hasLedger"]=false;if(i==4){bad["cached"]=nullptr;bad["rpc"]["status"]=503;}
    Check(qualify({{"kind","startup"},{"input",bad}}).value("ready",false)==false,"qualification accepted bad context");}
  auto probing=original;probing["configuration"]["enabled"]="false";
  Check(qualify({{"kind","startup"},{"input",probing}}).at("slot")=="anonymous","qualification skipped live checks");
  Json surfaces=Json::array();
  for(const Json& id:{Json(nullptr),Json("base-sepolia-v1"),Json("base-mainnet-v1")})for(bool isReady:{false,true}){
    const Json request={{"profileId",id},{"ready",isReady}};const auto surface=Json::parse(S::SurfacePacket(request.dump()));
    Check(surface.at("charging")==Json(id=="base-mainnet-v1"&&isReady),"surface charging wrong");surfaces.push_back({{"input",request.dump()},{"expected",surface}});
  }
  const auto sandbox=S::Surface(S::ServerMode::sandbox),production=S::Surface(S::ServerMode::production);
  Check(sandbox.accessMilliseconds==900000&&sandbox.downloadDomain.empty()&&production.accessMilliseconds==3600000&&!production.downloadDomain.empty()&&production.artifactPrefix!="sandbox","surface isolation");
  const Json initialize={{"jsonrpc","2.0"},{"id",1},{"method","initialize"},{"params",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},{"clientInfo",{{"name","fixture"},{"version","1"}}}}}};
  const auto result=Json::parse(Json::parse(S::PlanRequest(initialize.dump(),S::ServerMode::production)).at("body").get<std::string>()).at("result");
  Check(result.at("instructions")==production.instructions,"production discovery mislabeled");
  const unsigned char source[]={0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,30,20,10,255};
  namespace F=arken::file_conversion::v1;F::ImageOptions options;options.input=F::image::Format::tga;options.output=F::image::Format::png;
  const auto png=F::ConvertImage(std::span(reinterpret_cast<const std::byte*>(source),sizeof(source)),options);Check(bool(png),"native production PNG fixture");
  const auto pngSha=C::Hash(std::string_view(reinterpret_cast<const char*>(png.bytes.data()),png.bytes.size()));
  std::vector<std::uint8_t> entropy(1024*1024*4);std::uint32_t seed=123456789;
  for(std::size_t i=0;i<entropy.size();++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;entropy[i]=i%4==3?255:static_cast<std::uint8_t>(seed);}
  const auto largeSource=F::image::EncodeImage(F::image::Format::png,1024,1024,entropy);
  const auto largeJpeg=F::ConvertImage(largeSource.bytes,{.output=F::image::Format::jpeg});Check(bool(largeSource)&&bool(largeJpeg),"large JPEG fixture failed");
  const auto largeJpegHash=C::Hash(std::string_view(reinterpret_cast<const char*>(largeJpeg.bytes.data()),largeJpeg.bytes.size()));
  Check(largeJpegHash=="61f5dd69e9fcc1b921b5217cdfc568f4fd1fc6cbf48cc6cec38e8d96f1623088","portable JPEG quantization changed");
  const Json jpegFixture={{"sha256",largeJpegHash},{"codec",{{"input",static_cast<unsigned>(F::image::Format::png)},{"output",static_cast<unsigned>(F::image::Format::jpeg)},{"quality",95},{"alpha",0}}}};
  C::PrivateDirectory(output);C::Write(output/"startup-fixtures.json",Json{{"vectors",vectors},{"qualificationVectors",qualificationVectors},{"surfaces",surfaces},{"configuration",configuration},{"anonymous",input["anonymous"]},{"supported",input["supported"]},{"rpc",input["rpc"]},{"nativePngSha256",pngSha},{"largeJpeg",jpegFixture}}.dump(2)+"\n");
  C::Write(output/"jpeg-input.png",std::string_view(reinterpret_cast<const char*>(largeSource.bytes.data()),largeSource.bytes.size()));
  std::cout<<checks<<" startup checks passed; "<<vectors.size()<<" native vectors\n";
}
}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-startup-tests r4370\n";return 0;}
  try{C::Require(argc==4&&std::string_view(argv[1])=="--write-fixtures","invalid_arguments");Tests(argv[2],argv[3]);return 0;}
  catch(const std::exception& error){const std::string message=error.what();
    std::cerr<<Json{{"code",message.size()<160&&message.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ /_")==std::string::npos?message:"startup_test_failed"},{"checkIndex",checks},{"vectorCount",vectors.size()},{"lastKind",lastResult.value("kind","")},{"lastCode",lastResult.value("code","")}}.dump()<<'\n';return 1;}}
