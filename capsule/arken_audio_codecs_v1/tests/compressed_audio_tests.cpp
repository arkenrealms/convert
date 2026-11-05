// Native qualification of the lossy codecs of arken_audio_codecs_v1: Ogg Vorbis,
// Ogg Opus and MP3 decode/encode, encoder delay and padding, quality floors,
// channel handling, determinism, limits, truncation/corruption and Ogg structure.
#include "arken_audio_codecs_v1/api.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace audio = arken::audio::v1;
using audio::Container;
using audio::Error;
using Bytes = std::vector<std::byte>;

namespace {
int failures = 0;
bool verbose = false;
void Check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
std::string Name(Error e) { return std::string(audio::ErrorMessage(e)); }
void ExpectError(Error observed, Error expected, const std::string& what) {
    if (observed != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": expected '" << Name(expected) << "', observed '" << Name(observed) << "'\n";
    }
}
constexpr double pi = 3.14159265358979323846;

struct Lcg {
    std::uint64_t state;
    double Next() { state = state * 6364136223846793005ull + 1442695040888963407ull; return double(state >> 11) / double(1ull << 53) * 2.0 - 1.0; }
};

// Representative programme material (per handoff section 10): one second each
// of speech-like voiced sound, a music chord, transients, then silence.
enum class Material { programme, tone440, speech, music, transients, silence };
double Value(Material m, double t, std::uint32_t c, Lcg& lcg) {
    const double detune = 1.0 + 0.003 * c;
    switch (m) {
    case Material::tone440: return 0.5 * std::sin(2 * pi * 440.0 * t);
    case Material::speech: {
        const double f0 = 120.0 * (1.0 + 0.08 * std::sin(2 * pi * 3.0 * t)) * detune;
        const double envelope = 0.5 + 0.5 * std::sin(2 * pi * 4.0 * t);
        double s = 0;
        for (int h = 1; h <= 20; ++h) {
            const double f = f0 * h;
            const double formant = std::exp(-std::pow((f - 700.0) / 300.0, 2)) + 0.6 * std::exp(-std::pow((f - 1200.0) / 400.0, 2))
                + 0.3 * std::exp(-std::pow((f - 2500.0) / 500.0, 2));
            s += formant * std::sin(2 * pi * f * t + h);
        }
        return 0.25 * envelope * s;
    }
    case Material::music: {
        double s = 0;
        for (double f : {220.0, 277.18, 329.63, 440.0, 659.25})
            for (int h = 1; h <= 4; ++h) s += std::sin(2 * pi * f * h * detune * t) / (h * h);
        return 0.12 * s * (0.7 + 0.3 * std::sin(2 * pi * 0.5 * t));
    }
    case Material::transients: {
        const double phase = std::fmod(t, 0.25);
        return 0.6 * std::exp(-phase * 60.0) * lcg.Next() + 0.02 * std::sin(2 * pi * 330.0 * t);
    }
    case Material::silence: return 0.0;
    case Material::programme: {
        const int segment = int(t);
        const Material parts[4] = {Material::speech, Material::music, Material::transients, Material::silence};
        return Value(parts[std::min(segment, 3)], t, c, lcg);
    }
    }
    return 0;
}

audio::Audio Render(Material m, std::uint32_t rate, std::uint32_t channels, double seconds, std::uint32_t bits = 16) {
    audio::Audio a;
    a.sampleRate = rate; a.channels = channels; a.bitsPerSample = bits;
    a.frames = std::uint64_t(std::llround(seconds * rate));
    a.samples.resize(a.frames * channels);
    Lcg lcg{12345};
    const double full = double(std::int64_t(1) << (bits - 1)) - 1;
    for (std::uint64_t f = 0; f < a.frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c)
            a.samples[f * channels + c] = std::int32_t(std::llround(std::clamp(Value(m, double(f) / rate, c, lcg), -1.0, 1.0) * full));
    return a;
}

double Scale(std::uint32_t bits) { return double(std::int64_t(1) << (bits - 1)); }
// SNR (dB) of `test` against `ref` over frames [from, to), all channels, at a lag.
double Snr(const audio::Audio& ref, const audio::Audio& test, std::uint64_t from = 0, std::uint64_t to = ~0ull) {
    to = std::min({to, ref.frames, test.frames});
    double signal = 0, noise = 0;
    for (std::uint64_t f = from; f < to; ++f)
        for (std::uint32_t c = 0; c < ref.channels; ++c) {
            const double r = ref.samples[f * ref.channels + c] / Scale(ref.bitsPerSample);
            const double t = test.samples[f * test.channels + c] / Scale(test.bitsPerSample);
            signal += r * r;
            noise += (r - t) * (r - t);
        }
    return noise == 0 ? 200.0 : 10.0 * std::log10(signal / noise);
}
// Lag (test relative to ref) in [-range, range] maximising cross-correlation of channel 0.
long BestLag(const audio::Audio& ref, const audio::Audio& test, long range, std::uint64_t from, std::uint64_t length) {
    long best = 0;
    double bestValue = -1e300;
    for (long lag = -range; lag <= range; ++lag) {
        double sum = 0;
        for (std::uint64_t i = from; i < from + length; ++i) {
            const long j = long(i) + lag;
            if (j < 0 || std::uint64_t(j) >= test.frames || i >= ref.frames) continue;
            sum += double(ref.samples[i * ref.channels]) * double(test.samples[std::size_t(j) * test.channels]);
        }
        if (sum > bestValue) { bestValue = sum; best = lag; }
    }
    return best;
}
std::int32_t PeakAbs(const audio::Audio& a, std::uint64_t from, std::uint64_t to) {
    std::int32_t peak = 0;
    for (std::uint64_t f = from; f < std::min(to, a.frames); ++f)
        for (std::uint32_t c = 0; c < a.channels; ++c) peak = std::max(peak, std::abs(a.samples[f * a.channels + c]));
    return peak;
}

audio::EncodeResult Encode(Container c, const audio::Audio& a, const audio::LossyOptions& o = {}, const audio::Limits& l = {}) {
    switch (c) {
    case Container::vorbis: return audio::EncodeVorbis(a, o, l);
    case Container::opus: return audio::EncodeOpus(a, o, l);
    case Container::mp3: return audio::EncodeMp3(a, o, l);
    default: return {Error::invalid_audio, {}};
    }
}
audio::DecodeResult Decode(Container c, const Bytes& b, const audio::Limits& l = {}) {
    switch (c) {
    case Container::vorbis: return audio::DecodeVorbis(b, l);
    case Container::opus: return audio::DecodeOpus(b, l);
    case Container::mp3: return audio::DecodeMp3(b, l);
    case Container::wav: return audio::DecodeWav(b, l);
    case Container::flac: return audio::DecodeFlac(b, l);
    case Container::aiff: return audio::DecodeAiff(b, l);
    }
    return {};
}
const char* CodecName(Container c) { return c == Container::vorbis ? "vorbis" : c == Container::opus ? "opus" : "mp3"; }
// Frames a decoder must return for `source`: Opus always decodes at 48 kHz.
std::uint64_t ExpectedFrames(Container c, const audio::Audio& source) {
    return c == Container::opus ? source.frames * (48000 / source.sampleRate) : source.frames;
}
// Reference at the decoder's rate (Opus at 48 kHz from 48 kHz sources only).
struct Setting { audio::LossyOptions options; std::string label; };
std::vector<Setting> TwoSettings(Container c) {
    audio::LossyOptions low, high;
    if (c == Container::vorbis) { low.vorbisQuality = 20; high.vorbisQuality = 70; }
    if (c == Container::opus) { low.opusBitrateKbps = 64; high.opusBitrateKbps = 160; }
    if (c == Container::mp3) { low.mp3BitrateKbps = 128; high.mp3RateControl = audio::Mp3RateControl::vbr; high.mp3VbrQuality = 0; }
    return {{low, "low"}, {high, "high"}};
}

// ---- independent Ogg page helpers (no libogg): parse, rewrite and re-CRC ----
struct Page { Bytes header; Bytes body; };
std::uint32_t OggCrc(const Bytes& data) {
    std::uint32_t crc = 0;
    for (auto byte : data) {
        crc ^= std::uint32_t(std::to_integer<std::uint8_t>(byte)) << 24;
        for (int i = 0; i < 8; ++i) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}
std::uint8_t At(const Bytes& b, std::size_t i) { return std::to_integer<std::uint8_t>(b[i]); }
std::vector<Page> Pages(const Bytes& b) {
    std::vector<Page> pages;
    for (std::size_t p = 0; p + 27 <= b.size();) {
        const std::size_t segments = At(b, p + 26);
        std::size_t body = 0;
        for (std::size_t i = 0; i < segments; ++i) body += At(b, p + 27 + i);
        Page page;
        page.header.assign(b.begin() + std::ptrdiff_t(p), b.begin() + std::ptrdiff_t(p + 27 + segments));
        page.body.assign(b.begin() + std::ptrdiff_t(p + 27 + segments), b.begin() + std::ptrdiff_t(p + 27 + segments + body));
        pages.push_back(page);
        p += 27 + segments + body;
    }
    return pages;
}
Bytes Join(const std::vector<Page>& pages) {
    Bytes out;
    for (auto page : pages) {
        for (int i = 22; i < 26; ++i) page.header[std::size_t(i)] = std::byte{0};
        Bytes whole = page.header;
        whole.insert(whole.end(), page.body.begin(), page.body.end());
        const auto crc = OggCrc(whole);
        for (int i = 0; i < 4; ++i) whole[std::size_t(22 + i)] = std::byte((crc >> (8 * i)) & 0xFF);
        out.insert(out.end(), whole.begin(), whole.end());
    }
    return out;
}
// Replaces the body of a single-packet page (first page: OpusHead / Vorbis id) and its lacing.
void SetSinglePacket(Page& page, const Bytes& body) {
    Bytes header(page.header.begin(), page.header.begin() + 26);
    std::size_t left = body.size();
    Bytes lacing;
    while (left >= 255) { lacing.push_back(std::byte{255}); left -= 255; }
    lacing.push_back(std::byte(left));
    header.push_back(std::byte(lacing.size()));
    header.insert(header.end(), lacing.begin(), lacing.end());
    page.header = header;
    page.body = body;
}
std::int64_t Granule(const Page& p) {
    std::int64_t g = 0;
    for (int i = 7; i >= 0; --i) g = (g << 8) | At(p.header, std::size_t(6 + i));
    return g;
}
void SetGranule(Page& p, std::int64_t g) {
    for (int i = 0; i < 8; ++i) p.header[std::size_t(6 + i)] = std::byte((std::uint64_t(g) >> (8 * i)) & 0xFF);
}
void SetSerial(Page& p, std::uint32_t s) {
    for (int i = 0; i < 4; ++i) p.header[std::size_t(14 + i)] = std::byte((s >> (8 * i)) & 0xFF);
}

// ---------------------------------------------------------------------------
void Quality() {
    // Each encoder, mono and stereo, two quality settings, programme material:
    // exact frame count, alignment (lag 0), SNR floor, higher setting not worse,
    // silence stays silent.
    struct Floor { Container codec; double low, high; };
    // Floors are measured minima minus a margin (see NOTES.md): waveform SNR of
    // perceptual codecs on this material, not a perceptual-quality claim.
    const Floor floors[] = {{Container::vorbis, 9.0, 21.0}, {Container::opus, 9.0, 13.0}, {Container::mp3, 12.0, 20.0}};
    for (const auto& floor : floors) {
        for (std::uint32_t channels : {1u, 2u}) {
            const std::uint32_t rate = floor.codec == Container::opus ? 48000 : 44100;
            const auto source = Render(Material::programme, rate, channels, 4.0);
            double previous = -1e9;
            for (const auto& setting : TwoSettings(floor.codec)) {
                const auto what = std::string(CodecName(floor.codec)) + " " + std::to_string(channels) + "ch " + setting.label;
                const auto encoded = Encode(floor.codec, source, setting.options);
                Check(bool(encoded), what + ": encode " + Name(encoded.error));
                if (!encoded) continue;
                const auto decoded = Decode(floor.codec, encoded.bytes);
                Check(bool(decoded), what + ": decode " + Name(decoded.issue.code));
                if (!decoded) continue;
                Check(decoded.audio.frames == ExpectedFrames(floor.codec, source), what + ": frame count " + std::to_string(decoded.audio.frames));
                Check(decoded.audio.channels == channels && decoded.audio.bitsPerSample == 16 && !decoded.audio.channelMask,
                    what + ": parameters");
                Check(decoded.audio.sampleRate == (floor.codec == Container::opus ? 48000u : rate), what + ": rate");
                Check(audio::ValidateAudio(decoded.audio) == Error::none, what + ": canonical");
                Check(decoded.report.lossy, what + ": lossy flag");
                const auto lag = BestLag(source, decoded.audio, 1500, rate / 4, 8192);
                Check(lag == 0, what + ": alignment lag " + std::to_string(lag));
                const double snr = Snr(source, decoded.audio, 0, std::uint64_t(3) * rate);
                const double floorDb = setting.label == "low" ? floor.low : floor.high;
                if (verbose) std::cout << what << ": " << encoded.bytes.size() << " bytes, SNR " << std::fixed << std::setprecision(2) << snr << " dB\n";
                Check(snr >= floorDb, what + ": SNR " + std::to_string(snr) + " dB below floor " + std::to_string(floorDb));
                Check(snr >= previous - 0.5, what + ": higher setting is worse");
                previous = snr;
                // Silence segment (seconds 3..4 minus 150 ms margins) stays within a few LSB.
                const auto peak = PeakAbs(decoded.audio, std::uint64_t(3.15 * rate), std::uint64_t(3.85 * rate));
                Check(peak <= 8, what + ": silence peak " + std::to_string(peak));
            }
        }
    }
}

void Gapless() {
    // 1 s 440 Hz tone must return exactly 44100 (48000 for Opus) frames at lag 0.
    for (std::uint32_t channels : {1u, 2u}) {
        const auto tone = Render(Material::tone440, 44100, channels, 1.0);
        for (const auto mode : {audio::Mp3RateControl::cbr, audio::Mp3RateControl::vbr}) {
            audio::LossyOptions o;
            o.mp3RateControl = mode;
            const auto what = std::string("mp3 ") + (mode == audio::Mp3RateControl::cbr ? "cbr " : "vbr ") + std::to_string(channels) + "ch";
            const auto mp3 = audio::EncodeMp3(tone, o);
            Check(bool(mp3), what + ": encode");
            const auto d = audio::DecodeMp3(mp3.bytes);
            Check(bool(d) && d.audio.frames == 44100, what + ": 1 s tone frames " + std::to_string(d.audio.frames));
            Check(d.report.encoderDelayTrimmed && !d.report.encoderDelayUnknown, what + ": LAME tag delay applied");
            // LAME 3.100: encoder delay 576 + decoder delay 529.
            Check(d.report.trimmedLeadingFrames == 576 + 529, what + ": leading trim " + std::to_string(d.report.trimmedLeadingFrames));
            Check(d.report.trimmedTrailingFrames > 0, what + ": trailing padding trimmed");
            Check(BestLag(tone, d.audio, 45, 4410, 8192) == 0, what + ": aligned (within half a period)");
            Check(Snr(tone, d.audio) > 25.0, what + ": tone SNR " + std::to_string(Snr(tone, d.audio)));
            if (mode == audio::Mp3RateControl::cbr && channels == 2) {
                // Without the Info frame nothing is trimmed: decoder output keeps the
                // delay, and the report says the delay is unknown. Programme material
                // (not a periodic tone) makes the lag unambiguous.
                const auto programme = Render(Material::programme, 44100, 2, 2.0);
                const auto tagged = audio::EncodeMp3(programme, o);
                const auto d2 = audio::DecodeMp3(tagged.bytes);
                Check(d2 && d2.audio.frames == programme.frames && BestLag(programme, d2.audio, 2000, 4410, 8192) == 0,
                    "tagged programme mp3: exact length at lag 0");
                const auto frameBytes = std::size_t(144000 * 192 / 44100); // first frame: no padding bit
                Bytes untagged(tagged.bytes.begin() + std::ptrdiff_t(frameBytes), tagged.bytes.end());
                const auto u = audio::DecodeMp3(untagged);
                Check(bool(u), "untagged mp3 decodes: " + Name(u.issue.code));
                Check(u.report.encoderDelayUnknown && !u.report.encoderDelayTrimmed && u.report.trimmedLeadingFrames == 0,
                    "untagged mp3: delay reported unknown, nothing trimmed");
                Check(u.audio.frames % 1152 == 0 && u.audio.frames == d2.audio.frames + d2.report.trimmedLeadingFrames
                    + d2.report.trimmedTrailingFrames, "untagged mp3: every decoded frame kept " + std::to_string(u.audio.frames));
                Check(BestLag(programme, u.audio, 2000, 4410, 8192) == 576 + 529, "untagged mp3: delay visible as lag "
                    + std::to_string(BestLag(programme, u.audio, 2000, 4410, 8192)));
            }
        }
        const auto vorbis = audio::DecodeVorbis(audio::EncodeVorbis(tone).bytes);
        Check(bool(vorbis) && vorbis.audio.frames == 44100, "vorbis 1 s tone frames");
        // Vorbis has no encoder delay; only the final granule trims the last block.
        Check(vorbis.report.trimmedLeadingFrames == 0 && vorbis.report.trimmedTrailingFrames > 0
            && vorbis.report.trimmedTrailingFrames < 2048, "vorbis end trim only: "
            + std::to_string(vorbis.report.trimmedLeadingFrames) + "+" + std::to_string(vorbis.report.trimmedTrailingFrames));
        const auto tone48 = Render(Material::tone440, 48000, channels, 1.0);
        const auto opusBytes = audio::EncodeOpus(tone48).bytes;
        const auto opus = audio::DecodeOpus(opusBytes);
        Check(bool(opus) && opus.audio.frames == 48000, "opus 1 s tone frames");
        Check(opus.report.encoderDelayTrimmed && opus.report.trimmedLeadingFrames == 312, "opus pre-skip 312 trimmed: "
            + std::to_string(opus.report.trimmedLeadingFrames));
        Check(opus.report.originalSampleRate == 48000, "opus original rate");
        Check(BestLag(tone48, opus.audio, 45, 4800, 8192) == 0, "opus aligned (within half a period)");
    }
    // Opus at 16 kHz input: decoded at 48 kHz with exactly 3x the frames; original rate reported.
    const auto tone16 = Render(Material::tone440, 16000, 1, 1.0);
    const auto o16 = audio::DecodeOpus(audio::EncodeOpus(tone16).bytes);
    Check(bool(o16) && o16.audio.frames == 48000 && o16.audio.sampleRate == 48000 && o16.report.originalSampleRate == 16000,
        "opus 16 kHz input: 48 kHz output, 48000 frames, original rate 16000");
    // Short streams whose only audio page is also the EOS page: a granule below
    // the decoded count trims the end (Vorbis I A.2 / RFC 7845 4.5), not the start.
    for (Container c : {Container::vorbis, Container::opus}) {
        const auto clip = Render(Material::speech, 48000, 2, 0.05);
        const auto bytes = Encode(c, clip).bytes;
        std::size_t audioPages = 0;
        for (const auto& page : Pages(bytes)) audioPages += Granule(page) > 0;
        const auto d = Decode(c, bytes);
        const auto lag = d ? BestLag(clip, d.audio, 150, 200, 1800) : -1;
        Check(audioPages == 1 && d && d.audio.frames == clip.frames && lag == 0 && d.report.trimmedTrailingFrames > 0,
            std::string(CodecName(c)) + ": single audio page trims the end: pages " + std::to_string(audioPages) + ", frames "
            + std::to_string(d.audio.frames) + ", lag " + std::to_string(lag) + ", trim "
            + std::to_string(d.report.trimmedLeadingFrames) + "+" + std::to_string(d.report.trimmedTrailingFrames));
    }
    // Odd lengths and empty input.
    for (std::uint64_t frames : {0ull, 1ull, 575ull, 1153ull, 4097ull}) {
        auto a = Render(Material::music, 48000, 2, 0.0);
        a.frames = frames;
        a.samples.assign(frames * 2, 1000);
        for (Container c : {Container::vorbis, Container::opus, Container::mp3}) {
            const auto e = Encode(c, a);
            const auto d = e ? Decode(c, e.bytes) : audio::DecodeResult{};
            Check(bool(e) && bool(d) && d.audio.frames == frames, std::string(CodecName(c)) + " " + std::to_string(frames)
                + " frames round trip length " + std::to_string(d.audio.frames) + " " + Name(e.error) + " " + Name(d.issue.code));
        }
    }
}

void Determinism() {
    const auto source = Render(Material::programme, 48000, 2, 2.0);
    for (Container c : {Container::vorbis, Container::opus, Container::mp3}) {
        const auto first = Encode(c, source);
        (void)Encode(c, Render(Material::speech, 48000, 1, 0.5)); // unrelated work in between
        const auto second = Encode(c, source);
        Check(first && second && first.bytes == second.bytes, std::string(CodecName(c)) + ": identical bytes twice");
        const auto wav = audio::EncodeWav(source);
        const auto converted = audio::Convert(Container::wav, c, wav.bytes);
        Check(bool(converted) && converted.bytes == first.bytes, std::string(CodecName(c)) + ": Convert(wav) equals Encode");
        const auto d1 = Decode(c, first.bytes), d2 = Decode(c, first.bytes);
        Check(d1 && d2 && d1.audio.samples == d2.audio.samples, std::string(CodecName(c)) + ": decode is repeatable");
    }
}

void Reports() {
    const auto source = Render(Material::music, 48000, 2, 1.0);
    const auto wav = audio::EncodeWav(source).bytes;
    for (Container c : {Container::vorbis, Container::opus, Container::mp3}) {
        const auto toLossy = audio::Convert(Container::wav, c, wav);
        Check(bool(toLossy) && toLossy.lossyOutput && !toLossy.generationLoss && !toLossy.report.lossy,
            std::string("wav->") + CodecName(c) + ": labelled lossy output, no generation loss");
        const auto back = audio::Convert(c, Container::wav, toLossy.bytes);
        Check(bool(back) && !back.lossyOutput && back.report.lossy && back.bitsPerSample == 16,
            std::string(CodecName(c)) + "->wav: lossy source reported, 16-bit");
        Check(back.report.droppedComments == 0 && back.report.skippedStreams == 0, std::string(CodecName(c)) + ": clean report");
        for (Container d : {Container::vorbis, Container::opus, Container::mp3}) {
            const auto again = audio::Convert(c, d, toLossy.bytes);
            Check(bool(again) && again.lossyOutput && again.generationLoss,
                std::string(CodecName(c)) + "->" + CodecName(d) + ": generation loss reported " + Name(again.encodeError));
        }
        for (Container d : {Container::flac, Container::aiff}) {
            const auto lossless = audio::Convert(c, d, toLossy.bytes);
            Check(bool(lossless) && !lossless.lossyOutput && lossless.report.lossy, std::string(CodecName(c)) + "->lossless");
        }
    }
    // Flac -> mp3 and conversions of 24-bit sources.
    const auto hi = Render(Material::music, 44100, 2, 1.0, 24);
    const auto flacToMp3 = audio::Convert(Container::flac, Container::mp3, audio::EncodeFlac(hi).bytes);
    Check(bool(flacToMp3) && flacToMp3.lossyOutput, "flac(24-bit)->mp3");
    const auto mp3Back = audio::DecodeMp3(flacToMp3.bytes);
    Check(bool(mp3Back) && mp3Back.audio.frames == hi.frames && Snr(hi, mp3Back.audio) > 15, "flac->mp3 decodes to the source length");
    // A 44.1 kHz source cannot become Opus (no resampling), nor 96 kHz MP3.
    ExpectError(audio::Convert(Container::flac, Container::opus, audio::EncodeFlac(hi).bytes).encodeError, Error::resampling_required,
        "flac 44.1 kHz -> opus");
    // Comments are counted, never carried: add tags to a Vorbis stream via FFmpeg in interop;
    // here: an OpusTags with two comments.
    auto pages = Pages(audio::EncodeOpus(source).bytes);
    Bytes tags = pages[1].body;
    tags.resize(tags.size() - 4);
    const char* comments[] = {"TITLE=x", "METADATA_BLOCK_PICTURE=AAAA"};
    const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) tags.push_back(std::byte((v >> (8 * i)) & 0xFF)); };
    put32(2);
    for (auto* text : comments) {
        put32(std::uint32_t(std::strlen(text)));
        for (const char* p = text; *p; ++p) tags.push_back(std::byte(*p));
    }
    SetSinglePacket(pages[1], tags);
    const auto tagged = audio::DecodeOpus(Join(pages));
    Check(bool(tagged) && tagged.report.droppedComments == 2, "OpusTags comments counted as dropped: " + Name(tagged.issue.code));
}

