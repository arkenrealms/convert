#pragma once
#include "source_guard.hpp"
namespace arken::mcp::economics::cloudflare::v1 {
struct MaintenanceReplacement {std::string name,mediaType;path source;std::string sha256;};
struct ResourceActor {std::string binding,klass,key;};
enum class MaintenanceState { financialJournal, stateless };
struct MaintenanceResources {
 std::string factory;
 std::string checkpointObservationPath;
 std::vector<MaintenanceReplacement> fixtures;
 std::vector<ResourceActor> actors;
 std::map<std::string,std::string> textBindings;
 std::vector<std::string> phases;
 MaintenanceState state=MaintenanceState::financialJournal;
 std::string fixtureOutbound;
};
Json PrepareSourceGuardResources(const SourceGuardPackage&,const SourceGuardConfiguration&,
 const path& guardProof,const path& source,const MaintenanceResources&,const path& output);
}
