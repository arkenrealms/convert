// Native qualification of arken_audio_codecs_v1: exact PCM preservation across
// WAV and FLAC, channel-mask handling, malformed input, limits and determinism.
#include "arken_audio_codecs_v1/api.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace audio = arken::audio::v1;
using audio::Error;
using Bytes = std::vector<std::byte>;

namespace {
int failures = 0;
void Check(bool ok, std::string_view what) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
std::string Name(Error e) { return std::string(audio::ErrorMessage(e)); }
void ExpectError(Error observed, Error expected, std::string_view what) {
    if (observed != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": expected '" << Name(expected) << "', observed '" << Name(observed) << "'\n";
    }
}

struct Lcg {
    std::uint64_t state;
    std::uint32_t Next() { state = state * 6364136223846793005ull + 1442695040888963407ull; return std::uint32_t(state >> 32); }
};

enum class Signal { silence, impulse, extrema, clipping, sine, noise, mixed };
const Signal allSignals[] = {Signal::silence, Signal::impulse, Signal::extrema, Signal::clipping, Signal::sine, Signal::noise, Signal::mixed};

audio::Audio Make(std::uint32_t rate, std::uint32_t channels, std::uint32_t bits, std::uint64_t frames, Signal signal,
    std::optional<std::uint32_t> mask = std::nullopt, std::uint64_t seed = 1) {
    audio::Audio a;
    a.sampleRate = rate; a.channels = channels; a.bitsPerSample = bits; a.channelMask = mask; a.frames = frames;
    a.samples.resize(frames * channels);
    const std::int64_t high = (std::int64_t(1) << (bits - 1)) - 1, low = -high - 1;
    Lcg lcg{seed * 0x9E3779B97F4A7C15ull + bits * 131 + channels};
    for (std::uint64_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c) {
            std::int64_t v = 0;
            switch (signal) {
            case Signal::silence: v = 0; break;
            case Signal::impulse: v = (f == frames / 3) ? (c % 2 ? low : high) : 0; break;
            case Signal::extrema: v = ((f + c) % 2) ? low : high; break;
            case Signal::clipping: {
                const std::int64_t near[] = {high, high - 1, low, low + 1, 0, -1, 1};
                v = near[(f * 3 + c) % 7];
                break;
            }
            case Signal::sine:
                v = std::int64_t(std::llround(double(high) * 0.9 * std::sin(6.283185307179586 * 441.0 * double(c + 1) * double(f) / double(rate))));
                break;
            case Signal::noise: v = low + std::int64_t(lcg.Next() % std::uint64_t(high - low + 1)); break;
            case Signal::mixed: {
                const double t = double(f) / double(rate);
                double s = 0.5 * std::sin(6.283185307179586 * 220.0 * t) + 0.25 * std::sin(6.283185307179586 * 3520.0 * t * (1 + c));
                if ((f / 4096) % 5 == 3) s = (f / 64) % 2 ? 0.98 : -0.98; // square bursts
                v = std::int64_t(std::llround(s * double(high))) + std::int64_t(lcg.Next() % 7) - 3;
                v = std::clamp(v, low, high);
                if (f % 50000 == 1234) v = (c % 2) ? low : high; // isolated extrema
                break;
            }
            }
            a.samples[f * channels + c] = std::int32_t(v);
        }
    return a;
}

bool Same(const audio::Audio& a, const audio::Audio& b) {
    return a.sampleRate == b.sampleRate && a.channels == b.channels && a.bitsPerSample == b.bitsPerSample
        && a.channelMask == b.channelMask && a.frames == b.frames && a.samples == b.samples;
}
bool SameSamples(const audio::Audio& a, const audio::Audio& b) {
    return a.sampleRate == b.sampleRate && a.channels == b.channels && a.bitsPerSample == b.bitsPerSample
        && a.frames == b.frames && a.samples == b.samples;
}
std::string Describe(const audio::Audio& a, std::string_view signal) {
    return std::to_string(a.sampleRate) + "Hz/" + std::to_string(a.channels) + "ch/" + std::to_string(a.bitsPerSample) + "bit/" + std::string(signal);
}

// Independent little-endian RIFF builder for hand-authored inputs.
struct Riff {
    Bytes b;
    void Tag(const char* id) { for (int i = 0; i < 4; ++i) b.push_back(std::byte(std::uint8_t(id[i]))); }
    void Le(std::uint64_t v, int n) { for (int i = 0; i < n; ++i) b.push_back(std::byte((v >> (8 * i)) & 0xFF)); }
    void Chunk(const char* id, const Bytes& body, bool pad = true) {
        Tag(id); Le(body.size(), 4); b.insert(b.end(), body.begin(), body.end());
        if (pad && (body.size() & 1)) b.push_back(std::byte{0});
    }
    Bytes Finish(std::int64_t sizeAdjust = 0, const char* magic = "RIFF") const {
        Riff out; out.Tag(magic); out.Le(std::uint64_t(std::int64_t(b.size()) + 4 + sizeAdjust), 4); out.Tag("WAVE");
        out.b.insert(out.b.end(), b.begin(), b.end());
        return out.b;
    }
};
Bytes Fmt(std::uint16_t tag, std::uint16_t ch, std::uint32_t rate, std::uint16_t bits, std::int32_t alignAdjust = 0, std::int32_t avgAdjust = 0) {
    Riff r; const std::uint32_t align = ch * (bits / 8) + std::uint32_t(alignAdjust);
    r.Le(tag, 2); r.Le(ch, 2); r.Le(rate, 4); r.Le(std::uint64_t(rate) * align + std::uint64_t(avgAdjust), 4); r.Le(align, 2); r.Le(bits, 2);
    return r.b;
}
Bytes FmtExtensible(std::uint16_t ch, std::uint32_t rate, std::uint16_t bits, std::uint16_t valid, std::uint32_t mask, std::uint16_t subtype = 1, bool goodGuid = true) {
    auto r = Riff{Fmt(0xFFFE, ch, rate, bits)};
    r.Le(22, 2); r.Le(valid, 2); r.Le(mask, 4); r.Le(subtype, 2);
    const std::uint8_t tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, goodGuid ? std::uint8_t(0x71) : std::uint8_t(0x72)};
    for (auto t : tail) r.b.push_back(std::byte(t));
    return r.b;
}
Bytes Data(std::size_t n, std::uint8_t fill = 0) { return Bytes(n, std::byte(fill)); }
Bytes Wav16(std::uint16_t ch, std::uint32_t rate, const std::vector<std::int16_t>& s) {
    Riff r; r.Chunk("fmt ", Fmt(1, ch, rate, 16));
    Riff d; for (auto v : s) d.Le(std::uint16_t(v), 2);
    r.Chunk("data", d.b);
    return r.Finish();
}