void OggStructure() {
    const auto source = Render(Material::music, 48000, 2, 1.0);
    const auto vorbis = audio::EncodeVorbis(source).bytes;
    const auto opus = audio::EncodeOpus(source).bytes;
    const auto vorbisRef = audio::DecodeVorbis(vorbis);
    const auto opusRef = audio::DecodeOpus(opus);
    Check(vorbisRef && opusRef, "references decode");
    // Wrong codec and non-Ogg input.
    ExpectError(audio::DecodeVorbis(opus).issue.code, Error::wrong_magic, "Opus decoded as Vorbis");
    ExpectError(audio::DecodeOpus(vorbis).issue.code, Error::wrong_magic, "Vorbis decoded as Opus");
    ExpectError(audio::DecodeOpus(audio::EncodeWav(source).bytes).issue.code, Error::wrong_magic, "WAV decoded as Opus");
    ExpectError(audio::DecodeMp3(audio::EncodeWav(source).bytes).issue.code, Error::wrong_magic, "WAV decoded as MP3");
    ExpectError(audio::DecodeFlac(opus).issue.code, Error::unsupported_container, "Ogg decoded as FLAC");
    // Chained: a second link after the first ended.
    Bytes chained = vorbis;
    chained.insert(chained.end(), vorbis.begin(), vorbis.end());
    ExpectError(audio::DecodeVorbis(chained).issue.code, Error::chained_stream, "chained Vorbis");
    Bytes chainedOpus = opus;
    chainedOpus.insert(chainedOpus.end(), opus.begin(), opus.end());
    ExpectError(audio::DecodeOpus(chainedOpus).issue.code, Error::chained_stream, "chained Opus");
    // Multiplexed (grouped) Vorbis + Opus: each decoder selects its stream and reports the other.
    {
        const auto v = Pages(vorbis), o = Pages(opus);
        std::vector<Page> grouped = {v[0], o[0]};
        for (std::size_t i = 1; i < std::max(v.size(), o.size()); ++i) {
            if (i < v.size()) grouped.push_back(v[i]);
            if (i < o.size()) grouped.push_back(o[i]);
        }
        const auto muxed = Join(grouped);
        const auto dv = audio::DecodeVorbis(muxed), dop = audio::DecodeOpus(muxed);
        Check(dv && dv.report.skippedStreams == 1 && dv.audio.samples == vorbisRef.audio.samples, "grouped: Vorbis selected, Opus skipped");
        Check(dop && dop.report.skippedStreams == 1 && dop.audio.samples == opusRef.audio.samples, "grouped: Opus selected, Vorbis skipped");
        // BOS after data pages of a still-open stream is a layout error.
        std::vector<Page> late = {v[0], v[1], o[0]};
        late.insert(late.end(), v.begin() + 2, v.end());
        ExpectError(audio::DecodeVorbis(Join(late)).issue.code, Error::invalid_layout, "BOS after data");
    }
    // Missing EOS, missing page (sequence gap), duplicate serial, granule edits.
    {
        auto p = Pages(opus);
        auto noEos = p;
        noEos.back().header[5] = std::byte(At(noEos.back().header, 5) & ~4u);
        ExpectError(audio::DecodeOpus(Join(noEos)).issue.code, Error::truncated, "Opus without EOS");
        if (p.size() > 4) {
            auto gap = p;
            gap.erase(gap.begin() + 3);
            ExpectError(audio::DecodeOpus(Join(gap)).issue.code, Error::corrupt_stream, "Opus page missing");
        }
        auto shorter = p;
        SetGranule(shorter.back(), Granule(shorter.back()) - 100);
        const auto s = audio::DecodeOpus(Join(shorter));
        Check(s && s.audio.frames == opusRef.audio.frames - 100 && s.report.trimmedTrailingFrames == opusRef.report.trimmedTrailingFrames + 100,
            "Opus final granule trims the end");
        auto longer = p;
        SetGranule(longer.back(), Granule(longer.back()) + 5000);
        ExpectError(audio::DecodeOpus(Join(longer)).issue.code, Error::length_mismatch, "Opus granule beyond decoded audio");
        auto foreign = p;
        SetSerial(foreign[2], 77);
        ExpectError(audio::DecodeOpus(Join(foreign)).issue.code, Error::invalid_layout, "page of an unknown serial");
        auto vp = Pages(vorbis);
        auto vshort = vp;
        SetGranule(vshort.back(), Granule(vshort.back()) - 300);
        const auto vs = audio::DecodeVorbis(Join(vshort));
        Check(vs && vs.audio.frames == vorbisRef.audio.frames - 300 && vs.report.encoderDelayTrimmed, "Vorbis final granule trims the end");
        // Every audio granule 300 lower (first audio page not the last): the stream
        // starts 300 frames before zero, so those 300 leading frames are trimmed.
        auto vstart = vp;
        std::size_t audioPages = 0;
        for (std::size_t i = 2; i < vstart.size(); ++i)
            if (Granule(vstart[i]) > 0) { SetGranule(vstart[i], Granule(vstart[i]) - 300); ++audioPages; }
        const auto vst = audio::DecodeVorbis(Join(vstart));
        Check(audioPages > 1 && vst && vst.audio.frames == vorbisRef.audio.frames - 300 && vst.report.trimmedLeadingFrames == 300
            && std::equal(vst.audio.samples.begin(), vst.audio.samples.end(), vorbisRef.audio.samples.begin() + 300 * 2),
            "Vorbis negative start granule trims the start");
    }
    // OpusHead: pre-skip, gain, version, mapping family.
    {
        const auto head = Pages(opus)[0].body;
        const auto withHead = [&](const Bytes& body) { auto p = Pages(opus); SetSinglePacket(p[0], body); return Join(p); };
        Bytes h = head;
        const std::uint32_t preSkip = At(h, 10) | (At(h, 11) << 8);
        h[10] = std::byte((preSkip + 480) & 0xFF); h[11] = std::byte((preSkip + 480) >> 8);
        const auto skipped = audio::DecodeOpus(withHead(h));
        Check(skipped && skipped.audio.frames == opusRef.audio.frames - 480 && skipped.report.trimmedLeadingFrames == preSkip + 480,
            "larger pre-skip removes more leading frames");
        Check(skipped && std::equal(skipped.audio.samples.begin(), skipped.audio.samples.end(),
            opusRef.audio.samples.begin() + 480 * 2), "pre-skip shifts the same decoded samples");
        h = head;
        const std::int16_t gain = -1541; // -6.02 dB in Q7.8
        h[16] = std::byte(std::uint16_t(gain) & 0xFF); h[17] = std::byte(std::uint16_t(gain) >> 8);
        const auto quiet = audio::DecodeOpus(withHead(h));
        double a = 0, b = 0;
        for (std::size_t i = 0; quiet && i < quiet.audio.samples.size(); ++i) {
            a += double(opusRef.audio.samples[i]) * opusRef.audio.samples[i];
            b += double(quiet.audio.samples[i]) * quiet.audio.samples[i];
        }
        Check(quiet && std::abs(std::sqrt(b / a) - 0.5) < 0.01, "output gain -6.02 dB halves the amplitude: " + std::to_string(std::sqrt(b / std::max(a, 1.0))));
        h = head; h[8] = std::byte{0x10};
        ExpectError(audio::DecodeOpus(withHead(h)).issue.code, Error::invalid_header, "OpusHead major version 1");
        h = head; h[18] = std::byte{255};
        h.insert(h.end(), {std::byte{1}, std::byte{1}, std::byte{0}, std::byte{1}});
        ExpectError(audio::DecodeOpus(withHead(h)).issue.code, Error::unsupported_channel_mapping, "mapping family 255");
        h = head; h[18] = std::byte{2};
        h.insert(h.end(), {std::byte{1}, std::byte{1}, std::byte{0}, std::byte{1}});
        ExpectError(audio::DecodeOpus(withHead(h)).issue.code, Error::unsupported_channel_mapping, "mapping family 2");
        // Family 1 with an explicit stereo table decodes identically.
        h = head; h[18] = std::byte{1};
        h.insert(h.end(), {std::byte{1}, std::byte{1}, std::byte{0}, std::byte{1}});
        const auto family1 = audio::DecodeOpus(withHead(h));
        Check(family1 && family1.audio.samples == opusRef.audio.samples, "mapping family 1 stereo equals family 0");
        h[21] = std::byte{1}; h[22] = std::byte{0}; // swapped table swaps channels
        const auto swapped = audio::DecodeOpus(withHead(h));
        Check(swapped && swapped.audio.samples[1] == opusRef.audio.samples[0] && swapped.audio.samples[2001] == opusRef.audio.samples[2000],
            "mapping table order is honoured");
        h = head; h[18] = std::byte{1};
        h.insert(h.end(), {std::byte{1}, std::byte{1}, std::byte{0}, std::byte{9}});
        ExpectError(audio::DecodeOpus(withHead(h)).issue.code, Error::unsupported_channel_mapping, "mapping index out of range");
        h = head; h[9] = std::byte{3};
        ExpectError(audio::DecodeOpus(withHead(h)).issue.code, Error::invalid_header, "family 0 with 3 channels");
    }
}

