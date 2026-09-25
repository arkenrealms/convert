#pragma once
#include <span>
#include <string_view>

namespace arken::ao::schema::v1 {
// File roles are separate from envelope magic: AOED/AOSD use the existing
// canonical-value codec with different validated document schemas.
struct FileTypeDescriptor {
    std::string_view extension, name, purpose;
};
inline constexpr std::string_view editor_document_extension = ".aoed";
inline constexpr std::string_view scene_document_extension = ".aosd";
inline constexpr std::string_view default_editor_document_path = "world.aoed";
inline constexpr std::string_view default_scene_document_path = "document.aosd";
[[nodiscard]] std::span<FileTypeDescriptor const> file_types() noexcept;
[[nodiscard]] FileTypeDescriptor const* find_file_type(std::string_view extension) noexcept;
}