std::size_t Find(const Bytes& hay, std::string_view needle) {
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (std::memcmp(hay.data() + i, needle.data(), needle.size()) == 0) return i;
    return std::string_view::npos;
}

struct FlacBlock { std::uint32_t type, length; bool last; std::size_t body; };
std::vector<FlacBlock> FlacBlocks(const Bytes& f) {
    std::vector<FlacBlock> blocks;
    if (f.size() < 8 || std::memcmp(f.data(), "fLaC", 4)) return blocks;
    std::size_t p = 4;
    for (bool last = false; !last && p + 4 <= f.size();) {
        const auto h = std::to_integer<std::uint32_t>(f[p]);
        const auto len = (std::to_integer<std::uint32_t>(f[p + 1]) << 16) | (std::to_integer<std::uint32_t>(f[p + 2]) << 8) | std::to_integer<std::uint32_t>(f[p + 3]);
        last = h & 0x80;
        blocks.push_back({h & 0x7F, len, last, p + 4});
        p += 4 + len;
    }
    return blocks;
}

// --- checks ---------------------------------------------------------------

void WavHeaderReference() {
    // A hand-authored 16-bit stereo WAVE_FORMAT_PCM file must be reproduced byte-for-byte.
    const std::vector<std::int16_t> s = {0, -1, 32767, -32768, 1234, -4321};
    const auto reference = Wav16(2, 44100, s);
    auto decoded = audio::DecodeWav(reference);
    Check(bool(decoded), "decode hand-authored 16-bit WAV");
    Check(decoded.audio.frames == 3 && decoded.audio.channels == 2 && !decoded.audio.channelMask, "hand-authored WAV fields");
    Check(std::equal(s.begin(), s.end(), decoded.audio.samples.begin(), decoded.audio.samples.end()), "hand-authored WAV samples");
    auto encoded = audio::EncodeWav(decoded.audio);
    Check(encoded && encoded.bytes == reference, "EncodeWav reproduces the hand-authored PCM header and data");
    // 8-bit is unsigned on disk: 0x80 is silence, 0x00 the minimum.
    Riff r; r.Chunk("fmt ", Fmt(1, 1, 8000, 8)); r.Chunk("data", Bytes{std::byte{0x80}, std::byte{0x00}, std::byte{0xFF}});
    auto eight = audio::DecodeWav(r.Finish());
    Check(eight && eight.audio.samples == std::vector<std::int32_t>{0, -128, 127}, "8-bit unsigned mapping");
    auto eightOut = audio::EncodeWav(eight.audio);
    Check(eightOut && eightOut.bytes == r.Finish(), "8-bit odd-length data chunk reproduced with pad byte");
}

void WavRoundTrip() {
    WavHeaderReference();
    const std::uint32_t rates[] = {8000, 22050, 44100, 48000, 96000, 192000};
    int cases = 0;
    for (const auto channels : {1u, 2u})
        for (const auto bits : {8u, 16u, 24u, 32u})
            for (const auto rate : rates)
                for (const auto signal : allSignals) {
                    const auto frames = 1000 + rate / 50;
                    const auto a = Make(rate, channels, bits, frames, signal);
                    const auto label = Describe(a, std::to_string(int(signal)));
                    Check(audio::ValidateAudio(a) == Error::none, "generated audio valid " + label);
                    const auto wav = audio::EncodeWav(a);
                    Check(bool(wav), "EncodeWav " + label);
                    const auto fromWav = audio::DecodeWav(wav.bytes);
                    Check(fromWav && Same(fromWav.audio, a), "WAV decode exact " + label);
                    const auto flac = audio::EncodeFlac(fromWav.audio);
                    Check(bool(flac), "EncodeFlac " + label + " " + Name(flac.error));
                    const auto fromFlac = audio::DecodeFlac(flac.bytes);
                    Check(fromFlac && Same(fromFlac.audio, a), "WAV->FLAC->PCM exact " + label + " " + Name(fromFlac.issue.code));
                    const auto wavAgain = audio::EncodeWav(fromFlac.audio);
                    Check(wavAgain && wavAgain.bytes == wav.bytes, "WAV->FLAC->WAV byte-identical " + label);
                    ++cases;
                }
    // 20 significant bits in a 24-bit container, and 12 in 16: WAVE_FORMAT_EXTENSIBLE.
    for (const auto& [bits, mask] : {std::pair{20u, 3u}, std::pair{12u, 4u}, std::pair{4u, 4u}, std::pair{28u, 3u}}) {
        const auto channels = mask == 3u ? 2u : 1u;
        const auto a = Make(48000, channels, bits, 5000, Signal::mixed, mask);
        const auto wav = audio::EncodeWav(a);
        Check(wav && audio::DecodeWav(wav.bytes) && Same(audio::DecodeWav(wav.bytes).audio, a), "extensible valid-bits WAV exact " + std::to_string(bits));
        const auto flac = audio::EncodeFlac(a);
        const auto back = audio::DecodeFlac(flac.bytes);
        Check(back && Same(back.audio, a), "extensible valid-bits FLAC exact " + std::to_string(bits));
        const auto wav2 = audio::EncodeWav(back.audio);
        Check(wav2 && wav2.bytes == wav.bytes, "extensible valid-bits WAV->FLAC->WAV byte-identical " + std::to_string(bits));
        ++cases;
    }
    std::cout << "WAV round trips: " << cases << " cases\n";
}