void Malformed() {
    const auto source = Render(Material::music, 48000, 2, 0.5);
    for (Container c : {Container::vorbis, Container::opus, Container::mp3}) {
        const auto good = Encode(c, source).bytes;
        const auto reference = Decode(c, good);
        Check(bool(reference), std::string(CodecName(c)) + ": reference");
        // Every strict prefix is rejected (Ogg: EOS/partial page; MP3: partial
        // frame or Xing frame count). Stride keeps the sweep fast.
        for (std::size_t n = 0; n < good.size(); n += (n < 4096 ? 1 : 7)) {
            const auto d = Decode(c, Bytes(good.begin(), good.begin() + std::ptrdiff_t(n)));
            if (d) Check(false, std::string(CodecName(c)) + ": prefix of " + std::to_string(n) + " bytes accepted");
            if (!d) Check(d.audio.samples.empty() && d.audio.frames == 0, std::string(CodecName(c)) + ": failure leaks samples");
        }
        // Single-byte corruption sweep: Ogg's page CRC rejects every change; MP3
        // has no mandatory checksum, so a change may decode, but only to canonical
        // audio of the declared length.
        std::size_t decodedCorrupt = 0;
        for (std::size_t i = 0; i < good.size(); i += 13) {
            Bytes bad = good;
            bad[i] = std::byte(std::to_integer<std::uint8_t>(bad[i]) ^ (1u << (i % 8)));
            const auto d = Decode(c, bad);
            if (!d) { Check(d.audio.samples.empty(), "corrupt failure leaks samples"); continue; }
            ++decodedCorrupt;
            Check(c == Container::mp3, std::string(CodecName(c)) + ": corruption at " + std::to_string(i) + " accepted");
            Check(audio::ValidateAudio(d.audio) == Error::none, "corrupt decode is canonical");
        }
        if (verbose) std::cout << CodecName(c) << ": corrupt variants decoded " << decodedCorrupt << '\n';
        // Trailing garbage.
        Bytes trailing = good;
        trailing.push_back(std::byte{0x55});
        Check(!Decode(c, trailing), std::string(CodecName(c)) + ": trailing byte rejected");
    }
    // MP3 specifics.
    const auto mp3 = audio::EncodeMp3(source).bytes;
    const auto ref = audio::DecodeMp3(mp3);
    {
        Bytes free = mp3;
        free[2] = std::byte(std::to_integer<std::uint8_t>(free[2]) & 0x0F); // bitrate index 0
        ExpectError(audio::DecodeMp3(free).issue.code, Error::unsupported_encoding, "free-format MP3");
        Bytes layer2 = mp3;
        layer2[1] = std::byte((std::to_integer<std::uint8_t>(layer2[1]) & ~6u) | 4u); // layer II
        ExpectError(audio::DecodeMp3(layer2).issue.code, Error::unsupported_encoding, "Layer II");
        // ID3v2 (with a picture frame) + APEv2 + ID3v1 are skipped and counted.
        Bytes id3 = {std::byte{'I'}, std::byte{'D'}, std::byte{'3'}, std::byte{4}, std::byte{0}, std::byte{0},
                     std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}};
        id3.resize(10 + 128, std::byte{0});
        const char apic[] = "APIC";
        std::memcpy(id3.data() + 10, apic, 4);
        Bytes tagged = id3;
        tagged.insert(tagged.end(), mp3.begin(), mp3.end());
        Bytes ape(32 + 16, std::byte{0});
        std::memcpy(ape.data() + 16, "APETAGEX", 8);
        ape[16 + 8] = std::byte{0xD0}; ape[16 + 9] = std::byte{0x07};
        ape[16 + 12] = std::byte{48}; // size incl. footer, no header flag
        tagged.insert(tagged.end(), ape.begin(), ape.end());
        Bytes v1(128, std::byte{0});
        v1[0] = std::byte{'T'}; v1[1] = std::byte{'A'}; v1[2] = std::byte{'G'};
        tagged.insert(tagged.end(), v1.begin(), v1.end());
        const auto t = audio::DecodeMp3(tagged);
        Check(t && t.report.skippedTags == 3 && t.audio.samples == ref.audio.samples, "ID3v2/APEv2/ID3v1 skipped and counted: " + Name(t.issue.code));
        Bytes badSize = id3;
        badSize[8] = std::byte{0x81};
        badSize.insert(badSize.end(), mp3.begin(), mp3.end());
        ExpectError(audio::DecodeMp3(badSize).issue.code, Error::invalid_metadata, "non-syncsafe ID3v2 size");
        // Xing frame count disagreeing with the stream.
        Bytes miscount = mp3;
        const std::size_t xing = 4 + 32 + 8; // MPEG-1 stereo: frames field after "Xing"/"Info" + flags
        miscount[xing + 3] = std::byte(std::to_integer<std::uint8_t>(miscount[xing + 3]) ^ 1);
        ExpectError(audio::DecodeMp3(miscount).issue.code, Error::length_mismatch, "Xing frame count mismatch");
        // Sample-rate change mid-stream.
        Bytes changed = mp3;
        const std::size_t second = 144000 * 192 / 48000; // first frame size at 48 kHz, 192 kbps
        changed[second + 2] = std::byte((std::to_integer<std::uint8_t>(changed[second + 2]) & ~0x0Cu) | 0x00u);
        Check(!audio::DecodeMp3(changed), "MP3 parameter change rejected");
    }
}

