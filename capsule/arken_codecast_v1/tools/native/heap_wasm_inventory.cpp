#include "heap_wasm_inventory.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace arken::codecast::proof {
namespace {
using Json=nlohmann::json;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::size_t Unsigned(const Json& value){
    Require(value.is_number_unsigned(),"Heap graph requires unsigned integers");
    const auto result=value.get<std::uint64_t>();
    Require(result<=9007199254740991ull,"Heap graph integer exceeds exact range");
    return static_cast<std::size_t>(result);
}
std::size_t Field(const Json& fields,std::string_view name){
    Require(fields.is_array()&&!fields.empty()&&fields.size()<=32,"Invalid heap graph field metadata");
    std::size_t found=fields.size();
    for(std::size_t i=0;i<fields.size();++i){
        Require(fields[i].is_string(),"Invalid heap graph field name");
        if(fields[i]==name){Require(found==fields.size(),"Duplicate heap graph field");found=i;}
    }
    Require(found<fields.size(),"Required heap graph field absent");return found;
}
struct Graph {
    const Json &nodes,&edges,&strings,&nodeTypes,&edgeTypes;
    std::size_t nw,ew,nt,nn,ns,ne,et,en,ed,count;
    std::vector<std::size_t> starts;
    explicit Graph(const Json& data):nodes(data.at("nodes")),edges(data.at("edges")),strings(data.at("strings")),
        nodeTypes(data.at("snapshot").at("meta").at("node_types")),edgeTypes(data.at("snapshot").at("meta").at("edge_types")){
        const auto& meta=data.at("snapshot").at("meta");
        const auto& nf=meta.at("node_fields");const auto& ef=meta.at("edge_fields");
        nt=Field(nf,"type");nn=Field(nf,"name");ns=Field(nf,"self_size");ne=Field(nf,"edge_count");
        et=Field(ef,"type");en=Field(ef,"name_or_index");ed=Field(ef,"to_node");nw=nf.size();ew=ef.size();
        count=Unsigned(data.at("snapshot").at("node_count"));const auto edgeCount=Unsigned(data.at("snapshot").at("edge_count"));
        Require(count>0&&count<=1000000&&edgeCount<=8000000,"Heap graph count budget");
        Require(nodes.is_array()&&edges.is_array()&&nodes.size()==count*nw&&edges.size()==edgeCount*ew,"Heap graph array length mismatch");
        Require(strings.is_array()&&strings.size()<=1000000,"Heap graph string budget");
        Require(nodeTypes.is_array()&&nodeTypes.size()==nw&&nodeTypes[nt].is_array()&&edgeTypes.is_array()&&edgeTypes.size()==ew&&edgeTypes[et].is_array(),"Heap graph type metadata");
        for(const auto& string:strings)Require(string.is_string(),"Invalid heap graph string");
        starts.reserve(count+1);std::size_t cursor=0;
        for(std::size_t n=0;n<count;++n){
            starts.push_back(cursor);type(n);name(n);Unsigned(nodes[n*nw+ns]);
            const auto outgoing=Unsigned(nodes[n*nw+ne]);
            Require(outgoing<=(edges.size()-cursor)/ew,"Heap graph edge count overflow");cursor+=outgoing*ew;
        }
        Require(cursor==edges.size(),"Heap graph edge totals mismatch");starts.push_back(cursor);
        for(std::size_t e=0;e<edges.size();e+=ew){
            const auto kind=edgeType(e);target(e);const auto value=Unsigned(edges[e+en]);
            if(kind!="element"&&kind!="hidden")Require(value<strings.size(),"Heap graph edge string index");
        }
    }
    std::string_view type(std::size_t n)const{
        const auto t=Unsigned(nodes[n*nw+nt]);Require(t<nodeTypes[nt].size()&&nodeTypes[nt][t].is_string(),"Heap graph node type");
        return nodeTypes[nt][t].get_ref<const std::string&>();
    }
    std::string_view name(std::size_t n)const{
        const auto i=Unsigned(nodes[n*nw+nn]);Require(i<strings.size(),"Heap graph node name");return strings[i].get_ref<const std::string&>();
    }
    std::string_view edgeType(std::size_t e)const{
        const auto t=Unsigned(edges[e+et]);Require(t<edgeTypes[et].size()&&edgeTypes[et][t].is_string(),"Heap graph edge type");
        return edgeTypes[et][t].get_ref<const std::string&>();
    }
    std::string_view edgeName(std::size_t e)const{
        const auto i=Unsigned(edges[e+en]);Require(i<strings.size(),"Heap graph edge name");return strings[i].get_ref<const std::string&>();
    }
    std::size_t target(std::size_t e)const{
        const auto offset=Unsigned(edges[e+ed]);Require(offset<nodes.size()&&offset%nw==0,"Heap graph target misaligned or outside nodes");return offset/nw;
    }
};
}
nlohmann::json WasmInventoryFromSnapshot(const nlohmann::json& snapshot){
    const Graph g(snapshot);std::set<std::size_t> backings;std::size_t instances=0;std::uint64_t total=0;
    for(std::size_t n=0;n<g.count;++n){
        if(g.type(n)!="object"||g.name(n)!="Memory")continue;
        bool prototype=false,nativeStore=false;std::vector<std::size_t> buffers;
        for(auto e=g.starts[n];e<g.starts[n+1];e+=g.ew){
            const auto target=g.target(e);const auto kind=g.edgeType(e);
            if(kind=="property"&&g.edgeName(e)=="__proto__"&&g.type(target)=="object"&&g.name(target)=="WebAssembly.Memory")prototype=true;
            if(kind=="hidden"&&g.type(target)=="object"&&(g.name(target)=="ArrayBuffer"||g.name(target)=="SharedArrayBuffer"))buffers.push_back(target);
            if(kind=="hidden"&&g.type(target)=="native"&&g.name(target)=="system / CppGCManaged (BackingStore)")nativeStore=true;
        }
        if(!prototype)continue;
        Require(nativeStore&&buffers.size()==1,"Wasm memory has unsupported native backing relationship");
        Require(++instances<=256,"Wasm memory instance budget");
        const auto buffer=buffers.front();std::size_t backingCount=0;
        for(auto e=g.starts[buffer];e<g.starts[buffer+1];e+=g.ew){
            if(g.edgeType(e)!="internal"||g.edgeName(e)!="backing_store")continue;
            const auto target=g.target(e);++backingCount;
            Require(g.type(target)=="native"&&g.name(target)=="system / JSArrayBufferData","Unsupported Wasm backing node");
            const auto bytes=Unsigned(g.nodes[target*g.nw+g.ns]);
            Require(bytes>0&&bytes<=4294967296ull&&bytes%65536==0,"Invalid Wasm backing allocation");
            if(backings.insert(target).second)total+=bytes;
        }
        Require(backingCount==1,"Wasm backing allocation absent or ambiguous");
    }
    return {{"bytes",total},{"instances",instances},{"backingStores",backings.size()},{"nodeCount",g.count},{"edgeCount",g.edges.size()/g.ew}};
}
nlohmann::json ReadWasmInventory(const std::filesystem::path& path){
    Require(std::filesystem::is_regular_file(std::filesystem::symlink_status(path))&&std::filesystem::file_size(path)<=48u*1024u*1024u,"Heap snapshot input budget or type");
    std::ifstream input(path,std::ios::binary);Require(bool(input),"Heap snapshot unavailable");
    try{
        auto data=Json::parse(input,[](int depth,Json::parse_event_t,Json&){Require(depth<=64,"Heap snapshot nesting budget");return true;});
        return WasmInventoryFromSnapshot(data);
    }catch(const Json::exception&){throw std::runtime_error("Malformed heap snapshot graph");}
}
}
