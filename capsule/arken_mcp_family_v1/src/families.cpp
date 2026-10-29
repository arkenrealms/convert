#include "arken_mcp_family_v1/api.hpp"
namespace arken::mcp::family::v1 {
namespace {
constexpr Family inventory[]={
 {"antivirus","Antivirus","Inspect security evidence before you trust it.","Thirty published tools inspect package and installation risks, provenance and signing evidence, dependency advisories, content signals, archive structure, network destinations, executables and model-file headers. Bounded structural analysis is not a guarantee that a file is safe and does not replace endpoint protection."},
 {"convert","Convert","Files in. Useful formats out.","Convert PNG to JPEG (JPG), JPEG to PNG, BMP to PNG or JPEG, and TGA or supported DDS textures to PNG. Eight published MCP tools share one bounded C++ converter, explicit alpha handling and private downloads. Broader Canvas file conversions remain separate."},
 {"image","Image","Precise operations on pixels.","Crop, resize, rotate, flip, invert and stitch supported raster images through bounded C++ kernels. Inputs and outputs use scoped file capabilities. Grayscale, brightness, contrast and further image operations remain planned."},
 {"math","Math","Compute, explore, understand.","Thirty-eight commands cover numeric expressions, Taylor–Green models, graph coloring, exact cyclotomic arithmetic, exact rational linear solving and independent certificate verification, symmetric-matrix spectra and rank–trace diagnostics, CNF and one-hot encoding, assignment verification, bounded RUP checking, exact unit-distance geometry/counting, Kempe recoloring exploration/checking, bounded two-phase endpoint certificates, polynomial root counting/isolation, sign/range analysis, and public research retrieval. Retrieve all eight shipped research graphs, with 246 nodes and 449 relationships. Certificate checks distinguish invalid, valid prefix, contradiction and resource limit; recorded research is not a fresh theorem proof."},
 {"geometry","Geometry","Algorithms for shapes and space.","Planned geometric queries, intersections, hulls and mesh-to-voxel operations, with explicit accuracy, resolution and representation constraints."},
 {"physics","Physics","Physical calculations and models.","Sixty-three published tools cover bounded collision queries, orbital mechanics, oscillators, optics, elastic waves, quantum systems and Gaussian-state analysis. Typed inputs declare physical units, assumptions, tolerances and resource limits. Numerical results and local estimates are not exact analytical proofs."},
 {"audio","Audio","Measure sound samples precisely.","Two published C++ tools analyze interleaved audio: waveform summaries and aggregate metrics, or independent amplitude, RMS, mean, clipping and crossing counts for each channel. Inputs support up to 262,144 finite samples across 1–8 channels. This is signal analysis, not song recognition, transcription, resampling or loudness certification."},
 {"music","Music","Notes, rhythm and musical structure.","Planned music metadata extraction, MIDI, harmony, rhythm and arrangement capabilities using the owning music capsules."},
 {"video","Video","Understand and transform moving images.","Planned transcript extraction, frame operations and bounded video workflows. Codec and resource qualification precede public deployment."},
 {"games","Games","Game systems as reusable commands.","Planned game-content and gameplay capabilities composed from Arken game capsules, with explicit engine and asset constraints."},
 {"ui","UI","From a concept to an interface.","Planned concept-driven landing-page and interface design using the authored prompt collection and shared Arken components."},
 {"vector","Vector","Resolution-independent graphics.","Planned vector paths, graphic transformations and representation-aware export. Raster tracing must be identified as approximation."},
 {"two-d","2D","Sprites, tilemaps and scenes.","Planned two-dimensional asset and scene operations that compose pixel, vector and animation capabilities."},
 {"three-d","3D","Models and complete asset workflows.","Planned mesh and scene workflows. Image-to-mesh reconstruction and application adapters require their own qualified providers."},
 {"cad","CAD","Solids and parametric design.","Planned CAD operations with explicit solid, B-rep and NURBS semantics. A tessellated mesh is not an equivalent editable solid."},
 {"material","Material","Define how surfaces behave.","Planned material analysis, construction and conversion with explicit channel and rendering-model semantics."},
 {"texture","Texture","Texture assets for every surface.","Planned texture processing and AO texture workflows using existing Arken conversion owners, with color-space and loss reporting."},
 {"animation","Animation","Movement across time.","Planned frame, skeletal and clip operations with explicit timing, rig and format limits."},
 {"data","Data","Parse CSV. Export structured tables.","Two bounded C++ tools parse UTF-8 CSV and export string tables as CSV or JSON. Preserve row order, duplicate headers, Unicode and leading zeroes without type inference or formula execution. Database queries, file access and network retrieval are not supported."},
 {"document","Document","Readable documents, useful structure.","Planned document extraction, transformation and export. Layout preservation, text extraction and format conversion are distinct operations."},
 {"code","Code","Tools for source and programs.","Planned source inspection and transformation through scoped language owners, not an unrestricted shell or compiler endpoint."},
 {"web","Web","Work with pages and web content.","Planned web-document and page capabilities with explicit retrieval, privacy and network boundaries."},
 {"simulation","Simulation","Run controlled computational worlds.","Planned deterministic simulations with bounded time, state, seeds and observable outcomes."},
 {"spatial","Spatial","Location, coordinates and relationships.","Double-precision affine transforms for bounded point batches and axis-aligned boxes, with an explicit row-major convention. No implicit unit or coordinate-system conversion."},
 {"knowledge","Knowledge","Retrieve and explain grounded information.","Planned knowledge retrieval and analysis with citations and evidence provenance, distinct from mathematical or factual certification."},
 {"workflow","Workflow","Compose useful capabilities.","Planned bounded multi-stage operation graphs with typed dependencies, explicit cost, loss reporting and partial-result recovery."},
 {"canvas","Canvas","Make an execution understandable.","Private result links restore supported Math, Audio and Physics request/result graphs on canvas.arken.gg without executing another command or payment. The Canvas MCP endpoint is online for discovery; graph inspection methods remain planned."}
};
}
std::span<const Family> families() noexcept {return inventory;}
const Family* find(std::string_view id) noexcept {
 for(const auto& family:inventory)if(id==family.id)return &family;
 return nullptr;
}
bool scaffold(std::string_view id) noexcept {
 return find(id)&&id!="math"&&id!="physics"&&id!="antivirus"&&id!="audio"&&id!="image"&&id!="convert";
}
}
