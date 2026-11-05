#include "lossy.hpp"
#include <ogg/ogg.h>
#include <algorithm>
#include <new>

namespace arken::audio::v1::detail {
namespace {
constexpr std::size_t syncChunk = 64 * 1024;

struct Sync {
    ogg_sync_state state;
    Sync() noexcept { ogg_sync_init(&state); }
    ~Sync() { ogg_sync_clear(&state); }
    Sync(const Sync&) = delete;
    Sync& operator=(const Sync&) = delete;
};

struct Logical {
    std::uint32_t serial = 0;
    bool ended = false;
};

bool StartsWith(const ogg_page& page, const char* magic, std::size_t length) noexcept {
    return std::size_t(page.body_len) >= length && std::memcmp(page.body, magic, length) == 0;
}
} // namespace

OggScan ScanOgg(Bytes b, OggCodec codec, const Limits& limits) {
    OggScan scan;
    const auto fail = [&](Error e, std::uint64_t offset) { scan.issue = {e, offset}; return scan; };
    if (b.size() < 4) return fail(Error::truncated, 0);
    if (!Is(b, 0, "OggS")) return fail(Error::wrong_magic, 0);
    Sync sync;
    std::vector<Logical> streams;
    bool selected = false, dataStarted = false, otherKnownCodec = false;
    std::size_t fed = 0, consumed = 0;
    for (;;) {
        ogg_page page;
        const long r = ogg_sync_pageseek(&sync.state, &page);
        if (r < 0) return fail(Error::corrupt_stream, consumed); // lost sync or CRC mismatch: never resynchronised
        if (r == 0) {
            if (fed == b.size()) break;
            const auto chunk = std::min(syncChunk, b.size() - fed);
            char* buffer = ogg_sync_buffer(&sync.state, long(chunk));
            if (!buffer) return fail(Error::allocation_failed, fed);
            std::memcpy(buffer, b.data() + fed, chunk);
            if (ogg_sync_wrote(&sync.state, long(chunk)) != 0) return fail(Error::codec_failed, fed);
            fed += chunk;
            continue;
        }
        const std::size_t offset = consumed;
        consumed += std::size_t(r);
        if (ogg_page_version(&page) != 0) return fail(Error::corrupt_stream, offset);
        const auto serial = std::uint32_t(ogg_page_serialno(&page));
        auto index = std::size_t(std::find_if(streams.begin(), streams.end(),
            [&](const Logical& s) { return s.serial == serial; }) - streams.begin());
        const bool known = index < streams.size();
        if (ogg_page_bos(&page)) {
            if (dataStarted) {
                const bool allEnded = std::all_of(streams.begin(), streams.end(), [](const Logical& s) { return s.ended; });
                return fail(allEnded ? Error::chained_stream : Error::invalid_layout, offset);
            }
            if (known || ogg_page_continued(&page)) return fail(Error::invalid_layout, offset);
            if (streams.size() >= limits.maximumMetadataBlocks) return fail(Error::metadata_limit, offset);
            const bool vorbis = StartsWith(page, "\x01vorbis", 7);
            const bool opus = StartsWith(page, "OpusHead", 8);
            const bool wanted = codec == OggCodec::vorbis ? vorbis : opus;
            if (wanted && !selected) {
                selected = true;
                scan.serial = serial;
            } else {
                if (vorbis || opus) otherKnownCodec = true;
                ++scan.skippedStreams;
            }
            streams.push_back({serial, false});
            index = streams.size() - 1;
        } else {
            dataStarted = true;
            if (!known || streams[index].ended) return fail(Error::invalid_layout, offset);
        }
        if (ogg_page_eos(&page)) streams[index].ended = true;
        if (selected && serial == scan.serial) {
            const std::int64_t granule = ogg_page_granulepos(&page);
            if (granule < -1) return fail(Error::corrupt_stream, offset);
            if (granule != -1) {
                if (granule < scan.lastGranule) return fail(Error::corrupt_stream, offset); // granules never decrease
                scan.lastGranule = granule;
            }
            scan.pages.push_back({offset, std::size_t(page.header_len), std::size_t(page.body_len), granule});
        }
    }
    if (consumed != b.size()) return fail(Error::truncated, consumed); // a partial page remains
    if (!selected) return fail(otherKnownCodec ? Error::wrong_magic : Error::unsupported_encoding, 0);
    const auto chosen = std::find_if(streams.begin(), streams.end(), [&](const Logical& s) { return s.serial == scan.serial; });
    if (!chosen->ended) return fail(Error::truncated, b.size()); // no EOS page: the stream was cut
    return scan;
}

OggPackets::OggPackets(Bytes source, const OggScan& scan) : source_(source), scan_(scan) {
    auto* state = new (std::nothrow) ogg_stream_state;
    if (!state) return;
    if (ogg_stream_init(state, int(scan.serial)) != 0) {
        delete state;
        return;
    }
    state_ = state;
    valid_ = true;
}

OggPackets::~OggPackets() {
    if (auto* state = static_cast<ogg_stream_state*>(state_)) {
        ogg_stream_clear(state);
        delete state;
    }
}

bool OggPackets::NextPage(Error& error) noexcept {
    if (next_ >= scan_.pages.size()) return false;
    const auto& ref = scan_.pages[next_++];
    ogg_page page;
    // libogg only reads through these pointers.
    auto* base = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(source_.data()));
    page.header = base + ref.offset;
    page.header_len = long(ref.headerLength);
    page.body = base + ref.offset + ref.headerLength;
    page.body_len = long(ref.bodyLength);
    if (ogg_stream_pagein(static_cast<ogg_stream_state*>(state_), &page) != 0) {
        error = Error::corrupt_stream;
        return false;
    }
    return true;
}

int OggPackets::NextPacket(const unsigned char*& data, long& bytes) noexcept {
    ogg_packet packet;
    const int r = ogg_stream_packetout(static_cast<ogg_stream_state*>(state_), &packet);
    if (r == 1) {
        data = packet.packet;
        bytes = packet.bytes;
    }
    return r < 0 ? -1 : r;
}

std::int64_t OggPackets::PageGranule() const noexcept {
    return next_ ? scan_.pages[next_ - 1].granule : -1;
}

bool OggPackets::Incomplete() const noexcept {
    const auto* state = static_cast<const ogg_stream_state*>(state_);
    return state->lacing_fill > state->lacing_returned || state->body_fill > state->body_returned;
}

bool OggSink::Append(const unsigned char* data, long size) {
    if (error != Error::none) return false;
    const auto count = std::uint64_t(size);
    if (bytes.size() > limit || count > limit - bytes.size()) {
        error = Error::output_limit;
        return false;
    }
    const auto start = bytes.size();
    bytes.resize(start + std::size_t(count));
    if (count) std::memcpy(bytes.data() + start, data, std::size_t(count));
    return true;
}
} // namespace arken::audio::v1::detail
