#include "arken/ao/schema/v1/file_types.hpp"
#include <array>

namespace arken::ao::schema::v1 {
namespace {
constexpr std::array types{
    FileTypeDescriptor{".aoschema", "Arken Schema Source", "Native schema IDL source; decoded by the schema compiler, not an AO binary container"},
    FileTypeDescriptor{".aocb", "Arken Omniverse Creation Bundle", "Complete creation and declared client/server content"},
    FileTypeDescriptor{".aodb", "Arken Omniverse Data Bundle", "Canonical world and simulation data"},
    FileTypeDescriptor{".aopb", "Arken Omniverse Plugin Bundle", "Installable gameplay, systems, or content delta"},
    FileTypeDescriptor{".aoab", "Arken Omniverse Asset Bundle", "Packed art, audio, script, and resource assets"},
    FileTypeDescriptor{".aogm", "Arken Omniverse Geometry Model", "Geometry, mesh, skeleton, collision, scene-model, and model metadata"},
    FileTypeDescriptor{".aotm", "Arken Omniverse Texture Map", "GPU-ready texture, material, and surface map"},
    FileTypeDescriptor{editor_document_extension, "Arken Omniverse Editor Document", "Canonical authored Editor document data"},
    FileTypeDescriptor{scene_document_extension, "Arken Omniverse Scene Document", "Scene declaration linking document, resources, and launch metadata"},
    FileTypeDescriptor{".aopg", "Arken Omniverse Parametric Graph", "Typed parametric recipe and output records in their AO container"},
    FileTypeDescriptor{".aoan", "Arken Omniverse Animation", "Animation clips, channels, events, and clocks"},
    FileTypeDescriptor{".aoav", "Arken Omniverse Actor Visual", "Actor visual and skeletal resource bindings"},
    FileTypeDescriptor{".aoau", "Arken Omniverse Audio", "Audio manifests and payloads"},
    FileTypeDescriptor{".aoef", "Arken Omniverse Effects", "Effects, particles, ribbons, and splines"},
    FileTypeDescriptor{".aosc", "Arken Omniverse Scene", "Scene, light, and fog records"},
    FileTypeDescriptor{".aodg", "Arken Omniverse Dependency Graph", "Dependency graph records"},
    FileTypeDescriptor{".aois", "Arken Omniverse Interactive Semantics", "Source-neutral interactive semantic records"},
    FileTypeDescriptor{".aoui", "Arken Omniverse User Interface", "Authored interface data"},
    FileTypeDescriptor{".aovf", "Arken Omniverse Voxel Field", "Explicit voxel basis, palette, and cell identities"},
    FileTypeDescriptor{".aorp", "Arken Omniverse Rigid Pose", "Rigid clip joints, sparse tracks, keys, placement and interpolation; not render-frame transport"},
    FileTypeDescriptor{".aorb", "Arken Omniverse Rigid Binding", "Ordered rigid vertex or point bindings to animation joints"},
};
static_assert([]{for(std::size_t i=0;i<types.size();++i){
    if(!types[i].extension.starts_with(".ao")||types[i].name.empty()||types[i].purpose.empty())return false;
    for(std::size_t j=0;j<i;++j)if(types[i].extension==types[j].extension)return false;
}return true;}());
}
std::span<FileTypeDescriptor const> file_types() noexcept {return types;}
FileTypeDescriptor const* find_file_type(std::string_view extension) noexcept {
    for(auto const& type:types)if(type.extension==extension)return &type;
    return nullptr;
}
}
