#include "arken/ao/core/v1/core.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace arken::ao::core::v1
{
namespace
{
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint32_t read_u32(std::vector<uint8_t> const& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
}

uint16_t read_u16(std::vector<uint8_t> const& bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1u]) << 8u));
}

uint64_t read_u64(std::vector<uint8_t> const& bytes, size_t offset)
{
    uint64_t value = 0u;
    for(size_t index = 0u; index < 8u; index += 1u)
        value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8u);
    return value;
}

void write_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<uint8_t>((value >> 8u) & 0xffu);
}

void write_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for(size_t index = 0u; index < 4u; index += 1u)
        bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
}

void write_u64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
{
    for(size_t index = 0u; index < 8u; index += 1u)
        bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
}

size_t align16(size_t value)
{
    size_t const remainder = value & 15u;
    return remainder == 0u ? value : value + (16u - remainder);
}

ReadResult fail(Status status, std::string message)
{
    ReadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

uint32_t chunk_index(std::vector<Chunk> const& chunks, uint32_t type)
{
    for(size_t index = 0u; index < chunks.size(); index += 1u)
    {
        if(chunks[index].type == type)
            return static_cast<uint32_t>(index);
    }
    return UINT32_MAX;
}
}

uint64_t fnv1a64(uint8_t const* data, size_t size)
{
    uint64_t value = kFnvOffset;
    for(size_t index = 0u; index < size; index += 1u)
    {
        value ^= data[index];
        value *= kFnvPrime;
    }
    return value;
}