void FlacRoundTrip() {
    const std::uint32_t rates[] = {8000, 11025, 44100, 48000, 88200, 192000};
    int cases = 0;
    for (const auto channels : {1u, 2u})
        for (const auto bits : {8u, 16u, 24u})
            for (const auto rate : rates)
                for (const auto signal : allSignals) {
                    const auto a = Make(rate, channels, bits, 4096 * 2 + 777, signal, std::nullopt, rate);
                    const auto label = Describe(a, std::to_string(int(signal)));
                    const auto flac1 = audio::EncodeFlac(a);
                    Check(bool(flac1), "EncodeFlac " + label);
                    const auto pcm1 = audio::DecodeFlac(flac1.bytes);
                    Check(pcm1 && Same(pcm1.audio, a), "FLAC decode exact " + label);
                    const auto wav = audio::EncodeWav(pcm1.audio);
                    const auto pcm2 = audio::DecodeWav(wav.bytes);
                    Check(pcm2 && Same(pcm2.audio, a), "FLAC->WAV->PCM exact " + label);
                    const auto flac2 = audio::EncodeFlac(pcm2.audio);
                    Check(flac2 && flac2.bytes == flac1.bytes, "FLAC->WAV->FLAC byte-identical " + label);
                    ++cases;
                }
    // Bit depths that need WAVE_FORMAT_EXTENSIBLE: samples are exact; the WAV
    // carries an explicit zero mask, so the second FLAC adds the mask comment.
    for (const auto bits : {12u, 20u, 32u}) {
        const auto a = Make(44100, 2, bits, 10000, Signal::mixed);
        const auto flac1 = audio::EncodeFlac(a);
        const auto pcm1 = audio::DecodeFlac(flac1.bytes);
        const auto wav = audio::EncodeWav(pcm1.audio);
        const auto pcm2 = audio::DecodeWav(wav.bytes);
        Check(flac1 && pcm1 && pcm2 && SameSamples(pcm2.audio, a), "FLAC->WAV->PCM exact " + std::to_string(bits));
        if (bits == 32) Check(pcm2.audio.channelMask == std::nullopt, "32-bit stays WAVE_FORMAT_PCM without mask");
        else Check(pcm2.audio.channelMask == 0u, "non-aligned bits materialize an explicit zero mask");
        const auto flac2 = audio::EncodeFlac(pcm2.audio);
        const auto pcm3 = audio::DecodeFlac(flac2.bytes);
        Check(pcm3 && SameSamples(pcm3.audio, a), "FLAC->WAV->FLAC samples exact " + std::to_string(bits));
        ++cases;
    }
    // Stream layout written by the encoder.
    const auto a = Make(44100, 2, 16, 44100, Signal::sine);
    const auto f = audio::EncodeFlac(a);
    const auto blocks = FlacBlocks(f.bytes);
    Check(blocks.size() == 2 && blocks[0].type == 0 && blocks[0].length == 34 && !blocks[0].last
        && blocks[1].type == 4 && blocks[1].last, "exactly STREAMINFO + VORBIS_COMMENT, no SEEKTABLE or PADDING");
    if (blocks.size() == 2) {
        const auto* si = f.bytes.data() + blocks[0].body;
        std::uint64_t packed = 0;
        for (int i = 10; i < 18; ++i) packed = (packed << 8) | std::to_integer<std::uint64_t>(si[i]);
        Check((packed >> 44) == 44100 && ((packed >> 41) & 7) + 1 == 2 && ((packed >> 36) & 31) + 1 == 16
            && (packed & ((1ull << 36) - 1)) == 44100, "STREAMINFO rate/channels/bits/total samples");
        Check(std::any_of(si + 18, si + 34, [](std::byte x) { return x != std::byte{0}; }), "STREAMINFO MD5 written");
        Check(Find(f.bytes, audio::FlacVendorString()) == blocks[1].body + 4, "vendor string is libFLAC's fixed identity");
    }
    // Zero-length audio is a valid, empty stream.
    const auto empty = Make(44100, 2, 16, 0, Signal::silence);
    const auto ef = audio::EncodeFlac(empty);
    const auto ed = audio::DecodeFlac(ef.bytes);
    Check(ef && ed && Same(ed.audio, empty), "empty FLAC round trip");
    const auto ew = audio::EncodeWav(empty);
    Check(ew && ew.bytes.size() == 44 && audio::DecodeWav(ew.bytes), "empty WAV round trip");
    std::cout << "FLAC round trips: " << cases << " cases\n";
}

