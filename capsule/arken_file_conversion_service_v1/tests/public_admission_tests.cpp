#include "arken_file_conversion_service_v1/public_admission.hpp"
#include "../tools/native/platform.hpp"
#include <iostream>
namespace C=arken::conversion::cloudflare;
using Json=nlohmann::json;
namespace {
unsigned checks=0;Json vectors=Json::array();
void Check(bool value,const char* label){++checks;C::Require(value,label);}
std::string Id(unsigned n){auto text=std::to_string(n);return std::string(64-text.size(),'0')+text;}
Json Run(const Json& input){const auto text=input.dump(),out=arken::file_conversion::service::v1::PublicAdmissionPacket(text);vectors.push_back({{"input",text},{"expected",out}});return Json::parse(out);}
struct Budget {
  Json record=nullptr,scope=nullptr;
  Json input(unsigned id,std::uint64_t bytes,std::uint64_t now=86400){return {{"kind","reserve"},{"record",record},{"scope",scope},{"operation",Id(id)},{"bytes",std::to_string(bytes)},{"now",std::to_string(now)}};}
  Json reserve(unsigned id,std::uint64_t bytes,std::uint64_t now=86400){const auto out=Run(input(id,bytes,now));if(out.contains("writes"))for(const auto& w:out.at("writes")){if(w.at("key")=="scope")scope=w.at("value");else{Check(w.at("key")=="admission","foreign admission key");record=w.at("value");}}return out;}
};
void Tests(){
  Budget count;for(unsigned i=0;i<64;++i)Check(count.reserve(i,1).at("allowed")==true,"file budget denied early");
  const auto before=count.record;auto denied=count.reserve(64,1);Check(denied.at("allowed")==false&&denied.at("writes").empty()&&count.record==before,"file budget overrun");
  Check(denied.at("retryAfterSeconds")==86400,"quota reset time");
  auto replay=count.reserve(0,1);Check(replay.at("allowed")==true&&replay.at("replayed")==true&&replay.at("writes").empty(),"reservation replay charged twice");
  Check(count.reserve(0,2).contains("packetError")&&count.record==before,"changed reservation accepted");
  Check(count.reserve(65,1,172799).at("allowed")==false,"UTC day reset early");
  Check(count.reserve(65,1,172800).at("allowed")==true,"UTC day rollover failed");
  Check(count.reserve(66,1,172799).contains("packetError"),"reversed clock reset quota");
  Budget bytes;for(unsigned i=0;i<8;++i)Check(bytes.reserve(i,8388608).at("allowed")==true,"byte budget denied early");
  Check(bytes.reserve(8,1).at("allowed")==false,"byte budget overrun");
  for(auto value:{0ull,8388609ull,18446744073709551615ull})Check(Budget().reserve(1,value).contains("packetError"),"invalid request size accepted");
  for(unsigned field=0;field<6;++field){auto input=count.input(67,1,172800);
    if(field==0)input["scope"]=nullptr;if(field==1)input["record"]=nullptr;
    if(field==2)input["scope"]="{\"chain\":84532,\"asset\":\"x\",\"payer\":\"y\"}";
    if(field==3)input["operation"]="unknown";
    if(field==4){auto record=Json::parse(input["record"].get<std::string>());record["reservations"].push_back(record["reservations"][0]);input["record"]=record.dump();}
    if(field==5)input["bytes"]=1;
    Check(Run(input).contains("packetError"),"malformed or partial admission state accepted");
  }
  const auto capability=Run({{"kind","capability"},{"fileId",Id(1)},{"fileToken",Id(2)}});
  Check(capability.at("owner")==C::Hash(Id(2)),"capability owner digest differs");
  Check(Run({{"kind","capability"},{"fileId",Id(1)},{"fileToken",nullptr}}).at("allowed")==false,"missing capability accepted");
  Check(Run({{"kind","capability"},{"fileId",Id(1)},{"fileToken","invalid"}}).contains("packetError"),"invalid capability accepted");
  Json access={{"kind","access"},{"publicAccess",true},{"hasAuthorization",false},{"administrator",false},{"intent","discovery"},{"fileId",nullptr},{"fileToken",nullptr}};
  Check(Run(access).at("mode")=="public","public discovery denied");access["intent"]="file";access["fileId"]=Id(1);
  Check(Run(access).at("mode")=="denied","public file without capability admitted");access["fileToken"]=Id(2);
  Check(Run(access).at("owner")==capability.at("owner"),"file access changed owner");
  access["hasAuthorization"]=true;Check(Run(access).at("mode")=="denied","bad bearer fell back to public");
  access["administrator"]=true;Check(Run(access).at("mode")=="operator","operator path lost");
  access["publicAccess"]=false;access["administrator"]=false;access["hasAuthorization"]=false;access["intent"]="discovery";Check(Run(access).at("mode")=="denied","private preview opened");
}
}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-conversion-public-admission-tests r4354\n";return 0;}
  try{Check(argc==1||(argc==3&&std::string_view(argv[1])=="--write-fixtures"),"invalid arguments");Tests();
    if(argc==3){const auto path=C::fs::absolute(argv[2]);C::PrivateDirectory(path);C::Write(path/"public-admission-fixtures.json",vectors.dump()+"\n");}
    std::cout<<checks<<" public admission checks passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
