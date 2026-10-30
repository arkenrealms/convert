#include "cdp_page_session.hpp"
#include "../native_browser/heap_snapshot.hpp"
#include "heap_wasm_inventory.hpp"
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <sys/stat.h>
#import <Foundation/Foundation.h>
@interface ArkenCdpSessionDelegate : NSObject <NSURLSessionWebSocketDelegate>
@property(nonatomic,strong) dispatch_semaphore_t closed;
@property(nonatomic,strong) dispatch_semaphore_t invalidated;
@end
@implementation ArkenCdpSessionDelegate
- (instancetype)init { self=[super init];if(self){self.closed=dispatch_semaphore_create(0);self.invalidated=dispatch_semaphore_create(0);}return self; }
- (void)URLSession:(NSURLSession*)session webSocketTask:(NSURLSessionWebSocketTask*)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData*)reason { dispatch_semaphore_signal(self.closed); }
- (void)URLSession:(NSURLSession*)session didBecomeInvalidWithError:(NSError*)error { dispatch_semaphore_signal(self.invalidated); }
@end
namespace arken::codecast::proof {
namespace {void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}}
bool CdpPageMethodAllowed(std::string_view method,CdpPageAccess access) noexcept {
    if(access==CdpPageAccess::worker_memory)return method=="Runtime.getHeapUsage";
    if(method=="Page.enable"||method=="Page.getResourceTree"||method=="Page.getResourceContent")return true;
    if(access==CdpPageAccess::assets)return false;
    if(method=="Runtime.evaluate"||method=="Runtime.getProperties"||method=="DOM.enable"||method=="DOM.getDocument"||method=="DOM.querySelector"||method=="DOM.querySelectorAll"||method=="DOM.getAttributes"||method=="DOM.getBoxModel"||method=="DOM.getOuterHTML"||method=="CSS.enable"||method=="CSS.getComputedStyleForNode"||method=="Page.captureScreenshot"||method=="Page.getLayoutMetrics")return true;
    return access==CdpPageAccess::ui_inspection&&(method=="Input.dispatchMouseEvent"||method=="Input.dispatchKeyEvent");
}
bool CdpPageEndpointValid(std::string_view value){
    const std::string endpoint(value);NSURL* url=[NSURL URLWithString:[NSString stringWithUTF8String:endpoint.c_str()]];
    return [[url scheme] isEqualToString:@"ws"]&&([[url host] isEqualToString:@"127.0.0.1"]||[[url host] isEqualToString:@"::1"]||[[url host] isEqualToString:@"[::1]"])&&[url port]!=nil&&[url user]==nil&&[url password]==nil&&[[url path] hasPrefix:@"/devtools/page/"]&&[url query]==nil&&[url fragment]==nil;
}
bool CdpWorkerEndpointValid(std::string_view value){
    const std::string endpoint(value);NSURL* url=[NSURL URLWithString:[NSString stringWithUTF8String:endpoint.c_str()]];
    const auto path=std::string([[url path] UTF8String]?:"");
    return [[url scheme] isEqualToString:@"ws"]&&[[url host] isEqualToString:@"127.0.0.1"]&&[url port]!=nil&&[url user]==nil&&[url password]==nil&&[url query]==nil&&[url fragment]==nil
        &&path.size()>1&&path.size()<200&&!path.starts_with("/devtools/")&&path.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_:.")==path.npos;
}
struct CdpPageSession::Impl {
    NSURLSession* session;
    NSURLSessionWebSocketTask* socket;
    ArkenCdpSessionDelegate* lifecycle;
    CdpPageAccess access;
    int sequence{};
    bool closing=false,cleanClose=false;
    native_browser::HeapSnapshotRecorder heapSnapshot{48u*1024u*1024u};
    Impl(const std::string& endpoint,CdpPageAccess mode):access(mode){
        Require(mode==CdpPageAccess::worker_memory?CdpWorkerEndpointValid(endpoint):CdpPageEndpointValid(endpoint),"Explicit loopback CDP endpoint required");
        lifecycle=[ArkenCdpSessionDelegate new];
        session=[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration] delegate:lifecycle delegateQueue:nil];
        socket=[session webSocketTaskWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:endpoint.c_str()]]];
        // Match the existing bounded response contract instead of NSURLSession's
        // smaller default, which rejects a large page resource tree before parsing.
        socket.maximumMessageSize=48u*1024u*1024u;[socket resume];
    }
    bool Close() noexcept {
        if(closing)return cleanClose;closing=true;
        [socket cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
        const bool acknowledged=dispatch_semaphore_wait(lifecycle.closed,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))==0;
        if(acknowledged)[session finishTasksAndInvalidate];else [session invalidateAndCancel];
        const bool invalidated=dispatch_semaphore_wait(lifecycle.invalidated,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))==0;
        cleanClose=acknowledged&&invalidated;return cleanClose;
    }
    ~Impl(){if(!Close())std::cerr<<"CDP session close was not acknowledged\n";}
    nlohmann::json Call(std::string_view method,nlohmann::json params){
        Require(!closing,"CDP session is closed");
        Require(CdpPageMethodAllowed(method,access),"CDP method is outside the admitted page inspection capability");
        if(method=="Runtime.evaluate"){
            Require(params.value("expression",std::string()).size()<=128*1024,"Inspection expression budget");
            params["throwOnSideEffect"]=true;params["returnByValue"]=true;params["silent"]=true;params["timeout"]=5000;
            params.erase("awaitPromise");
        }
        return Send(method,std::move(params));
    }
    nlohmann::json Send(std::string_view method,nlohmann::json params,bool snapshot=false){
        using Json=nlohmann::json;
        Require(!closing,"CDP session is closed");
        const int id=++sequence;const auto request=Json{{"id",id},{"method",method},{"params",std::move(params)}}.dump();
        Require(request.size()<=256*1024,"CDP request budget");
        auto sent=dispatch_semaphore_create(0);__block NSString* sendError=nil;
        [socket sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:[NSString stringWithUTF8String:request.c_str()]] completionHandler:^(NSError* error){sendError=error.localizedDescription;dispatch_semaphore_signal(sent);}];
        Require(dispatch_semaphore_wait(sent,dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC))==0,"CDP send timed out");
        Require(sendError==nil,"CDP send failed");
        // A memory sample can queue behind the owned Workerd request. Its
        // observation deadline must cover that request's 60-second deadline;
        // this does not extend the request or change its resource budget.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(snapshot?60:access==CdpPageAccess::worker_memory?75:15);
        for(int count=0;count<(snapshot?4096:500);++count){
            const auto left=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();Require(left>0,"CDP response deadline");
            auto received=dispatch_semaphore_create(0);__block NSURLSessionWebSocketMessage* response=nil;__block NSError* receiveError=nil;
            [socket receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message,NSError* error){response=message;receiveError=error;dispatch_semaphore_signal(received);}];
            Require(dispatch_semaphore_wait(received,dispatch_time(DISPATCH_TIME_NOW,left))==0,"CDP receive timed out");
            Require(receiveError==nil,"CDP receive failed: "+std::string(receiveError.localizedDescription.UTF8String?:"unknown transport error"));
            NSString* text=response.string?response.string:[[NSString alloc] initWithData:response.data encoding:NSUTF8StringEncoding];
            Require(text!=nil&&[text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]<=48u*1024u*1024u,"CDP response budget");
            auto packet=Json::parse([text UTF8String]);
            if(packet.value("method",std::string{})=="HeapProfiler.addHeapSnapshotChunk")
                heapSnapshot.Chunk(packet.at("params").at("chunk").get_ref<const std::string&>());
            if(packet.value("id",0)!=id)continue;
            Require(!packet.contains("error"),"CDP command rejected: "+packet.value("error",Json::object()).dump());
            // Workerd's owned snapshot dispatcher acknowledges the command
            // envelope without a result object after streaming its chunks.
            if(snapshot&&!packet.contains("result")){
                Require(packet.value("method",std::string{})=="HeapProfiler.takeHeapSnapshot","Invalid Workerd snapshot acknowledgement");
                return Json::object();
            }
            return packet.at("result");
        }
        throw std::runtime_error("CDP response event budget");
    }
    nlohmann::json WasmMemoryInventory(const std::filesystem::path& output){
        using Json=nlohmann::json;
        Require(access==CdpPageAccess::worker_memory,"Wasm inventory requires owned Worker memory access");
        Require(std::filesystem::create_directory(output),"Heap snapshot output already exists");
        Require(chmod(output.c_str(),0700)==0,"Heap snapshot private directory failed");
        heapSnapshot.Begin(output);
        try{
            Send("HeapProfiler.enable",Json::object());
            Send("HeapProfiler.takeHeapSnapshot",{{"reportProgress",false}},true);
            Send("HeapProfiler.disable",Json::object());
            const auto receipt=heapSnapshot.Finish();
            Require(receipt.at("bytes").get<std::size_t>()>0,"Empty snapshot");
            auto inventory=ReadWasmInventory(output/"page.heapsnapshot");
            inventory["snapshot"]=receipt;return inventory;
        }catch(...){
            heapSnapshot.Cancel();
            try{Send("HeapProfiler.disable",Json::object());}
            catch(const std::exception& error){std::cerr<<"Heap profiler cleanup failed: "<<error.what()<<'\n';}
            throw;
        }
    }
};
CdpPageSession::CdpPageSession(const std::string& endpoint,CdpPageAccess access):impl_(std::make_unique<Impl>(endpoint,access)){}
CdpPageSession::~CdpPageSession()=default;
bool CdpPageSession::Close() noexcept{return impl_->Close();}
nlohmann::json CdpPageSession::WasmMemoryInventory(const std::filesystem::path& output){return impl_->WasmMemoryInventory(output);}
nlohmann::json CdpPageSession::Call(std::string_view method,nlohmann::json params){return impl_->Call(method,std::move(params));}
}