void ChannelMask() {
    struct Case { std::uint32_t channels, bits; std::optional<std::uint32_t> mask; const char* name; };
    const Case cases[] = {
        {6, 24, 0x3F, "5.1 0x3F"}, {6, 16, 0x60F, "5.1(side) 0x60F"}, {4, 16, 0x33, "quad 0x33"},
        {1, 16, 0x4, "mono front-centre"}, {2, 16, 0x0, "explicit zero mask"}, {2, 16, 0x3, "explicit stereo mask"},
        {8, 24, 0x63F, "7.1 0x63F"}, {3, 16, std::nullopt, "3ch no mask"}, {8, 16, std::nullopt, "8ch no mask"},
        {2, 24, 0x80000003u, "high bit preserved"}, {2, 16, 0x7FFFF, "more bits than channels"},
    };
    for (const auto& c : cases) {
        const auto a = Make(48000, c.channels, c.bits, 6000, Signal::mixed, c.mask, c.channels);
        const auto wav = audio::EncodeWav(a);
        const auto w = audio::DecodeWav(wav.bytes);
        Check(w && Same(w.audio, a), std::string("WAV mask/order exact: ") + c.name);
        // Header form: an absent mask with aligned bits stays WAVE_FORMAT_PCM.
        const auto tag = std::to_integer<unsigned>(wav.bytes[20]) | (std::to_integer<unsigned>(wav.bytes[21]) << 8);
        Check(tag == (c.mask ? 0xFFFEu : 1u), std::string("WAV format tag: ") + c.name);
        const auto flac = audio::EncodeFlac(w.audio);
        const auto f = audio::DecodeFlac(flac.bytes);
        Check(f && Same(f.audio, a), std::string("FLAC mask/order exact: ") + c.name);
        Check((Find(flac.bytes, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x") != std::string_view::npos) == c.mask.has_value(),
            std::string("mask comment present iff declared: ") + c.name);
        const auto wav2 = audio::EncodeWav(f.audio);
        Check(wav2 && wav2.bytes == wav.bytes, std::string("WAV->FLAC->WAV byte-identical: ") + c.name);
    }
    const auto quad = audio::EncodeFlac(Make(48000, 4, 16, 100, Signal::sine, 0x33));
    Check(Find(quad.bytes, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x0033") != std::string_view::npos, "mask spelled 0x%04X");
    const auto wide = audio::EncodeFlac(Make(48000, 2, 16, 100, Signal::sine, 0x80000003u));
    Check(Find(wide.bytes, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x80000003") != std::string_view::npos, "wide mask spelled in full");
    // Channel order is never remapped: channel c of every frame carries value c.
    audio::Audio order = Make(48000, 6, 16, 10, Signal::silence, 0x3F);
    for (std::size_t i = 0; i < order.samples.size(); ++i) order.samples[i] = std::int32_t(i % 6) * 1000 + 1;
    const auto of = audio::DecodeFlac(audio::EncodeFlac(order).bytes);
    const auto ow = audio::DecodeWav(audio::EncodeWav(of.audio).bytes);
    Check(of && ow && ow.audio.samples == order.samples, "channel order preserved through FLAC and WAV");
}

void Malformed() {
    const auto good = audio::EncodeWav(Make(44100, 2, 16, 100, Signal::sine)).bytes;
    ExpectError(audio::DecodeWav({}).issue.code, Error::truncated, "empty WAV");
    ExpectError(audio::DecodeWav(Bytes(good.begin(), good.begin() + 10)).issue.code, Error::truncated, "10-byte WAV");
    auto magic = good; magic[0] = std::byte{'X'};
    ExpectError(audio::DecodeWav(magic).issue.code, Error::wrong_magic, "wrong RIFF magic");
    auto wave = good; wave[8] = std::byte{'A'};
    ExpectError(audio::DecodeWav(wave).issue.code, Error::wrong_magic, "wrong WAVE form");
    for (const char* id : {"RF64", "BW64", "RIFX"}) {
        auto other = good; std::memcpy(other.data(), id, 4);
        ExpectError(audio::DecodeWav(other).issue.code, Error::unsupported_container, std::string("container ") + id);
    }
    const std::uint8_t w64[16] = {'r', 'i', 'f', 'f', 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00};
    Bytes wave64(64); std::memcpy(wave64.data(), w64, 16);
    ExpectError(audio::DecodeWav(wave64).issue.code, Error::unsupported_container, "Sony Wave64");
    // Every strict prefix of a valid file is rejected; none decodes as shorter audio.
    for (std::size_t cut = 0; cut < good.size(); ++cut) {
        const auto r = audio::DecodeWav(Bytes(good.begin(), good.begin() + std::ptrdiff_t(cut)));
        if (r) { Check(false, "truncated WAV accepted at " + std::to_string(cut)); break; }
    }
    auto trailing = good; trailing.push_back(std::byte{0});
    ExpectError(audio::DecodeWav(trailing).issue.code, Error::invalid_layout, "bytes after RIFF extent");
    const auto fmt16 = Fmt(1, 2, 44100, 16);
    const auto build = [](std::initializer_list<std::pair<const char*, Bytes>> chunks) {
        Riff r; for (const auto& [id, body] : chunks) r.Chunk(id, body); return r.Finish();
    };
    ExpectError(audio::DecodeWav(build({{"fmt ", Bytes(fmt16.begin(), fmt16.begin() + 14)}, {"data", Data(4)}})).issue.code, Error::invalid_header, "short fmt");
    ExpectError(audio::DecodeWav(build({{"data", Data(4)}})).issue.code, Error::invalid_layout, "missing fmt");
    ExpectError(audio::DecodeWav(build({{"data", Data(4)}, {"fmt ", fmt16}})).issue.code, Error::invalid_layout, "data before fmt");
    ExpectError(audio::DecodeWav(build({{"fmt ", fmt16}, {"fmt ", fmt16}, {"data", Data(4)}})).issue.code, Error::invalid_layout, "duplicate fmt");
    ExpectError(audio::DecodeWav(build({{"fmt ", fmt16}, {"data", Data(4)}, {"data", Data(4)}})).issue.code, Error::invalid_layout, "duplicate data");
    ExpectError(audio::DecodeWav(build({{"fmt ", fmt16}})).issue.code, Error::invalid_layout, "missing data");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(3, 2, 44100, 32)}, {"data", Data(8)}})).issue.code, Error::unsupported_float, "IEEE float tag");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(2, 44100, 32, 32, 3, 3)}, {"data", Data(8)}})).issue.code, Error::unsupported_float, "extensible float subtype");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(6, 1, 8000, 8)}, {"data", Data(4)}})).issue.code, Error::unsupported_encoding, "A-law");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(2, 1, 8000, 16)}, {"data", Data(4)}})).issue.code, Error::unsupported_encoding, "MS ADPCM");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(2, 44100, 16, 16, 3, 1, false)}, {"data", Data(4)}})).issue.code, Error::unsupported_encoding, "unknown subformat GUID");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(2, 44100, 16, 16, 3, 2)}, {"data", Data(4)}})).issue.code, Error::unsupported_encoding, "extensible ADPCM");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 1, 8000, 12)}, {"data", Data(4)}})).issue.code, Error::unsupported_bit_depth, "12-bit container");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 1, 8000, 64)}, {"data", Data(8)}})).issue.code, Error::unsupported_bit_depth, "64-bit container");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(1, 8000, 8, 3, 4)}, {"data", Data(4)}})).issue.code, Error::unsupported_bit_depth, "3 valid bits");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 2, 44100, 16, 1)}, {"data", Data(10)}})).issue.code, Error::invalid_header, "block align");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 2, 44100, 16, 0, 4)}, {"data", Data(4)}})).issue.code, Error::invalid_header, "average bytes per second");
    ExpectError(audio::DecodeWav(build({{"fmt ", fmt16}, {"data", Data(6)}})).issue.code, Error::invalid_header, "partial frame in data");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(1, 8000, 16, 17, 4)}, {"data", Data(4)}})).issue.code, Error::invalid_header, "valid bits above container");
    ExpectError(audio::DecodeWav(build({{"fmt ", FmtExtensible(1, 8000, 16, 0, 4)}, {"data", Data(4)}})).issue.code, Error::invalid_header, "zero valid bits");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 0, 8000, 16)}, {"data", Data(4)}})).issue.code, Error::channel_limit, "zero channels");
    ExpectError(audio::DecodeWav(build({{"fmt ", Fmt(1, 1, 0, 16)}, {"data", Data(4)}})).issue.code, Error::sample_rate_limit, "zero sample rate");
    // 20 valid bits in 24: the four padding bits must be zero.
    Riff pad; pad.Chunk("fmt ", FmtExtensible(1, 8000, 24, 20, 4));
    pad.Chunk("data", Bytes{std::byte{0x10}, std::byte{0}, std::byte{0}, std::byte{0x01}, std::byte{0}, std::byte{0}});
    const auto padded = audio::DecodeWav(pad.Finish());
    ExpectError(padded.issue.code, Error::invalid_sample_padding, "non-zero padding bits");
    Check(padded.issue.byteOffset == pad.Finish().size() - 3, "padding error offset names the sample");
    // Tolerated: unknown chunks (odd sizes padded) before and after data, fmt extension bytes.
    Riff tolerant;
    tolerant.Chunk("LIST", Data(13, 'x')); tolerant.Chunk("fact", Data(4));
    auto fmt18 = fmt16; fmt18.push_back(std::byte{0}); fmt18.push_back(std::byte{0});
    tolerant.Chunk("fmt ", fmt18); tolerant.Chunk("bext", Data(5)); tolerant.Chunk("data", Data(8, 1)); tolerant.Chunk("id3 ", Data(3));
    const auto t = audio::DecodeWav(tolerant.Finish());
    Check(t && t.report.skippedChunks == 4 && t.audio.frames == 2 && t.audio.samples[0] == 0x0101, "unknown chunks skipped and counted");
    Riff unpadded; unpadded.Chunk("fmt ", fmt16); unpadded.Chunk("data", Data(4)); unpadded.Chunk("junk", Data(3), false);
    ExpectError(audio::DecodeWav(unpadded.Finish()).issue.code, Error::truncated, "odd final chunk without pad byte");
    Riff bigChunk; bigChunk.Chunk("fmt ", fmt16); bigChunk.Tag("data"); bigChunk.Le(1000, 4); bigChunk.b.resize(bigChunk.b.size() + 8);
    ExpectError(audio::DecodeWav(bigChunk.Finish()).issue.code, Error::truncated, "data chunk extends past RIFF");

    // FLAC.
    const auto flac = audio::EncodeFlac(Make(44100, 2, 16, 20000, Signal::mixed, 0x3)).bytes;
    ExpectError(audio::DecodeFlac({}).issue.code, Error::truncated, "empty FLAC");
    auto fm = flac; fm[0] = std::byte{'f' ^ 0x20};
    ExpectError(audio::DecodeFlac(fm).issue.code, Error::wrong_magic, "FLAC magic");
    Bytes ogg = {std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}, std::byte{0}, std::byte{2}};
    ExpectError(audio::DecodeFlac(ogg).issue.code, Error::unsupported_container, "Ogg FLAC");
    ExpectError(audio::DecodeFlac(good).issue.code, Error::wrong_magic, "WAV given to FLAC decoder");
    ExpectError(audio::DecodeWav(flac).issue.code, Error::wrong_magic, "FLAC given to WAV decoder");
    std::size_t step = std::max<std::size_t>(1, flac.size() / 997);
    int accepted = 0;
    for (std::size_t cut = 0; cut < flac.size(); cut += (cut < 200 ? 1 : step))
        if (audio::DecodeFlac(Bytes(flac.begin(), flac.begin() + std::ptrdiff_t(cut)))) ++accepted;
    Check(accepted == 0, "every truncated FLAC prefix is rejected");
    auto frameFlip = flac; frameFlip[flac.size() / 2] ^= std::byte{0x10};
    Check(!audio::DecodeFlac(frameFlip), "frame bit flip rejected");
    ExpectError(audio::DecodeFlac(frameFlip).issue.code, Error::corrupt_stream, "frame bit flip is a CRC/sync error");
    const auto blocks = FlacBlocks(flac);
    auto md5 = flac; md5[blocks[0].body + 20] ^= std::byte{0x01};
    ExpectError(audio::DecodeFlac(md5).issue.code, Error::checksum_mismatch, "STREAMINFO MD5 mismatch");
    auto total = flac; total[blocks[0].body + 17] ^= std::byte{0x01};
    ExpectError(audio::DecodeFlac(total).issue.code, Error::length_mismatch, "STREAMINFO total samples mismatch");
    auto trailingFlac = flac; trailingFlac.insert(trailingFlac.end(), 64, std::byte{0x55});
    Check(!audio::DecodeFlac(trailingFlac), "trailing garbage after the last frame rejected");
    auto firstBlock = flac; firstBlock[4] = std::byte{0x04};
    ExpectError(audio::DecodeFlac(firstBlock).issue.code, Error::invalid_layout, "first block not STREAMINFO");
    auto reserved = flac; reserved[blocks[1].body - 4] = std::byte{0x80 | 127};
    ExpectError(audio::DecodeFlac(reserved).issue.code, Error::invalid_metadata, "forbidden metadata block type");
    const auto maskAt = Find(flac, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x");
    auto badMask = flac; badMask[maskAt + 35] = std::byte{'Z'};
    ExpectError(audio::DecodeFlac(badMask).issue.code, Error::invalid_metadata, "malformed mask comment");
    // Unrelated metadata blocks are skipped and reported, never carried.
    Bytes withApp(flac.begin(), flac.begin() + std::ptrdiff_t(blocks[1].body - 4));
    for (std::uint8_t b : {std::uint8_t(2), std::uint8_t(0), std::uint8_t(0), std::uint8_t(4), std::uint8_t('t'), std::uint8_t('e'), std::uint8_t('s'), std::uint8_t('t')})
        withApp.push_back(std::byte(b));
    withApp.insert(withApp.end(), flac.begin() + std::ptrdiff_t(blocks[1].body - 4), flac.end());
    const auto app = audio::DecodeFlac(withApp);
    Check(app && app.report.ignoredMetadataBlocks == 1 && !app.report.ignoredComments, "APPLICATION block skipped and reported");
    // ID3v2 prefix is skipped within the metadata limit.
    Bytes id3 = {std::byte{'I'}, std::byte{'D'}, std::byte{'3'}, std::byte{4}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{4}};
    id3.resize(14); id3.insert(id3.end(), flac.begin(), flac.end());
    const auto withId3 = audio::DecodeFlac(id3);
    Check(withId3 && withId3.report.ignoredMetadataBlocks == 1, "ID3v2-prefixed FLAC");
    id3[6] = std::byte{0x80};
    ExpectError(audio::DecodeFlac(id3).issue.code, Error::invalid_metadata, "ID3v2 size not syncsafe");
}

