#pragma once
#include "api.hpp"
namespace arken::cloudflare::v1 {
// Native tooling only. Shell-free bounded spawn; exclusive private log and
// cancellation confined to the exact unreaped child/process group.
void RunQualificationProcess(const fs::path&,const std::vector<std::string>&,const fs::path& log,unsigned timeoutSeconds);
struct WorkerResourceInput {fs::path package,workerd,profiler,state;};
// Consumes a trusted family-prepared flat fixture, not a caller success file.
// Measures once, then reuses evidence only while package and live tools match.
Json QualifyWorkerResources(const WorkerResourceInput&);
// Verify the original intent/receipt/proof and unchanged package without tool
// execution or dependence on the continued availability of historical binaries.
// Supplied paths must still match the accepted identity exactly.
Json VerifyWorkerResourceQualification(const WorkerResourceInput&);
}
