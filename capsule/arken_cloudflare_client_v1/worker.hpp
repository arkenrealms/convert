#pragma once
#include "api.hpp"
namespace arken::cloudflare::v1 {
// Native release-file bounds, distinct from runtime memory and public RPC limits.
// Every upload, immutable read-back and rollback reader uses the same envelope.
inline constexpr std::size_t workerModuleByteLimit=16u*1024u*1024u;
inline constexpr std::size_t workerPackageByteLimit=32u*1024u*1024u;
inline constexpr std::size_t workerManagementResponseLimit=workerPackageByteLimit+2u*1024u*1024u;
// Version JSON includes base64-encoded modules; keep this management-only.
inline constexpr std::size_t workerVersionResponseLimit=((workerPackageByteLimit+2u)/3u)*4u+2u*1024u*1024u;
struct WorkerDurableBinding { std::string name, className; };
struct WorkerServiceBinding { std::string name, service, entrypoint; };
// identity is obtained from the accepted private-bucket transaction. Every
// planning, activation, recovery and audit re-observes it through Cloudflare.
struct WorkerR2Binding { std::string name; Json identity; };
// Cloudflare's per-location abuse limiter, not an exact account/economy quota.
struct WorkerRateLimitBinding { std::string name, namespaceId; unsigned limit = 0, periodSeconds = 0; };
struct WorkerUpdateAdditions {
  std::vector<std::string> entrypoints;
  std::vector<WorkerServiceBinding> serviceBindings;
  std::vector<WorkerR2Binding> privateR2Bindings{};
  std::vector<std::string> compatibilityFlags{};
  std::vector<WorkerRateLimitBinding> rateLimitBindings{};
};
struct WorkerInstallTarget {
  std::string accountId, worker, mainModule, compatibilityDate;
  // An empty primary pair declares a stateless Worker. Partial pairs reject.
  std::string durableBinding, durableClass, admissionBinding;
  unsigned cpuMilliseconds = 1000;
  std::vector<WorkerDurableBinding> additionalDurables{};
  std::vector<WorkerServiceBinding> serviceBindings{};
  std::vector<std::string> workerEntrypoints{};
  std::vector<WorkerR2Binding> privateR2Bindings{};
  std::vector<std::string> compatibilityFlags{};
  std::vector<WorkerRateLimitBinding> rateLimitBindings{};
};
// Pure validated canonical identity, shared by receipt readers and composers.
// No source, filesystem, account or provider access.
Json WorkerTargetContract(const WorkerInstallTarget&);
// Decode the exact canonical target shape; reject unknown keys and ignored
// optional fields instead of silently dropping deployment resources.
WorkerInstallTarget WorkerTargetFromContract(const Json&);
struct WorkerModule {
  std::string name, mediaType;
  fs::path source;
  bool requiresProof = true;
};
using WorkerBindings =
    std::function<std::map<std::string, std::string>(std::string_view release)>;
using WorkerSourceReader = std::function<Json(const fs::path &)>;
using WorkerSecrets = std::map<std::string, std::string>;
using WorkerReadinessCheck = std::function<Json()>;
Json WorkerSourceIdentity(const fs::path &);
// Create-only, admission-disabled transaction. No secrets, DNS, R2 mutations, deletion,
// replacement or payment. Source overrides are an owned native test seam.
Json PlanWorkerInstall(const Transport &, std::string_view token,
                       const WorkerInstallTarget &, const fs::path &state,
                       const fs::path &source,
                       const std::vector<WorkerModule> &, const fs::path &proof,
                       const WorkerBindings &,
                       WorkerSourceReader = WorkerSourceIdentity,
                       const WorkerReadinessCheck &serviceDependency = {},
                       const WorkerReadinessCheck &prerequisite = {});
Json ApplyWorkerInstall(const Transport &, std::string_view token,
                        const fs::path &state, std::string_view confirmation,
                        bool recover = false,
                        WorkerSourceReader = WorkerSourceIdentity,
                        const WorkerReadinessCheck &serviceDependency = {},
                        const WorkerReadinessCheck &prerequisite = {});
// One first SQLite namespace on an accepted stateless, secret-free, disabled
// predecessor. Atomic script upload, NOT a staged version update. All other
// target fields and existing routes/domains/script settings remain unchanged.
// Unknown script settings reject. No admission, deletion or rollback across
// this lifecycle floor. Recovery only observes; it never repeats the PUT.
// Pre/post checks detect drift; they are not a provider-side deployment CAS.
Json PlanWorkerFirstDurable(const Transport &, std::string_view token,
                           const fs::path &priorState, const fs::path &state,
                           const fs::path &source, const WorkerDurableBinding &,
                           const std::vector<WorkerModule> &, const fs::path &proof,
                           const WorkerBindings &,
                           WorkerSourceReader = WorkerSourceIdentity,
                           const WorkerReadinessCheck &serviceDependency = {},
                           const WorkerReadinessCheck &prerequisite = {});
Json ApplyWorkerFirstDurable(const Transport &, std::string_view token,
                            const fs::path &state, std::string_view confirmation,
                            bool recover = false,
                            WorkerSourceReader = WorkerSourceIdentity,
                            const WorkerReadinessCheck &serviceDependency = {},
                            const WorkerReadinessCheck &prerequisite = {});
Json AuditWorkerInstall(const Transport &, std::string_view token,
                        const fs::path &state,
                        const WorkerReadinessCheck &serviceDependency = {},
                        const WorkerReadinessCheck &prerequisite = {});
// Explicit metadata-only observation for an owned migration preimage. Never
// substitutes for dependency-current acceptance or changes local receipts.
Json InspectWorkerDeployment(const Transport&,std::string_view,const fs::path&);
// Receipt-independent GET-only evidence, never acceptance or adoption.
struct WorkerObservationIdentity { std::string accountId, worker, versionId, deploymentId; bool operator==(const WorkerObservationIdentity&) const = default; };
struct WorkerObservation { Json evidence; std::map<std::string,std::string> modules; };
// Bounded GET-only current configuration evidence. Rechecks the selected
// deployment and script settings; returns safe resource descriptors, never
// module bytes or binding values. Not an atomic snapshot or enforcement proof.
Json ObserveWorkerExecutionPolicy(const Transport&,std::string_view token,
                                 std::string_view accountId,std::string_view worker);
WorkerObservation ObserveWorkerWithoutReceipt(const Transport&,std::string_view,const WorkerObservationIdentity&);
// Cross-check immutable beta-version bytes against the selected deployment's
// strong-ETag script download. GET-only; still not historical acceptance.
WorkerObservation ObserveWorkerVersionWithoutReceipt(const Transport&,std::string_view,const WorkerObservationIdentity&);
// Explicit fresh-observation recovery boundary, not adoption of a lost receipt.
// Stage only: unchanged configuration/resources/existing bindings, exact-version secret
// inheritance, no traffic activation. Every replacement must have hash-bound
// passed proof. Additions are explicit: replacements must already exist and
// additions must not. No deletion, implicit addition, or media-type conversion.
// New nonsecret text bindings are separately explicit, bounded and collision-
// rejecting. Explicit replacements may change only an existing plain_text
// value, fenced by the complete predecessor. Unchanged values reject.
// Existing secrets and resource bindings always inherit unchanged.
// Named Worker entrypoint additions are explicit and collision-rejecting;
// existing exports, including Durable Objects, remain unchanged.
// Service additions require the owning dependency audit, including all existing
// service bindings. The frozen audit is rechecked at stage/activation/audit.
// A valid configuration-only addition retains the entire module package.
// Source overrides are an owned native test seam.
Json PlanObservedWorkerStage(const Transport&,std::string_view,const WorkerObservationIdentity&,
    const fs::path& state,const fs::path& source,const std::vector<WorkerModule>& replacements,
    const fs::path& proof,WorkerSourceReader=WorkerSourceIdentity,
    const std::vector<WorkerModule>& additions={},
    const std::map<std::string,std::string>& textBindingAdditions={},
    const std::vector<std::string>& entrypointAdditions={},
    const std::vector<WorkerServiceBinding>& serviceBindingAdditions={},
    const WorkerReadinessCheck& serviceDependency={},
    const std::map<std::string,std::string>& textBindingReplacements={});
Json StageObservedWorker(const Transport&,std::string_view,const fs::path& state,
    std::string_view confirmation,WorkerSourceReader=WorkerSourceIdentity,
    const WorkerReadinessCheck& serviceDependency={});
// GET-only diagnosis of an already uploaded candidate. No upload, activation,
// source mutation or binding values: mismatched binding fields expose hashes.
Json InspectObservedWorkerCandidate(const Transport&,std::string_view,const fs::path& state);
// Fresh acceptance of an already-staged, fully qualified immutable package.
// The product-owned check must exercise the exact version's live contract and
// return {status:"passed",versionId:expected,...}; never a caller-authored file.
// No generic CLI bypass: product owners provide this required capability.
using ObservedWorkerCheck=std::function<Json(std::string_view expectedVersion)>;
// Validated immutable local identity for product-owned checks. Only ordinary
// text bindings are exposed; secret descriptors/values never cross this API.
struct ObservedWorkerContract {
  std::string accountId, worker, predecessorVersion, candidateVersion;
  // Validated candidate values, including explicitly admitted changes.
  std::map<std::string,std::string> textBindings;
  // Validated frozen preimage, not an observation of today's selection.
  Json predecessorModules = Json::array();
  Json modules = Json::array();
  std::string packageSha256{}, stagePlanSha256{};
  fs::path packageRoot{};
};
ObservedWorkerContract ObservedWorkerStageContract(const fs::path& stagedState);
ObservedWorkerContract ObservedWorkerActivationContract(const fs::path& state,std::string_view confirmation);
// Explicit dependency renewal for a selected, not-yet-qualified activation.
// A product validator supplies the permitted transition and stable contract;
// this owner fences the unchanged candidate, bindings and selection. Empty
// renewalConfirmation previews; the returned hash admits only that snapshot.
Json RenewObservedWorkerDependency(const Transport&,std::string_view,const fs::path& state,
    std::string_view activationConfirmation,const Json& productContract,
    const WorkerReadinessCheck&,std::string_view renewalConfirmation={});
Json ObservedWorkerDependencyRenewal(const fs::path& state,std::string_view confirmation);
// GET-only current audit of an accepted activation. Rejects rollback, foreign
// deployment, package/settings drift and concurrent receipt changes; no apply.
Json AuditObservedWorkerActivation(const Transport&,std::string_view,const fs::path& state,
    std::string_view confirmation,const WorkerReadinessCheck& serviceDependency={});
Json PlanObservedWorkerActivation(const Transport&,std::string_view,const fs::path& stagedState,
    const fs::path& newState,const fs::path& source,const fs::path& qualification,
    const ObservedWorkerCheck&,WorkerSourceReader=WorkerSourceIdentity,
    const WorkerReadinessCheck& serviceDependency={});
Json ApplyObservedWorkerActivation(const Transport&,std::string_view,const fs::path& state,
    std::string_view confirmation,const ObservedWorkerCheck&,WorkerSourceReader=WorkerSourceIdentity,
    const WorkerReadinessCheck& serviceDependency={});
// Explicit rollback only for this operation's exact selected deployment. It
// creates a new deployment, never rolls back data, never forces provider guards,
// and never replays an uncertain POST. A completed rollback is terminal.
Json RollbackObservedWorkerActivation(const Transport&,std::string_view,const fs::path& state,
    std::string_view confirmation,const ObservedWorkerCheck&,
    const WorkerReadinessCheck& serviceDependency={});
Json WorkerInstallStatus(const fs::path &state);
// Offline, side-effect-free continuation for observed stage/activation receipts.
// Validates the saved package/evidence closure; never claims fresh cloud health.
// Pending effects require reconciliation through the same product-owned command.
Json ObservedWorkerProcedure(const fs::path& state);
// Offline projection of an accepted activation's hash-fenced dependency
// evidence. Does not assert today's provider health or restore lost receipts.
Json ObservedWorkerAcceptedDependency(const fs::path& state,std::string_view confirmation);
// Read-only validated configuration identity; never exposes binding values.
Json WorkerInstallContract(const fs::path &state);
// Receipt-fenced script-level log configuration; no code, version, binding,
// route, secret or payment mutation. Invocation logs and traces stay disabled.
Json PlanWorkerCallLogs(const Transport&,std::string_view,const fs::path& installedState,const fs::path& newState);
Json ApplyWorkerCallLogs(const Transport&,std::string_view,const fs::path& installedState,const fs::path& state,std::string_view confirmation);
// Same console-only policy, with honest observed activation authority. State
// lives in activationState/call-logs; accepted settings survive code rollback.
Json PlanObservedWorkerCallLogs(const Transport&,std::string_view,const fs::path& activationState,std::string_view activationConfirmation,
                              const WorkerReadinessCheck& serviceDependency={});
Json ApplyObservedWorkerCallLogs(const Transport&,std::string_view,const fs::path& activationState,
    std::string_view activationConfirmation,std::string_view logConfirmation,
    const WorkerReadinessCheck& serviceDependency={});
// Explicit provider CPU telemetry opt-in, never the console-only default.
// Chains after accepted call-logs (if present); preserves settings/deployment,
// redacts query strings, leaves tracing off, and never replays uncertain PATCH.
Json PlanObservedWorkerInvocationLogs(const Transport&,std::string_view,const fs::path& activationState,
    std::string_view activationConfirmation,const WorkerReadinessCheck& serviceDependency={});
Json ApplyObservedWorkerInvocationLogs(const Transport&,std::string_view,const fs::path& activationState,
    std::string_view activationConfirmation,std::string_view logConfirmation,
    const WorkerReadinessCheck& serviceDependency={});
// Validates the owned receipt/package before reading one ordinary text binding.
// Secret bindings are deliberately absent from this API.
std::string WorkerConfigurationBinding(const fs::path &state, std::string_view name);
// Existing namespace, always-disabled candidate. An enabled predecessor must
// have accepted workflow evidence; it stays selected until the candidate is
// atomically activated. Version-specific secrets never mutate the live version.
// Explicit additions may introduce named private entrypoints; existing exports
// namespaces and existing service/worker identities remain unchanged. Service
// additions require a stable owning audit {status, bindings, evidence}; bindings
// is the exact complete canonical service-binding list. This requirement is
// inherited by later updates, admission, recovery and audits.
// Non-service prerequisites are separate {status:"passed",evidence:{...}}
// records with bounded nonempty evidence, frozen and reobserved at the same
// mutation/acceptance boundaries; service-binding validation stays strict.
Json PlanWorkerUpdate(const Transport &, std::string_view token,
                      const fs::path &priorState, const fs::path &state,
                      const fs::path &source, const std::vector<WorkerModule> &,
                      const fs::path &proof, const WorkerBindings &,
                      const WorkerSecrets &,
                      WorkerSourceReader = WorkerSourceIdentity,
                      const WorkerUpdateAdditions &additions = {},
                      const WorkerReadinessCheck &serviceDependency = {},
                      const WorkerReadinessCheck &prerequisite = {});
// Explicit recovery for an enabled release whose final readiness failed. A
// sealed rejection is mandatory; never accepts success evidence or enables the
// replacement. No target additions are admitted through this repair boundary.
Json PlanWorkerDisabledRepair(const Transport &, std::string_view token,
                             const fs::path &priorState, const fs::path &state,
                             const fs::path &source, const std::vector<WorkerModule> &,
                             const fs::path &proof, const WorkerBindings &,
                             const WorkerSecrets &,
                             WorkerSourceReader = WorkerSourceIdentity,
                             const WorkerReadinessCheck &serviceDependency = {},
                             const WorkerReadinessCheck &prerequisite = {});
Json ApplyWorkerUpdate(const Transport &, std::string_view token,
                       const fs::path &state, std::string_view confirmation,
                       bool recover = false,
                       WorkerSourceReader = WorkerSourceIdentity,
                       const WorkerReadinessCheck &beforeAdmission = {},
                       const WorkerReadinessCheck &serviceDependency = {},
                       const WorkerReadinessCheck &prerequisite = {});
// Upload/reconcile a disabled candidate and stop before selected deployment
// mutation. Uses the same source, package, dependency and namespace fences.
Json StageWorkerUpdate(const Transport&,std::string_view,const fs::path&,std::string_view,bool recover=true,WorkerSourceReader=WorkerSourceIdentity,const WorkerReadinessCheck& serviceDependency={},const WorkerReadinessCheck& prerequisite={});
Json InspectWorkerUpdate(const Transport &, std::string_view token,
                         const fs::path &state);
// Enables only the already accepted package: one flag, unchanged modules,
// credentials and all other settings. Requires prior workflow and fresh
// readiness.
Json PlanWorkerAdmission(const Transport &, std::string_view token,
                         const fs::path &priorState, const fs::path &state,
                         const fs::path &source, const WorkerReadinessCheck &,
                         WorkerSourceReader = WorkerSourceIdentity,
                         const WorkerReadinessCheck &serviceDependency = {},
                         const WorkerReadinessCheck &prerequisite = {});
// Saved cross-family completion process. Resumes the exact update, checks its
// resource proof, calls the product's readiness contract, then seals evidence.
// Admission requires the explicit PlanWorkerAdmission profile. Never pays.
Json FinishWorkerUpdate(const Transport &, std::string_view token,
                        const fs::path &state, std::string_view confirmation,
                        const fs::path &resourceProof,
                        const WorkerReadinessCheck &,
                        WorkerSourceReader = WorkerSourceIdentity,
                        const WorkerReadinessCheck &beforeAdmission = {},
                        const WorkerReadinessCheck &serviceDependency = {},
                        const WorkerReadinessCheck &prerequisite = {});
Json WorkerWorkflowContract();
inline constexpr std::string_view workerRuntimeNotice =
    "capsule/arken_cloudflare_client_v1/notices/worker-runtime.txt";
} // namespace arken::cloudflare::v1