void LimitsCheck() {
    const auto a = Make(44100, 2, 16, 3000, Signal::mixed);
    const auto wav = audio::EncodeWav(a).bytes;
    const auto flac = audio::EncodeFlac(a).bytes;
    audio::Limits l;
    l.maximumSourceBytes = wav.size() - 1;
    ExpectError(audio::DecodeWav(wav, l).issue.code, Error::input_too_large, "WAV source bytes");
    l.maximumSourceBytes = wav.size();
    Check(bool(audio::DecodeWav(wav, l)), "WAV exactly at source limit");
    l = {}; l.maximumSourceBytes = flac.size() - 1;
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::input_too_large, "FLAC source bytes");
    l = {}; l.maximumTotalSamples = 5999;
    ExpectError(audio::DecodeWav(wav, l).issue.code, Error::sample_limit, "WAV total samples");
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::sample_limit, "FLAC total samples (STREAMINFO)");
    ExpectError(audio::EncodeWav(a, l).error, Error::sample_limit, "EncodeWav total samples");
    ExpectError(audio::EncodeFlac(a, l).error, Error::sample_limit, "EncodeFlac total samples");
    l.maximumTotalSamples = 6000;
    Check(audio::DecodeWav(wav, l) && audio::DecodeFlac(flac, l), "exactly at sample limit");
    // A FLAC with unknown total length is bounded during decoding.
    auto unknown = flac; const auto si = FlacBlocks(flac)[0].body;
    unknown[si + 13] &= std::byte{0xF0}; unknown[si + 14] = unknown[si + 15] = unknown[si + 16] = unknown[si + 17] = std::byte{0};
    Check(bool(audio::DecodeFlac(unknown)), "unknown total samples accepted within limits");
    l = {}; l.maximumTotalSamples = 4000;
    ExpectError(audio::DecodeFlac(unknown, l).issue.code, Error::sample_limit, "unknown-length FLAC bounded while decoding");
    l = {}; l.maximumChannels = 1;
    ExpectError(audio::DecodeWav(wav, l).issue.code, Error::channel_limit, "WAV channels");
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::channel_limit, "FLAC channels");
    ExpectError(audio::EncodeFlac(a, l).error, Error::channel_limit, "EncodeFlac channels");
    l = {}; l.maximumChannels = 16;
    const auto nine = Make(48000, 9, 16, 10, Signal::sine);
    Check(audio::EncodeWav(nine, l) && audio::DecodeWav(audio::EncodeWav(nine, l).bytes, l), "9-channel WAV within raised limit");
    ExpectError(audio::EncodeFlac(nine, l).error, Error::channel_limit, "FLAC cannot carry 9 channels");
    l = {}; l.minimumSampleRate = 48000;
    ExpectError(audio::DecodeWav(wav, l).issue.code, Error::sample_rate_limit, "WAV minimum rate");
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::sample_rate_limit, "FLAC minimum rate");
    l = {}; l.maximumSampleRate = 22050;
    ExpectError(audio::DecodeWav(wav, l).issue.code, Error::sample_rate_limit, "WAV maximum rate");
    ExpectError(audio::EncodeWav(a, l).error, Error::sample_rate_limit, "EncodeWav maximum rate");
    l = {}; l.maximumSampleRate = 2000000;
    ExpectError(audio::EncodeFlac(Make(1100000, 1, 16, 10, Signal::sine), l).error, Error::sample_rate_limit, "FLAC's own sample-rate ceiling");
    Check(bool(audio::EncodeFlac(Make(705600, 1, 16, 100, Signal::sine), l)), "non-subset high sample rate still encodes");
    // Metadata sizes and counts.
    Riff big; big.Chunk("LIST", Data(2048)); big.Chunk("fmt ", Fmt(1, 1, 8000, 16)); big.Chunk("data", Data(2));
    l = {}; l.maximumMetadataBlockBytes = 1024;
    ExpectError(audio::DecodeWav(big.Finish(), l).issue.code, Error::metadata_limit, "WAV chunk size");
    l.maximumMetadataBlockBytes = 4096;
    Check(bool(audio::DecodeWav(big.Finish(), l)), "WAV chunk within size limit");
    Riff many; many.Chunk("fmt ", Fmt(1, 1, 8000, 16)); for (int i = 0; i < 20; ++i) many.Chunk("junk", Data(2)); many.Chunk("data", Data(2));
    l = {}; l.maximumMetadataBlocks = 10;
    ExpectError(audio::DecodeWav(many.Finish(), l).issue.code, Error::metadata_limit, "WAV chunk count");
    l = {}; l.maximumMetadataBlockBytes = 39; // STREAMINFO (34) fits; the 40-byte vendor-only VORBIS_COMMENT does not
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::metadata_limit, "FLAC metadata block size");
    l = {}; l.maximumMetadataBlocks = 1;
    ExpectError(audio::DecodeFlac(flac, l).issue.code, Error::metadata_limit, "FLAC metadata block count");
    // Output.
    l = {}; l.maximumOutputBytes = wav.size() - 1;
    ExpectError(audio::EncodeWav(a, l).error, Error::output_limit, "WAV output bytes");
    l.maximumOutputBytes = wav.size();
    Check(bool(audio::EncodeWav(a, l)), "WAV exactly at output limit");
    l = {}; l.maximumOutputBytes = flac.size() - 1;
    ExpectError(audio::EncodeFlac(a, l).error, Error::output_limit, "FLAC output bytes");
    l.maximumOutputBytes = flac.size();
    const auto exact = audio::EncodeFlac(a, l);
    Check(exact && exact.bytes == flac, "FLAC exactly at output limit");
    // Canonical audio validation.
    auto range = a; range.samples[17] = 32768;
    ExpectError(audio::EncodeFlac(range).error, Error::invalid_audio, "sample above 16-bit range");
    ExpectError(audio::EncodeWav(range).error, Error::invalid_audio, "WAV sample above range");
    auto size = a; size.samples.pop_back();
    ExpectError(audio::EncodeFlac(size).error, Error::invalid_audio, "sample count mismatch");
    auto bits = a; bits.bitsPerSample = 3;
    ExpectError(audio::ValidateAudio(bits), Error::unsupported_bit_depth, "3-bit audio");
    bits.bitsPerSample = 33;
    ExpectError(audio::ValidateAudio(bits), Error::unsupported_bit_depth, "33-bit audio");
    auto zero = a; zero.sampleRate = 0;
    ExpectError(audio::ValidateAudio(zero), Error::sample_rate_limit, "zero sample rate");
}