// MP3 CBR floor: a legal bitrate whose frame cannot hold the Info/LAME tag is rejected before
// encoding with bitrate_too_low_for_gapless_tag (never encoded without the tag); the lowest
// bitrate that fits encodes with the tag and decodes to the exact length. Every legal bitrate
// is checked on both sides of the boundary for all nine rates and both channel counts.
void Mp3GaplessFloor() {
    struct Floor { std::uint32_t rate, mono, stereo; };
    const Floor floors[] = {{48000, 64, 64}, {44100, 56, 64}, {32000, 40, 48}, {24000, 64, 64}, {22050, 56, 56},
                            {16000, 40, 40}, {12000, 32, 32}, {11025, 32, 32}, {8000, 24, 24}};
    const std::uint32_t mpeg1[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    const std::uint32_t mpeg2[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    Check(Name(Error::bitrate_too_low_for_gapless_tag).find("minimum CBR kbps") != std::string::npos
        && Name(Error::bitrate_too_low_for_gapless_tag).find("44.1 kHz 56/64") != std::string::npos, "floor message names the minimums");
    for (const auto& f : floors) {
        for (std::uint32_t channels : {1u, 2u}) {
            const std::uint32_t minimum = channels == 1 ? f.mono : f.stereo;
            const auto where = std::to_string(f.rate) + " Hz " + std::to_string(channels) + " ch";
            Check(audio::Mp3MinimumCbrBitrateKbps(f.rate, channels) == minimum, where + ": Mp3MinimumCbrBitrateKbps");
            const auto source = Render(Material::music, f.rate, channels, 0.25);
            const auto& legal = f.rate >= 32000 ? mpeg1 : mpeg2;
            std::uint32_t highestRejected = 0;
            for (const auto kbps : legal) {
                audio::LossyOptions o; o.mp3BitrateKbps = kbps;
                const auto out = audio::EncodeMp3(source, o);
                if (kbps < minimum) {
                    ExpectError(out.error, Error::bitrate_too_low_for_gapless_tag, where + " " + std::to_string(kbps) + " kbps");
                    Check(out.bytes.empty(), where + ": rejected encode leaks bytes");
                    highestRejected = kbps;
                } else {
                    ExpectError(out.error, Error::none, where + " " + std::to_string(kbps) + " kbps");
                    const auto d = audio::DecodeMp3(out.bytes);
                    Check(d && d.audio.frames == source.frames && d.report.encoderDelayTrimmed && !d.report.encoderDelayUnknown,
                        where + " " + std::to_string(kbps) + " kbps: gapless tag missing or length changed");
                }
            }
            if (verbose)
                std::cout << where << ": highest rejected " << highestRejected << " kbps, lowest accepted " << minimum << " kbps\n";
            Check(highestRejected != 0, where + ": the boundary has a failing side");
            // Through Convert too, and VBR is unaffected.
            audio::LossyOptions below; below.mp3BitrateKbps = highestRejected;
            ExpectError(audio::Convert(Container::wav, Container::mp3, audio::EncodeWav(source).bytes, below).encodeError,
                Error::bitrate_too_low_for_gapless_tag, where + " via Convert");
            audio::LossyOptions vbr; vbr.mp3RateControl = audio::Mp3RateControl::vbr; vbr.mp3VbrQuality = 9;
            ExpectError(audio::EncodeMp3(source, vbr).error, Error::none, where + " VBR 9");
        }
    }
    Check(audio::Mp3MinimumCbrBitrateKbps(96000, 2) == 0 && audio::Mp3MinimumCbrBitrateKbps(48000, 3) == 0, "non-MP3 rate or channel count");
    // An illegal bitrate stays invalid_option, decided before the floor.
    audio::LossyOptions illegal; illegal.mp3BitrateKbps = 20;
    ExpectError(audio::EncodeMp3(Render(Material::music, 48000, 2, 0.1), illegal).error, Error::invalid_option, "illegal bitrate");
}
void Limits() {
    const auto source = Render(Material::music, 48000, 2, 1.0);
    for (Container c : {Container::vorbis, Container::opus, Container::mp3}) {
        const auto name = std::string(CodecName(c));
        const auto good = Encode(c, source).bytes;
        audio::Limits l;
        l.maximumSourceBytes = good.size() - 1;
        ExpectError(Decode(c, good, l).issue.code, Error::input_too_large, name + " source limit");
        l = {};
        l.maximumTotalSamples = 2 * 48000 - 2;
        ExpectError(Decode(c, good, l).issue.code, Error::sample_limit, name + " sample limit on decode");
        l = {};
        l.maximumChannels = 1;
        ExpectError(Decode(c, good, l).issue.code, Error::channel_limit, name + " channel limit on decode");
        l = {};
        l.maximumOutputBytes = good.size() - 1;
        const auto limited = Encode(c, source, {}, l);
        ExpectError(limited.error, Error::output_limit, name + " output limit");
        Check(limited.bytes.empty(), name + " output limit leaks bytes");
        l.maximumOutputBytes = good.size();
        Check(bool(Encode(c, source, {}, l)), name + " exact output limit accepted");
        auto three = Render(Material::music, 48000, 3, 0.1);
        ExpectError(Encode(c, three).error, Error::channel_limit, name + " 3-channel encode");
        auto bad = source;
        bad.samples[5] = 40000; // outside 16 bits
        ExpectError(Encode(c, bad).error, Error::invalid_audio, name + " invalid canonical audio");
        if (c != Container::mp3) {
            l = {};
            l.maximumMetadataBlockBytes = 16;
            ExpectError(Decode(c, good, l).issue.code, Error::metadata_limit, name + " header packet limit");
        }
    }
    // Options.
    audio::LossyOptions o;
    o.vorbisQuality = 101;
    ExpectError(audio::EncodeVorbis(source, o).error, Error::invalid_option, "vorbis quality 101");
    o = {}; o.opusBitrateKbps = 5;
    ExpectError(audio::EncodeOpus(source, o).error, Error::invalid_option, "opus 5 kbps");
    o = {}; o.opusBitrateKbps = 511;
    ExpectError(audio::EncodeOpus(source, o).error, Error::invalid_option, "opus 511 kbps");
    o = {}; o.mp3BitrateKbps = 144; // MPEG-2 only
    ExpectError(audio::EncodeMp3(source, o).error, Error::invalid_option, "mp3 144 kbps at 48 kHz");
    o = {}; o.mp3RateControl = audio::Mp3RateControl::vbr; o.mp3VbrQuality = 10;
    ExpectError(audio::EncodeMp3(source, o).error, Error::invalid_option, "mp3 V10");
    o = {}; o.mp3RateControl = audio::Mp3RateControl(7);
    ExpectError(audio::EncodeMp3(source, o).error, Error::invalid_option, "mp3 unknown rate control");
    // Rates.
    for (std::uint32_t rate : {44100u, 32000u, 96000u, 11025u}) {
        ExpectError(audio::EncodeOpus(Render(Material::music, rate, 1, 0.1)).error, Error::resampling_required,
            "opus at " + std::to_string(rate));
    }
    for (std::uint32_t rate : {96000u, 88200u, 7999u, 64000u})
        ExpectError(audio::EncodeMp3(Render(Material::music, rate, 1, 0.1)).error, Error::resampling_required, "mp3 at " + std::to_string(rate));
    for (std::uint32_t rate : {8000u, 11025u, 12000u, 16000u, 22050u, 24000u, 32000u, 44100u, 48000u}) {
        audio::LossyOptions mp3;
        mp3.mp3BitrateKbps = rate >= 32000 ? 128 : 64;
        const auto a = Render(Material::tone440, rate, 2, 0.5);
        const auto d = audio::DecodeMp3(audio::EncodeMp3(a, mp3).bytes);
        Check(d && d.audio.sampleRate == rate && d.audio.frames == a.frames, "mp3 round trip at " + std::to_string(rate) + " Hz");
        const auto v = audio::DecodeVorbis(audio::EncodeVorbis(a).bytes);
        Check(v && v.audio.sampleRate == rate && v.audio.frames == a.frames, "vorbis round trip at " + std::to_string(rate) + " Hz");
    }
    for (std::uint32_t rate : {8000u, 12000u, 16000u, 24000u, 48000u}) {
        const auto a = Render(Material::tone440, rate, 1, 0.5);
        const auto d = audio::DecodeOpus(audio::EncodeOpus(a).bytes);
        Check(d && d.audio.frames == a.frames * (48000 / rate) && d.report.originalSampleRate == rate, "opus round trip at " + std::to_string(rate));
    }
}

// ---- interop support (tests/interop.cmake) ----
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
Bytes Raw16(const audio::Audio& a) {
    Bytes out;
    for (auto s : a.samples) { out.push_back(std::byte(s & 0xFF)); out.push_back(std::byte((s >> 8) & 0xFF)); }
    return out;
}
audio::Audio FromRaw16(const Bytes& raw, std::uint32_t rate, std::uint32_t channels) {
    audio::Audio a;
    a.sampleRate = rate; a.channels = channels; a.bitsPerSample = 16;
    a.frames = raw.size() / 2 / channels;
    a.samples.resize(a.frames * channels);
    for (std::size_t i = 0; i < a.samples.size(); ++i)
        a.samples[i] = std::int16_t(std::uint16_t(At(raw, 2 * i) | (At(raw, 2 * i + 1) << 8)));
    return a;
}
// Six channels in WAVE 5.1 order (FL FR FC LFE BL BR), each its own rising
// chirp (non-periodic, so the alignment lag is unambiguous).
audio::Audio Surround(std::uint32_t rate, double seconds) {
    audio::Audio a;
    a.sampleRate = rate; a.channels = 6; a.bitsPerSample = 16;
    a.frames = std::uint64_t(seconds * rate);
    a.samples.resize(a.frames * 6);
    const double start[6] = {300, 500, 800, 40, 1300, 2000};
    for (std::uint64_t f = 0; f < a.frames; ++f) {
        const double t = double(f) / rate;
        for (int c = 0; c < 6; ++c) {
            // Instantaneous frequency start * (1 + t / 2): phase = 2 pi start (t + t^2 / 4).
            const double phase = 2 * pi * start[c] * (t + t * t / 4);
            a.samples[f * 6 + std::size_t(c)] = std::int32_t(std::lround(8000 * std::sin(phase)));
        }
    }
    return a;
}
int Emit(const std::string& dir) {
    bool ok = true;
    for (std::uint32_t channels : {1u, 2u}) {
        const auto ch = std::to_string(channels);
        const auto p44 = Render(Material::programme, 44100, channels, 4.0);
        const auto p48 = Render(Material::programme, 48000, channels, 4.0);
        ok &= WriteFile(dir + "/prog44_" + ch + ".raw", Raw16(p44));
        ok &= WriteFile(dir + "/prog48_" + ch + ".raw", Raw16(p48));
        ok &= WriteFile(dir + "/arken_" + ch + ".ogg", audio::EncodeVorbis(p44).bytes);
        ok &= WriteFile(dir + "/arken_" + ch + ".opus", audio::EncodeOpus(p48).bytes);
        ok &= WriteFile(dir + "/arken_" + ch + ".mp3", audio::EncodeMp3(p44).bytes);
        audio::LossyOptions vbr;
        vbr.mp3RateControl = audio::Mp3RateControl::vbr;
        ok &= WriteFile(dir + "/arken_vbr_" + ch + ".mp3", audio::EncodeMp3(p44, vbr).bytes);
    }
    ok &= WriteFile(dir + "/surround48.raw", Raw16(Surround(48000, 1.0)));
    return ok ? 0 : 1;
}
// Compares 16-bit raw PCM (e.g. FFmpeg's decode) against a raw reference:
// frame count within `tolerance` frames, lag 0, SNR >= floor.
int CompareRaw(const std::string& refPath, const std::string& testPath, std::uint32_t rate, std::uint32_t channels,
    double floorDb, long tolerance) {
    const auto ref = FromRaw16(ReadFile(refPath), rate, channels);
    const auto test = FromRaw16(ReadFile(testPath), rate, channels);
    const long delta = long(test.frames) - long(ref.frames);
    const long lag = BestLag(ref, test, 2000, rate / 4, 8192);
    const double snr = Snr(ref, test);
    std::cout << testPath << ": " << test.frames << " frames (reference " << ref.frames << ", delta " << delta << "), lag "
              << lag << ", SNR " << std::fixed << std::setprecision(2) << snr << " dB\n";
    return (std::abs(delta) <= tolerance && lag == 0 && snr >= floorDb) ? 0 : 1;
}
// Decodes an independent encoder's file with Arken and checks it against the raw source.
int DecodeAndCompare(const std::string& codec, const std::string& file, const std::string& refPath, std::uint32_t refRate,
    std::uint32_t channels, double floorDb, long tolerance) {
    const Container c = codec == "vorbis" ? Container::vorbis : codec == "opus" ? Container::opus : Container::mp3;
    const auto d = Decode(c, ReadFile(file));
    if (!d) { std::cerr << file << ": " << Name(d.issue.code) << '\n'; return 1; }
    const auto ref = FromRaw16(ReadFile(refPath), refRate, channels);
    if (d.audio.channels != channels || d.audio.sampleRate != refRate) {
        std::cerr << file << ": decoded " << d.audio.sampleRate << " Hz " << d.audio.channels << " ch\n";
        return 1;
    }
    const long delta = long(d.audio.frames) - long(ref.frames);
    const long lag = BestLag(ref, d.audio, 2000, refRate / 4, 8192);
    bool ok = std::abs(delta) <= tolerance && lag == 0;
    std::cout << file << ": Arken decodes " << d.audio.frames << " frames (reference " << ref.frames << ", delta " << delta
              << "), lag " << lag << ", trimmed " << d.report.trimmedLeadingFrames << "+" << d.report.trimmedTrailingFrames
              << ", comments " << d.report.droppedComments << ", tags " << d.report.skippedTags;
    if (channels == 6) {
        // Per-channel SNR proves the Vorbis-order -> WAVE-order mapping and the mask.
        ok &= d.audio.channelMask == std::optional<std::uint32_t>(0x3F);
        for (std::uint32_t c6 = 0; c6 < 6; ++c6) {
            audio::Audio r1 = ref, t1 = d.audio;
            r1.channels = t1.channels = 1;
            r1.samples.clear(); t1.samples.clear();
            for (std::uint64_t f = 0; f < ref.frames; ++f) r1.samples.push_back(ref.samples[f * 6 + c6]);
            for (std::uint64_t f = 0; f < d.audio.frames; ++f) t1.samples.push_back(d.audio.samples[f * 6 + c6]);
            r1.frames = ref.frames; t1.frames = d.audio.frames;
            const double s = Snr(r1, t1);
            std::cout << ", ch" << c6 << " " << std::fixed << std::setprecision(1) << s << " dB";
            ok &= s >= floorDb;
        }
    } else {
        const double snr = Snr(ref, d.audio);
        std::cout << ", SNR " << std::fixed << std::setprecision(2) << snr << " dB";
        ok &= snr >= floorDb;
    }
    std::cout << '\n';
    return ok ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--emit") return Emit(argv[2]);
    if (argc == 8 && std::string_view(argv[1]) == "--compare-raw")
        return CompareRaw(argv[2], argv[3], std::uint32_t(std::stoul(argv[4])), std::uint32_t(std::stoul(argv[5])),
            std::stod(argv[6]), std::stol(argv[7]));
    if (argc == 9 && std::string_view(argv[1]) == "--decode-compare")
        return DecodeAndCompare(argv[2], argv[3], argv[4], std::uint32_t(std::stoul(argv[5])), std::uint32_t(std::stoul(argv[6])),
            std::stod(argv[7]), std::stol(argv[8]));
    if (argc == 5 && std::string_view(argv[1]) == "--decode-raw") {
        const std::string_view codec = argv[2];
        const Container c = codec == "vorbis" ? Container::vorbis : codec == "opus" ? Container::opus : Container::mp3;
        const auto d = Decode(c, ReadFile(argv[3]));
        if (!d) { std::cerr << Name(d.issue.code) << '\n'; return 1; }
        std::cout << d.audio.frames << " frames, " << d.audio.sampleRate << " Hz, " << d.audio.channels << " ch, trimmed "
                  << d.report.trimmedLeadingFrames << "+" << d.report.trimmedTrailingFrames << '\n';
        return WriteFile(argv[4], Raw16(d.audio)) ? 0 : 1;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "arken_audio_codecs_v1 lossy (" << audio::VorbisVendorString() << "; " << audio::OpusVersionString()
                  << "; LAME " << audio::LameVersionString() << ")\n";
        return 0;
    }
    const std::pair<std::string_view, std::function<void()>> checks[] = {
        {"Quality", Quality}, {"Gapless", Gapless}, {"Determinism", Determinism}, {"Reports", Reports},
        {"OggStructure", OggStructure}, {"Malformed", Malformed}, {"Limits", Limits}, {"Mp3GaplessFloor", Mp3GaplessFloor}};
    std::string_view selected;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--verbose") verbose = true;
        else if (a == "--check" && i + 1 < argc) selected = argv[++i];
        else { std::cerr << "usage: arken-compressed-audio-tests [--check NAME] [--verbose] | --emit DIR | --compare-raw ... | --decode-compare ...\n"; return 2; }
    }
    bool ran = false;
    for (const auto& [name, run] : checks)
        if (selected.empty() || selected == name) { run(); ran = true; }
    if (!ran) { std::cerr << "unknown check " << selected << '\n'; return 2; }
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "ok\n";
    return 0;
}
