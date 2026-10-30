#include "workerd_profile.hpp"
#include "workerd_profile_inventory.hpp"
#include "cdp_page_session.hpp"
#include "../native_browser/owned_child.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arken/digest/v1/digest.hpp>
#import <Foundation/Foundation.h>
extern char** environ;
namespace {
using Json=nlohmann::json;namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
struct WorkerdSpawnAttributes {
    posix_spawnattr_t value;
    explicit WorkerdSpawnAttributes(bool captureTerm=true){
        require(posix_spawnattr_init(&value)==0,"workerd spawn attributes");
        sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);sigaddset(&defaults,SIGTERM);
        // KJ captures SIGTERM in main; block it before executable initialization
        // too, so pre-main threads cannot consume it outside KJ's event loop.
        if(captureTerm)sigaddset(&mask,SIGTERM);
        const int error=posix_spawnattr_setsigmask(&value,&mask)|posix_spawnattr_setsigdefault(&value,&defaults)|posix_spawnattr_setflags(&value,POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF);
        if(error){posix_spawnattr_destroy(&value);throw std::runtime_error("workerd signal admission failed");}
    }
    ~WorkerdSpawnAttributes(){posix_spawnattr_destroy(&value);}
    WorkerdSpawnAttributes(const WorkerdSpawnAttributes&)=delete;
    WorkerdSpawnAttributes& operator=(const WorkerdSpawnAttributes&)=delete;
};
std::string read(const fs::path& p){require(fs::file_size(p)<=1048576,"profile input limit");std::ifstream in(p);require(bool(in),"profile input unavailable");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& p,const Json& j){std::ofstream out(p);out<<j.dump(2)<<'\n';out.close();require(bool(out),"profile output failed");}
std::string hash(const fs::path& p){std::ifstream in(p,std::ios::binary);require(bool(in),"profile hash input");arken::digest::v1::Sha256Hasher h;char data[65536];while(in){in.read(data,sizeof data);const auto n=in.gcount();if(n)require(h.update(std::as_bytes(std::span(data,static_cast<std::size_t>(n)))),"profile hash update");}require(in.eof(),"profile hash read");const auto digest=h.finalize();require(bool(digest),"profile hash finalization");std::array<char,71> text{};require(arken::digest::v1::format_sha256(*digest,text),"profile digest format");return std::string(text.data()+7,64);}
Json http(const std::string& url,const char* method="GET",unsigned timeout=30,const fs::path& bodyFile={}){
    @autoreleasepool {
        NSMutableURLRequest* request=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];request.HTTPMethod=[NSString stringWithUTF8String:method];request.timeoutInterval=timeout;
        if(!bodyFile.empty()){require(fs::file_size(bodyFile)<=2u*1024u*1024u,"profile request body limit");request.HTTPBody=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:bodyFile.c_str()]];require(request.HTTPBody!=nil,"profile request body unavailable");}
        // Each probe has an ephemeral session. Do not leave an HTTP keepalive
        // racing the owned worker's graceful shutdown after the final reply.
        [request setValue:@"close" forHTTPHeaderField:@"Connection"];
        auto done=dispatch_semaphore_create(0);__block NSData* body=nil;__block NSError* error=nil;__block NSHTTPURLResponse* response=nil;
        NSURLSession* session=[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
        NSURLSessionDataTask* task=[session dataTaskWithRequest:request completionHandler:^(NSData* data,NSURLResponse* reply,NSError* e){body=data;response=(NSHTTPURLResponse*)reply;error=e;dispatch_semaphore_signal(done);}];[task resume];
        const bool finished=dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,(timeout+1)*NSEC_PER_SEC))==0;[session invalidateAndCancel];
        if(!finished||error!=nil){
            const std::string detail=error
              ? std::string(error.domain.UTF8String)+"/"+std::to_string(error.code)+": "+error.localizedDescription.UTF8String
              : "completion deadline exceeded";
            throw std::runtime_error("owned workerd HTTP failed: "+detail);
        }
        require(response.statusCode==200&&body.length<=1048576,"owned workerd response invalid");
        return Json::parse(std::string(static_cast<const char*>(body.bytes),body.length));
    }
}
struct Child {
    pid_t pid=-1;int control=-1;std::string buffered;fs::path outputRoot;
    arken::codecast::native_browser::ChildStopResult stopped;
    ~Child(){close();}
    void close(){if(pid>0){stopped=arken::codecast::native_browser::StopOwnedChild(pid,std::chrono::milliseconds(0),std::chrono::milliseconds(2000),std::chrono::milliseconds(2000),[&](pid_t waiting){
        // Distinguish signal delivery from a drain waiting on HTTP clients.
        // The callback runs only while this exact launched child is waitable.
        const auto sockets=(outputRoot/"shutdown-sockets.log").string(),childId=std::to_string(waiting);
        std::vector<std::string> socketArgs{"/usr/sbin/lsof","-nP","-a","-p",childId,"-i"};std::vector<char*> socketArgv;for(auto& a:socketArgs)socketArgv.push_back(a.data());socketArgv.push_back(nullptr);
        posix_spawn_file_actions_t socketActions;posix_spawn_file_actions_init(&socketActions);posix_spawn_file_actions_addopen(&socketActions,1,sockets.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);posix_spawn_file_actions_adddup2(&socketActions,1,2);
        pid_t socketObserver=-1;const auto socketError=posix_spawn(&socketObserver,socketArgs[0].c_str(),&socketActions,nullptr,socketArgv.data(),environ);posix_spawn_file_actions_destroy(&socketActions);
        if(socketError)std::cerr<<"shutdown socket observation spawn failed: "<<socketError<<'\n';
        else {const auto result=arken::codecast::native_browser::StopOwnedChild(socketObserver,std::chrono::milliseconds(1000));if(!result.reaped||result.forced||result.error)std::cerr<<"shutdown socket observation incomplete\n";}
        // macOS process-native stack evidence; only the still-waitable child,
        // no account/environment dump and no mutation of the target runtime.
        const auto file=(outputRoot/"shutdown.sample").string(),id=std::to_string(waiting);
        std::vector<std::string> args{"/usr/bin/sample",id,"1","10","-file",file};std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_addopen(&actions,1,"/dev/null",O_WRONLY,0);posix_spawn_file_actions_adddup2(&actions,1,2);
        pid_t observer=-1;const auto error=posix_spawn(&observer,args[0].c_str(),&actions,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);
        if(error){std::cerr<<"shutdown sample spawn failed: "<<error<<'\n';return;}
        const auto result=arken::codecast::native_browser::StopOwnedChild(observer,std::chrono::milliseconds(2500));
        if(!result.reaped||result.forced||result.error)std::cerr<<"shutdown sample incomplete\n";
    },4);pid=-1;}if(control>=0){::close(control);control=-1;}}
    void start(const std::string& executable,const fs::path& config,const fs::path& output,bool inspect,bool diskState){
        outputRoot=output;
        WorkerdSpawnAttributes attributes;
        int channels[2];require(pipe(channels)==0,"profile control pipe");const int log=open((output/"workerd.log").c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);require(log>=0,"profile log open");
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,channels[1],3);posix_spawn_file_actions_adddup2(&actions,log,1);posix_spawn_file_actions_adddup2(&actions,log,2);
        std::vector<std::string> args{executable,"serve","--control-fd=3","--socket-addr=profile=127.0.0.1:0"};
        if(diskState){fs::create_directory(output/"state");args.push_back("--directory-path=state="+(output/"state").string());}
        if(inspect)args.push_back("--inspector-addr=127.0.0.1:0");args.push_back(config.string());
        std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
        const int error=posix_spawn(&pid,executable.c_str(),&actions,&attributes.value,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);::close(channels[1]);::close(log);
        if(error){::close(channels[0]);pid=-1;throw std::runtime_error("profile workerd spawn failed");}control=channels[0];
    }
    Json message(){
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        for(;;){const auto at=buffered.find('\n');if(at!=buffered.npos){auto text=buffered.substr(0,at);buffered.erase(0,at+1);return Json::parse(text);}
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();require(left>0,"workerd startup deadline");pollfd p{control,POLLIN,0};const auto ready=poll(&p,1,static_cast<int>(left));if(ready<0&&errno==EINTR)continue;require(ready>0,"workerd control timeout");
            char b[4096];const auto n=::read(control,b,sizeof b);require(n>0,"workerd closed control pipe");buffered.append(b,n);require(buffered.size()<=65536,"workerd control limit");}
    }
};
Json one(const std::string& executable,const fs::path& config,const Json& recipe,const fs::path& output,bool inspect){
    fs::create_directory(output);Child child;const auto began=Clock::now();child.start(executable,config,output,inspect,recipe.value("diskState",true));unsigned port=0,inspector=0;
    for(unsigned i=0;i<10&&(!port||(inspect&&!inspector));++i){const auto m=child.message();const auto event=m.at("event").get<std::string>();if(event=="listen"&&m.at("socket")=="profile")port=m.at("port").get<unsigned>();if(event=="listen-inspector")inspector=m.at("port").get<unsigned>();}
    require(port>0&&port<65536&&(!inspect||(inspector>0&&inspector<65536)),"workerd port receipt missing");
    const auto origin="http://127.0.0.1:"+std::to_string(port);Json samples=Json::array(),results=Json::array();std::unique_ptr<arken::codecast::proof::CdpPageSession> session;
    if(inspect){const auto targets=http("http://127.0.0.1:"+std::to_string(inspector)+"/json/list");
        const auto name=recipe.at("workerName").get<std::string>();
        const auto target=arken::codecast::proof::SelectWorkerdProfileTarget(targets,name,recipe.value("fixtureWorkers",Json::array()),inspector);
        const auto endpoint=target.at("webSocketDebuggerUrl").get<std::string>();require(endpoint=="ws://127.0.0.1:"+std::to_string(inspector)+"/"+name,"foreign inspector endpoint");
        session=std::make_unique<arken::codecast::proof::CdpPageSession>(endpoint,arken::codecast::proof::CdpPageAccess::worker_memory);
    }
    const bool nativeWasm=recipe.value("nativeWasmInventory",false);
    const auto observationPath=recipe.value("checkpointObservationPath",std::string{});
    Json observations=Json::array();
    double maximum=0;const auto sample=[&](std::string phase,bool checkpoint=false){
        // Fixture-owned diagnostics run at quiescent checkpoints in both runs.
        // They may expose lazy buffer wrappers; only the snapshot counts bytes.
        if(checkpoint&&!observationPath.empty()){
            const auto observation=http(origin+observationPath,"POST");
            require(observation.is_object()&&observation.value("status",std::string{})=="passed","checkpoint observation failed");
            observations.push_back({{"phase",phase},{"result",observation}});
        }
        if(!session)return;const auto heap=session->Call("Runtime.getHeapUsage");
        // V8 heap allocations and ArrayBuffer/Wasm backing stores are separate
        // CDP counters. Never confuse either with process RSS.
        for(const auto field:{"usedSize","totalSize","embedderHeapUsedSize","backingStorageSize"})require(heap.contains(field)&&heap.at(field).is_number(),"incomplete whole-isolate counters");
        const auto sum=heap.at("totalSize").get<double>()+heap.at("embedderHeapUsedSize").get<double>()+heap.at("backingStorageSize").get<double>();require(sum>=0,"invalid heap counters");maximum=std::max(maximum,sum);samples.push_back({{"phase",phase},{"heap",heap},{"accountedBytes",sum}});
        // Preserve the heap observation before the diagnostic snapshot. Never
        // instrument a frozen application's constructors or private instances.
        if(nativeWasm&&checkpoint){
            require(!phase.empty()&&phase.size()<=128&&phase.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==phase.npos,"Invalid snapshot checkpoint identity");
            samples.back()["wasmInventory"]=session->WasmMemoryInventory(output/("heap-"+phase));
        }
    };
    sample("cold",true);const auto readyMs=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
    for(const auto& step:recipe.at("steps")){const auto repetitions=step.value("repeat",1u);require(repetitions>0&&repetitions<=512,"invalid profile repetition");
     for(unsigned index=0;index<repetitions;++index){const auto suffix=repetitions==1?std::string{}:"/"+std::to_string(index);const auto path=step.at("path").get<std::string>()+suffix;require(WorkerdProfilePathAdmitted(path),"invalid profile step");const auto id=step.at("id").get<std::string>()+(repetitions==1?std::string{}:"-"+std::to_string(index));const auto start=Clock::now();
        require(results.size()<512,"profile action count exceeded");
        const auto bodyName=step.value("requestBody",std::string{});require(bodyName.empty()||fs::path(bodyName).filename()==bodyName,"profile body identity");
        const auto bodyFile=bodyName.empty()?fs::path{}:config.parent_path()/bodyName;
        // Supported-limit streaming and sealing are measured, not timed as a
        // service SLA. Allow the native fixture to finish under observation;
        // product CPU, payload and memory admission remain unchanged.
        auto pending=std::async(std::launch::async,[&]{return http(origin+path,"POST",60,bodyFile);});
        Json result;
        try {
            if(session)while(pending.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready)sample(id);
            result=pending.get();
        }
        catch(const std::exception& error){
            write(output/"failure.json",{{"status","failed"},{"step",id},{"path",path},
              {"completedSteps",results.size()},{"wallMs",std::chrono::duration<double,std::milli>(Clock::now()-start).count()},
              {"error",error.what()}});
            throw;
        }
        require(result.at("status")=="passed","worker profile action failed");sample(id+"-complete",true);results.push_back({{"id",id},{"wallMs",std::chrono::duration<double,std::milli>(Clock::now()-start).count()},{"result",result}});
     }
    }
    const bool inspectorClosed=!session||session->Close();
    session.reset();const auto childPid=child.pid;child.close();
    const bool clean=inspectorClosed&&child.stopped.reaped&&!child.stopped.forced&&child.stopped.error==0;
    Json artifacts=Json::array();
    const auto declared=recipe.value("outputArtifacts",Json::array());require(declared.is_array()&&declared.size()<=16,"profile artifact count");
    for(const auto& entry:declared){
        const auto name=entry.at("path").get<std::string>();const fs::path relative=name;
        require(!name.empty()&&!relative.is_absolute()&&relative.lexically_normal()==relative,"profile artifact path");
        for(const auto& part:relative)require(part!=".."&&part!=".","profile artifact traversal");
        const auto root=fs::canonical(output/"state"),file=fs::canonical(root/relative);
        const auto confined=file.lexically_relative(root);require(!confined.empty()&&*confined.begin()!=".."&&fs::is_regular_file(file),"profile artifact escape");
        const auto bytes=fs::file_size(file);require(entry.at("bytes").is_number_unsigned()&&bytes==entry.at("bytes").get<std::uint64_t>(),"profile artifact length");
        const auto digest=hash(file);require(digest==entry.at("sha256").get<std::string>(),"profile artifact bytes changed");artifacts.push_back({{"path",name},{"bytes",bytes},{"sha256",digest}});
    }
    const Json report{{"status",clean?"passed":"failed"},{"inspected",inspect},{"inspectorClosed",inspectorClosed},{"readyMs",readyMs},{"maximumAccountedBytes",maximum},{"samples",samples},{"results",results},{"artifacts",artifacts},{"childPid",childPid},{"childReaped",child.stopped.reaped},{"childForced",child.stopped.forced},{"childError",child.stopped.error},{"childStatus",child.stopped.status}};
    auto evidence=report;evidence["terminationSignals"]=child.stopped.terminationSignals;
    evidence["checkpointObservations"]=observations;
    write(output/"profile.json",evidence);require(clean,"workerd cleanup failed");return evidence;
}
}
int WorkerdSignalMaskProbe(){sigset_t mask;require(pthread_sigmask(SIG_BLOCK,nullptr,&mask)==0,"signal probe failed");return sigismember(&mask,SIGTERM)==1?0:2;}
void WorkerdSpawnSelfTest(const char* executable){
    const auto path=fs::canonical(executable).string();
    for(const bool capture:{false,true}){
        WorkerdSpawnAttributes attributes(capture);pid_t child=-1;
        std::array<char*,3> args{const_cast<char*>(path.c_str()),const_cast<char*>("--workerd-signal-mask-probe"),nullptr};
        require(posix_spawn(&child,path.c_str(),nullptr,&attributes.value,args.data(),environ)==0,"signal probe spawn");
        const auto result=arken::codecast::native_browser::StopOwnedChild(child,std::chrono::milliseconds(1000));
        require(result.reaped&&!result.forced&&!result.error&&WIFEXITED(result.status)&&WEXITSTATUS(result.status)==(capture?0:2),"workerd pre-main signal mask contract");
    }
}
int WorkerdProfile(int argc,char** argv){
    require(argc==6,"Usage: arken-codecast-cdp --workerd-profile WORKERD CONFIG RECIPE NEW_OUTPUT");
    const auto executable=fs::canonical(argv[2]).string();const fs::path config=fs::canonical(argv[3]),output=fs::absolute(argv[5]);const auto recipe=Json::parse(read(argv[4]));
    require(recipe.at("schema")=="arken.codecast.workerd-profile.v1"&&recipe.at("steps").is_array(),"workerd profile recipe");
    require(recipe.value("diskState",true)||recipe.value("outputArtifacts",Json::array()).empty(),"stateless profile cannot declare disk artifacts");
    require(WorkerdProfileStepCountAdmitted(recipe.at("steps").size()),"workerd profile requires 1..128 steps");require(!fs::exists(output),"workerd profile output exists");
    const auto observationPath=recipe.value("checkpointObservationPath",std::string{});
    require(observationPath.empty()||WorkerdProfilePathAdmitted(observationPath),"invalid checkpoint observation path");
    const auto limit=recipe.at("maximumAccountedBytes").get<double>();require(limit>0&&limit<=128*1024*1024,"invalid memory budget");
    const auto identityPath=config.parent_path()/"identity.json";const auto identityText=read(identityPath);const auto identity=Json::parse(identityText);
    for(const auto& step:recipe.at("steps")){const auto name=step.value("requestBody",std::string{});if(!name.empty())require(identity.contains(name),"unfenced profile request body");}
    const auto verifyInputs=[&]{require(read(identityPath)==identityText,"profile package identity changed");for(const auto& [name,value]:identity.items())if(name!="releaseId"){
        require(fs::path(name).filename()==name&&value.is_string(),"invalid profile module identity");require(hash(config.parent_path()/name)==value.get<std::string>(),"profile module bytes changed");}};verifyInputs();
    fs::create_directories(output);chmod(output.c_str(),0700);
    const auto baseline=one(executable,config,recipe,output/"baseline",false),observed=one(executable,config,recipe,output/"observed",true);
    require(baseline.at("results").size()==observed.at("results").size(),"profile behavior count changed");
    for(std::size_t i=0;i<baseline.at("results").size();++i)require(baseline.at("results")[i].at("result")==observed.at("results")[i].at("result"),"inspector changed behavior");
    require(baseline.at("artifacts")==observed.at("artifacts"),"inspector changed output artifacts");
    require(baseline.at("checkpointObservations")==observed.at("checkpointObservations"),"inspector changed checkpoint observations");
    Json calibrationRecipe=recipe;calibrationRecipe["steps"]=recipe.at("calibrationSteps");calibrationRecipe.erase("outputArtifacts");const auto calibration=one(executable,config,calibrationRecipe,output/"calibration",true);
    const bool nativeWasm=recipe.value("nativeWasmInventory",false);
    double previous=calibration.at("samples")[0].at("heap").at("backingStorageSize");
    double previousWasm=nativeWasm?calibration.at("samples")[0].at("wasmInventory").at("bytes").get<double>():0;
    Json deltas=Json::array();bool wasmIncluded=false,wasmCalibrated=false,arrayCalibrated=false;
    for(const auto& step:calibrationRecipe.at("steps")){const auto phase=step.at("id").get<std::string>()+"-complete";bool found=false;for(const auto& row:calibration.at("samples"))if(row.at("phase")==phase){
        const double current=row.at("heap").at("backingStorageSize"),difference=current-previous,wanted=step.at("expectedBackingIncrease");
        const bool included=difference>=wanted-65536&&difference<=wanted+1048576;
        if(step.at("kind")=="arrayBuffer"){require(included,"inspector ArrayBuffer calibration failed");arrayCalibrated=true;}
        else{require(step.at("kind")=="wasm"&&(included||std::abs(difference)<=65536),"inspector Wasm calibration ambiguous");wasmIncluded=included;wasmCalibrated=true;}
        deltas.push_back({{"phase",phase},{"observedIncrease",difference},{"expectedIncrease",wanted},{"includedInBackingStore",included}});
        if(nativeWasm){
            const double currentWasm=row.at("wasmInventory").at("bytes"),increase=currentWasm-previousWasm;
            require(increase==(step.at("kind")=="wasm"?wanted:0),"native Wasm inventory calibration failed");
            deltas.back()["nativeWasmIncrease"]=increase;previousWasm=currentWasm;
        }
        previous=current;found=true;break;}
        require(found,"calibration sample missing");}
    require(arrayCalibrated&&wasmCalibrated,"memory calibration incomplete");double wasmBytes=0;
    if(nativeWasm){for(const auto& sample:observed.at("samples"))if(sample.contains("wasmInventory"))wasmBytes=std::max(wasmBytes,sample.at("wasmInventory").at("bytes").get<double>());}
    else for(const auto& step:observed.at("results"))wasmBytes=std::max(wasmBytes,step.at("result").at("wasmBytes").get<double>());
    require(wasmBytes>0&&wasmBytes<128*1024*1024,"missing runtime Wasm inventory");
    // Linear memories only grow in this retained fixture. Add their maximum
    // allocation to every sampled inspector peak when calibration proves V8
    // omits them. This is conservative, and cannot double-count on a runtime
    // that includes Wasm backing stores.
    const auto inspectorBytes=observed.at("maximumAccountedBytes").get<double>(),supplement=wasmIncluded?0:wasmBytes,bytes=inspectorBytes+supplement;
    require(bytes<limit,"workerd memory budget exceeded");verifyInputs();
    const Json result{{"schema","arken.codecast.workerd-resource.v1"},{"status","passed"},{"releaseId",identity.at("releaseId")},{"packageIdentitySha256",hash(identityPath)},{"maximumAccountedBytes",bytes},{"maximumInspectorBytes",inspectorBytes},{"maximumWasmBytes",wasmBytes},{"supplementalWasmBytes",supplement},{"accounting","sampled allocated V8 heap + embedder heap + backing storage, plus retained runtime Wasm high-water when omitted by calibrated inspector"},{"limitBytes",limit},{"behaviorNeutral",true},{"externalNetwork","denied by fixture"},{"childReaped",true},{"baselineReadyMs",baseline.at("readyMs")},{"observedReadyMs",observed.at("readyMs")},{"sampleCount",observed.at("samples").size()},{"calibration",deltas},{"executable",executable},{"executableSha256",hash(executable)},{"configSha256",hash(config)},{"recipeSha256",hash(argv[4])}};
    auto proof=result;
    proof["requestObservationDeadlineMs"]=60000;
    proof["profiledWorker"]=recipe.at("workerName");
    proof["fixtureWorkers"]=recipe.value("fixtureWorkers",Json::array());
    proof["wasmInventoryAuthority"]=nativeWasm?"native-cdp-heap-snapshot":"owned-runtime-diagnostic";
    // Generic measured results, not caller assertions. Product owners can
    // qualify the workload they require without teaching this observer domains.
    proof["workloadResults"]=observed.at("results");
    proof["artifactResults"]=observed.at("artifacts");
    proof["checkpointObservations"]=observed.at("checkpointObservations");
    write(output/"proof.json",proof);std::cout<<proof.dump(2)<<'\n';return 0;
}