void Determinism() {
    const auto a = Make(48000, 2, 24, 48000 * 3, Signal::mixed, 0x3);
    const auto first = audio::EncodeFlac(a);
    (void)audio::EncodeFlac(Make(8000, 1, 8, 777, Signal::noise)); // unrelated work between runs
    const auto second = audio::EncodeFlac(a);
    Check(first && second && first.bytes == second.bytes, "FLAC encode twice is byte-identical");
    const auto w1 = audio::EncodeWav(a), w2 = audio::EncodeWav(a);
    Check(w1 && w2 && w1.bytes == w2.bytes, "WAV encode twice is byte-identical");
    const auto c1 = audio::Convert(audio::Container::wav, audio::Container::flac, w1.bytes);
    Check(c1 && c1.bytes == first.bytes, "Convert(wav->flac) equals EncodeFlac of the same samples");
    std::uint64_t h = 1469598103934665603ull;
    for (auto b : first.bytes) h = (h ^ std::to_integer<std::uint64_t>(b)) * 1099511628211ull;
    char text[17]; std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
    std::cout << "FLAC fingerprint (this build): " << text << " bytes=" << first.bytes.size() << '\n';
}

void LongSignal() {
    // Three minutes of 24-bit stereo at 48 kHz: tones, square bursts, dither and extrema.
    const auto a = Make(48000, 2, 24, 48000ull * 180, Signal::mixed, std::nullopt, 7);
    const auto wav = audio::EncodeWav(a);
    const auto flac = audio::EncodeFlac(a);
    Check(wav && flac, "long signal encodes");
    const auto fromFlac = audio::DecodeFlac(flac.bytes);
    Check(fromFlac && Same(fromFlac.audio, a), "long FLAC exact");
    const auto fromWav = audio::DecodeWav(wav.bytes);
    Check(fromWav && Same(fromWav.audio, a), "long WAV exact");
    const auto back = audio::EncodeWav(fromFlac.audio);
    Check(back && back.bytes == wav.bytes, "long WAV->FLAC->WAV byte-identical");
    Check(flac.bytes.size() < wav.bytes.size(), "FLAC compresses the long tonal signal");
    std::cout << "long signal: frames=" << a.frames << " wav=" << wav.bytes.size() << " flac=" << flac.bytes.size() << '\n';
}