std::string fourcc_to_string(uint32_t value)
{
    std::string text(4u, '\0');
    for(size_t index = 0u; index < 4u; index += 1u)
        text[index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
    return text;
}

char const* status_name(Status status)
{
    switch(status)
    {
        case Status::ok: return "ok";
        case Status::invalid_argument: return "invalid_argument";
        case Status::truncated: return "truncated";
        case Status::unsupported_version: return "unsupported_version";
        case Status::unsupported_endian: return "unsupported_endian";
        case Status::invalid_magic: return "invalid_magic";
        case Status::invalid_chunk_directory: return "invalid_chunk_directory";
        case Status::unsupported_compression: return "unsupported_compression";
        case Status::missing_required_chunk: return "missing_required_chunk";
    }
    return "unknown";
}

std::vector<uint8_t> bytes_from_text(std::string const& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<Chunk> default_system_chunks(std::string const& meta_text)
{
    return {
        Chunk { fourcc('S', 'T', 'B', 'L'), 1u, 0u, kCompressionNone, kEncodingBytes, 0u, 0u, {} },
        Chunk { fourcc('D', 'E', 'P', 'S'), 1u, 0u, kCompressionNone, kEncodingBytes, 0u, 0u, {} },
        Chunk { fourcc('P', 'R', 'O', 'V'), 1u, 0u, kCompressionNone, kEncodingBytes, 0u, 0u, {} },
        Chunk { fourcc('E', 'X', 'T', 'S'), 1u, 0u, kCompressionNone, kEncodingBytes, 0u, 0u, {} },
        Chunk { fourcc('M', 'E', 'T', 'A'), 1u, 0u, kCompressionNone, kEncodingBytes, 1u, 0u, bytes_from_text(meta_text) }
    };
}

std::vector<uint8_t> write_container(FileKind kind, std::vector<Chunk> chunks, uint32_t flags)
{
    std::sort(chunks.begin(), chunks.end(), [](Chunk const& left, Chunk const& right) {
        return left.type < right.type;
    });

    size_t const directory_offset = kHeaderSize;
    size_t const directory_size = chunks.size() * kChunkRecordSize;
    size_t payload_offset = align16(directory_offset + directory_size);

    std::vector<size_t> offsets(chunks.size(), 0u);
    for(size_t index = 0u; index < chunks.size(); index += 1u)
    {
        offsets[index] = payload_offset;
        payload_offset = align16(payload_offset + chunks[index].payload.size());
    }

    std::vector<uint8_t> output(payload_offset, 0u);
    write_u32(output, 0u, static_cast<uint32_t>(kind));
    write_u16(output, 4u, kContainerVersion);
    write_u16(output, 6u, kHeaderSize);
    write_u32(output, 8u, kEndianMarker);
    write_u32(output, 12u, flags);
    write_u64(output, 16u, directory_offset);
    write_u32(output, 24u, static_cast<uint32_t>(chunks.size()));
    write_u32(output, 32u, chunk_index(chunks, fourcc('S', 'T', 'B', 'L')));
    write_u32(output, 36u, chunk_index(chunks, fourcc('D', 'E', 'P', 'S')));
    write_u32(output, 40u, chunk_index(chunks, fourcc('P', 'R', 'O', 'V')));
    write_u32(output, 44u, chunk_index(chunks, fourcc('E', 'X', 'T', 'S')));
    write_u32(output, 48u, chunk_index(chunks, fourcc('M', 'E', 'T', 'A')));
    write_u32(output, 52u, kHashFnv1a64);

    uint64_t combined_hash = kFnvOffset;
    for(size_t index = 0u; index < chunks.size(); index += 1u)
    {
        Chunk const& chunk = chunks[index];
        size_t const record = directory_offset + (index * kChunkRecordSize);
        uint64_t const chunk_hash = fnv1a64(chunk.payload.data(), chunk.payload.size());
        combined_hash ^= chunk_hash;
        combined_hash *= kFnvPrime;

        write_u32(output, record + 0u, chunk.type);
        write_u16(output, record + 4u, chunk.version);
        write_u16(output, record + 6u, chunk.flags);
        write_u32(output, record + 8u, chunk.compression);
        write_u32(output, record + 12u, chunk.encoding);
        write_u32(output, record + 16u, chunk.element_count);
        write_u64(output, record + 24u, static_cast<uint64_t>(offsets[index]));
        write_u64(output, record + 32u, static_cast<uint64_t>(chunk.payload.size()));
        write_u64(output, record + 40u, static_cast<uint64_t>(chunk.payload.size()));
        write_u64(output, record + 48u, chunk_hash);
        write_u32(output, record + 56u, chunk.platform_mask);

        if(!chunk.payload.empty())
            std::memcpy(output.data() + offsets[index], chunk.payload.data(), chunk.payload.size());
    }
    write_u64(output, 56u, combined_hash);
    return output;
}

ReadResult read_container(uint8_t const* raw_bytes, size_t size)
{
    if(raw_bytes == nullptr || size == 0u)
        return fail(Status::invalid_argument, "AO container reader requires bytes.");
    if(size < kHeaderSize)
        return fail(Status::truncated, "AO container header is truncated.");

    std::vector<uint8_t> bytes(raw_bytes, raw_bytes + size);
    uint32_t const magic = read_u32(bytes, 0u);
    if(magic != static_cast<uint32_t>(FileKind::aodb)
    && magic != static_cast<uint32_t>(FileKind::aopb)
    && magic != static_cast<uint32_t>(FileKind::aoab)
    && magic != static_cast<uint32_t>(FileKind::aogm)
    && magic != static_cast<uint32_t>(FileKind::aotm)
    && magic != static_cast<uint32_t>(FileKind::aoan)
    && magic != static_cast<uint32_t>(FileKind::aopg)
    && magic != static_cast<uint32_t>(FileKind::aoau)
    && magic != static_cast<uint32_t>(FileKind::aoui)
    && magic != static_cast<uint32_t>(FileKind::aosc)
    && magic != static_cast<uint32_t>(FileKind::aoef))
    {
        return fail(Status::invalid_magic, "AO container magic is not recognized.");
    }
    if(read_u16(bytes, 4u) != kContainerVersion || read_u16(bytes, 6u) < kHeaderSize)
        return fail(Status::unsupported_version, "AO container version/header size is unsupported.");
    if(read_u32(bytes, 8u) != kEndianMarker)
        return fail(Status::unsupported_endian, "AO container endian marker is unsupported.");

    uint64_t const directory_offset = read_u64(bytes, 16u);
    uint32_t const chunk_count = read_u32(bytes, 24u);
    uint64_t const directory_size = static_cast<uint64_t>(chunk_count) * kChunkRecordSize;
    if(directory_offset < kHeaderSize || directory_offset + directory_size > size)
        return fail(Status::invalid_chunk_directory, "AO chunk directory is out of bounds.");

    ReadResult result;
    result.status = Status::ok;
    result.container.kind = static_cast<FileKind>(magic);
    result.container.version = read_u16(bytes, 4u);
    result.container.flags = read_u32(bytes, 12u);
    result.container.content_hash64 = read_u64(bytes, 56u);
    result.container.chunks.reserve(chunk_count);

    for(uint32_t index = 0u; index < chunk_count; index += 1u)
    {
        size_t const record = static_cast<size_t>(directory_offset) + (index * kChunkRecordSize);
        ChunkView chunk;
        chunk.type = read_u32(bytes, record + 0u);
        chunk.version = read_u16(bytes, record + 4u);
        chunk.flags = read_u16(bytes, record + 6u);
        chunk.compression = read_u32(bytes, record + 8u);
        chunk.encoding = read_u32(bytes, record + 12u);
        chunk.element_count = read_u32(bytes, record + 16u);
        uint64_t const offset = read_u64(bytes, record + 24u);
        uint64_t const uncompressed_size = read_u64(bytes, record + 32u);
        uint64_t const compressed_size = read_u64(bytes, record + 40u);
        chunk.hash64 = read_u64(bytes, record + 48u);
        chunk.platform_mask = read_u32(bytes, record + 56u);
        if(chunk.compression != kCompressionNone || compressed_size != uncompressed_size)
            return fail(Status::unsupported_compression, "AO container v1 supports uncompressed chunks only.");
        if(offset + compressed_size > size)
            return fail(Status::invalid_chunk_directory, "AO chunk payload is out of bounds.");
        chunk.payload.assign(bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.begin() + static_cast<ptrdiff_t>(offset + compressed_size));
        if(fnv1a64(chunk.payload.data(), chunk.payload.size()) != chunk.hash64)
            return fail(Status::invalid_chunk_directory, "AO chunk hash mismatch.");
        result.container.chunks.push_back(std::move(chunk));
    }

    if(find_chunk(result.container, fourcc('M', 'E', 'T', 'A')) == nullptr)
        return fail(Status::missing_required_chunk, "AO container is missing META chunk.");
    return result;
}

ChunkView const* find_chunk(Container const& container, uint32_t type)
{
    for(ChunkView const& chunk : container.chunks)
    {
        if(chunk.type == type)
            return &chunk;
    }
    return nullptr;
}

uint32_t count_chunks(Container const& container, uint32_t type)
{
    uint32_t count = 0u;
    for(ChunkView const& chunk : container.chunks)
    {
        if(chunk.type == type)
            count += 1u;
    }
    return count;
}

} // namespace arken::ao::core::v1
