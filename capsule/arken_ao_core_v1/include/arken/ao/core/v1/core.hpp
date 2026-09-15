#ifndef ARKEN_AO_CORE_V1_HPP
#define ARKEN_AO_CORE_V1_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace arken::ao::core::v1
{
constexpr uint16_t kContainerVersion = 1u;
constexpr uint16_t kHeaderSize = 64u;
constexpr uint16_t kChunkRecordSize = 64u;
constexpr uint32_t kEndianMarker = 0x01020304u;
constexpr uint32_t kCompressionNone = 0u;
constexpr uint32_t kEncodingBytes = 0u;
constexpr uint32_t kHashFnv1a64 = 1u;

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return (static_cast<uint32_t>(static_cast<unsigned char>(a)) << 0u)
        | (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8u)
        | (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16u)
        | (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

enum class Status
{
    ok,
    invalid_argument,
    truncated,
    unsupported_version,
    unsupported_endian,
    invalid_magic,
    invalid_chunk_directory,
    unsupported_compression,
    missing_required_chunk
};

enum class FileKind : uint32_t
{
    aodb = fourcc('A', 'O', 'D', 'B'),
    aopb = fourcc('A', 'O', 'P', 'B'),
    aoab = fourcc('A', 'O', 'A', 'B'),
    aogm = fourcc('A', 'O', 'G', 'M'),
    aotm = fourcc('A', 'O', 'T', 'M'),
    aoan = fourcc('A', 'O', 'A', 'N'),
    aopg = fourcc('A', 'O', 'P', 'G'),
    aoau = fourcc('A', 'O', 'A', 'U'),
    aoui = fourcc('A', 'O', 'U', 'I'),
    aosc = fourcc('A', 'O', 'S', 'C'),
    aoef = fourcc('A', 'O', 'E', 'F')
};

struct Chunk
{
    uint32_t type = 0u;
    uint16_t version = 1u;
    uint16_t flags = 0u;
    uint32_t compression = kCompressionNone;
    uint32_t encoding = kEncodingBytes;
    uint32_t element_count = 0u;
    uint32_t platform_mask = 0u;
    std::vector<uint8_t> payload;
};

struct ChunkView
{
    uint32_t type = 0u;
    uint16_t version = 0u;
    uint16_t flags = 0u;
    uint32_t compression = 0u;
    uint32_t encoding = 0u;
    uint32_t element_count = 0u;
    uint32_t platform_mask = 0u;
    uint64_t hash64 = 0u;
    std::vector<uint8_t> payload;
};

struct Container
{
    FileKind kind = FileKind::aodb;
    uint16_t version = kContainerVersion;
    uint32_t flags = 0u;
    uint64_t content_hash64 = 0u;
    std::vector<ChunkView> chunks;
};

struct ReadResult
{
    Status status = Status::ok;
    std::string message;
    Container container;
};

// A layout owns only header/directory bytes, never container-sized payloads.
struct ChunkDescriptor
{
    uint32_t type = 0u;
    uint16_t version = 1u;
    uint16_t flags = 0u;
    uint32_t compression = kCompressionNone;
    uint32_t encoding = kEncodingBytes;
    uint32_t element_count = 0u;
    uint32_t platform_mask = 0u;
    uint64_t byte_length = 0u;
    uint64_t hash64 = 0u;
};
struct ChunkPlacement { ChunkDescriptor chunk; uint64_t offset = 0u; };
struct ContainerLayout
{
    Status status = Status::ok;
    std::string message;
    std::vector<uint8_t> directory_bytes;
    std::vector<ChunkPlacement> chunks;
    uint64_t byte_length = 0u;
    explicit operator bool() const noexcept { return status == Status::ok; }
};
ContainerLayout plan_container(FileKind kind, std::span<ChunkDescriptor const> chunks,
    uint64_t maximum_bytes, uint32_t flags = 0u) noexcept;
// Kept header-owned so bounded browser kernels can reuse the same hash without
// compiling the container reader/writer into their source graph.
inline uint64_t fnv1a64_update(uint64_t hash, std::span<uint8_t const> bytes) noexcept
{
    for(auto byte:bytes) hash=(hash^byte)*1099511628211ull;
    return hash;
}
inline uint64_t fnv1a64(uint8_t const* data, size_t size)
{
    return fnv1a64_update(14695981039346656037ull,{data,size});
}
std::string fourcc_to_string(uint32_t value);
char const* status_name(Status status);

std::vector<uint8_t> bytes_from_text(std::string const& text);
std::vector<Chunk> default_system_chunks(std::string const& meta_text = {});
std::vector<uint8_t> write_container(FileKind kind, std::vector<Chunk> chunks, uint32_t flags = 0u);
ReadResult read_container(uint8_t const* bytes, size_t size);
ChunkView const* find_chunk(Container const& container, uint32_t type);
uint32_t count_chunks(Container const& container, uint32_t type);

} // namespace arken::ao::core::v1

#endif