void Conversion() {
    Riff r; r.Chunk("LIST", Data(6, 'i')); r.Chunk("fmt ", FmtExtensible(2, 96000, 24, 24, 0x3));
    Riff d; for (int i = 0; i < 300; ++i) d.Le(std::uint32_t(i * 7919 - 1000000) & 0xFFFFFF, 3);
    r.Chunk("data", d.b);
    const auto wav = r.Finish();
    const auto toFlac = audio::Convert(audio::Container::wav, audio::Container::flac, wav);
    Check(toFlac && toFlac.sampleRate == 96000 && toFlac.channels == 2 && toFlac.bitsPerSample == 24 && toFlac.frames == 150
        && toFlac.report.skippedChunks == 1, "Convert wav->flac reports parameters and skipped chunks");
    const auto toWav = audio::Convert(audio::Container::flac, audio::Container::wav, toFlac.bytes);
    Riff expected; expected.Chunk("fmt ", FmtExtensible(2, 96000, 24, 24, 0x3)); expected.Chunk("data", d.b);
    Check(toWav && toWav.bytes == expected.Finish(), "Convert flac->wav reproduces the WAV without the skipped chunk");
    const auto bad = audio::Convert(audio::Container::flac, audio::Container::wav, wav);
    ExpectError(bad.decodeIssue.code, Error::wrong_magic, "Convert reports the decode issue");
    const auto unknown = audio::Convert(audio::Container(9), audio::Container::wav, wav);
    Check(!unknown, "Convert rejects an unknown container");
    Check(audio::ErrorMessage(Error::unsupported_float).find("float") != std::string_view::npos, "error message text");
}

// --- independent interoperability support (driven by tests/interop.cmake) ---
bool WriteFile(const std::string& path, const Bytes& b) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(out);
}
Bytes ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Bytes b(s.size());
    if (!s.empty()) std::memcpy(b.data(), s.data(), s.size());
    return b;
}
Bytes Raw(const audio::Audio& a) { // packed little-endian, container = ceil(bits/8)
    Bytes out; const auto width = (a.bitsPerSample + 7) / 8;
    for (auto s : a.samples) for (unsigned i = 0; i < width; ++i) out.push_back(std::byte((std::uint32_t(s) >> (8 * i)) & 0xFF));
    return out;
}
int Emit(const std::string& dir) {
    const auto s16 = Make(44100, 2, 16, 44100 * 2, Signal::mixed, std::nullopt, 16);
    const auto s24 = Make(96000, 2, 24, 96000, Signal::mixed, std::nullopt, 24);
    bool ok = true;
    for (const auto& [name, a] : {std::pair{"s16", s16}, std::pair{"s24", s24}}) {
        ok &= WriteFile(dir + "/" + name + ".raw", Raw(a));
        ok &= WriteFile(dir + "/" + name + ".wav", audio::EncodeWav(a).bytes);
        ok &= WriteFile(dir + "/" + name + ".flac", audio::EncodeFlac(a).bytes);
    }
    return ok ? 0 : 1;
}
int Compare(const std::string& container, const std::string& file, const std::string& raw, std::uint32_t rate, std::uint32_t channels, std::uint32_t bits) {
    const auto bytes = ReadFile(file);
    const auto r = container == "flac" ? audio::DecodeFlac(bytes) : audio::DecodeWav(bytes);
    if (!r) { std::cerr << file << ": " << audio::ErrorMessage(r.issue.code) << '\n'; return 1; }
    if (r.audio.sampleRate != rate || r.audio.channels != channels || r.audio.bitsPerSample != bits) {
        std::cerr << file << ": parameters " << r.audio.sampleRate << '/' << r.audio.channels << '/' << r.audio.bitsPerSample << '\n';
        return 1;
    }
    if (Raw(r.audio) != ReadFile(raw)) { std::cerr << file << ": samples differ from " << raw << '\n'; return 1; }
    std::cout << file << ": " << r.audio.frames << " frames identical; skipped chunks " << r.report.skippedChunks
              << ", ignored FLAC blocks " << r.report.ignoredMetadataBlocks << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--version") {
            std::cout << "arken_audio_codecs_v1 r1 (" << audio::FlacVendorString() << ")\n";
            return 0;
        }
    if (argc == 3 && std::string_view(argv[1]) == "--emit") return Emit(argv[2]);
    if (argc == 8 && std::string_view(argv[1]) == "--compare")
        return Compare(argv[2], argv[3], argv[4], std::uint32_t(std::stoul(argv[5])), std::uint32_t(std::stoul(argv[6])), std::uint32_t(std::stoul(argv[7])));
    const std::pair<std::string_view, std::function<void()>> checks[] = {
        {"WavRoundTrip", WavRoundTrip}, {"FlacRoundTrip", FlacRoundTrip}, {"ChannelMask", ChannelMask},
        {"Malformed", Malformed}, {"Limits", LimitsCheck}, {"Determinism", Determinism},
        {"LongSignal", LongSignal}, {"Conversion", Conversion}};
    std::string_view selected;
    if (argc == 3 && std::string_view(argv[1]) == "--check") selected = argv[2];
    else if (argc != 1) { std::cerr << "usage: arken-audio-codec-tests [--check NAME | --version | --emit DIR | --compare ...]\n"; return 2; }
    bool ran = false;
    for (const auto& [name, run] : checks)
        if (selected.empty() || selected == name) { run(); ran = true; }
    if (!ran) { std::cerr << "unknown check " << selected << '\n'; return 2; }
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "ok\n";
    return 0;
}
