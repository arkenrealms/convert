#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Portable, memory-only audio codecs. Every call takes a complete byte span and
// returns a complete result; nothing opens files, reads stdio, starts threads or
// keeps global state.
//  - Lossless (WAV, FLAC, AIFF): integer PCM only; precision is never reduced and
//    floating-point sources are rejected, never quantized.
//  - Lossy (Vorbis in Ogg, Opus in Ogg, MP3): decoded to 16-bit canonical PCM with
//    container-declared encoder delay/padding trimmed; encoded with explicit quality
//    controls (LossyOptions). Lossy results are labelled (DecodeReport::lossy,
//    ConvertResult::lossyOutput / generationLoss); see NOTES.md for the contract.
namespace arken::audio::v1 {

// Values are stable (cast from command-layer enums). "Ogg" alone is never a
// container choice here: vorbis means Vorbis I in Ogg, opus means Opus in Ogg
// (RFC 7845); mp3 means MPEG-1/2/2.5 Layer III elementary stream.
enum class Container : std::uint32_t { wav = 1, flac = 2, aiff = 3, vorbis = 4, opus = 5, mp3 = 6 };

// Canonical lossless sample model shared by every container.
//  - samples: interleaved frames (frame-major), right-justified, sign-extended
//    signed integers. Every value lies in [-2^(bitsPerSample-1), 2^(bitsPerSample-1)-1].
//    8-bit WAV's (and AIFF-C 'raw ') unsigned storage is mapped by subtracting 128.
//  - bitsPerSample: significant bits (WAV wValidBitsPerSample, FLAC
//    bits-per-sample, AIFF COMM sampleSize), 4..32. The WAV/AIFF container
//    width is derived as the next multiple of 8 and is not a separate degree
//    of freedom.
//  - channelMask: the explicit speaker mask the source declared, preserved
//    bit-for-bit (0 is a legitimate "no assignment" mask). std::nullopt means
//    the source declared none: WAVE_FORMAT_PCM, or FLAC without a
//    WAVEFORMATEXTENSIBLE_CHANNEL_MASK comment (RFC 9639 default order applies),
//    and always for AIFF, which has no mask field. EncodeAiff cannot store a
//    mask and drops it (Convert reports this in droppedChannelMask).
//    Channel order in `samples` is the source order for WAV, FLAC and AIFF;
//    nothing is remapped. The one deliberate mapping: Vorbis and Opus family 1
//    streams with 3..8 channels are reordered from Vorbis order to WAVE order
//    and given the standard WAVE mask (1-2 channels: no mask).
//  - Lossy decoders (Vorbis, Opus, MP3) always produce bitsPerSample = 16.
struct Audio {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t bitsPerSample = 0;
    std::optional<std::uint32_t> channelMask;
    std::uint64_t frames = 0;
    std::vector<std::int32_t> samples;
};

// Explicit resource profile. Each limit is checked before the corresponding
// allocation; exceeding one is a typed rejection, never a truncated result.
// Peak memory of one call is bounded by roughly source + 4 * maximumTotalSamples
// + maximumOutputBytes (plus libFLAC's fixed per-stream state).
struct Limits {
    std::uint64_t maximumSourceBytes = 256ull * 1024 * 1024;
    std::uint64_t maximumOutputBytes = 512ull * 1024 * 1024;
    // frames * channels; 64 Mi samples = 256 MiB of canonical int32 PCM.
    std::uint64_t maximumTotalSamples = 64ull * 1024 * 1024;
    std::uint32_t maximumChannels = 8;
    std::uint32_t minimumSampleRate = 1;
    std::uint32_t maximumSampleRate = 768000;
    // Any single WAV chunk other than "data", any AIFF chunk other than "SSND", and any FLAC metadata block
    // (including an ID3v2 prefix), may not exceed this size.
    std::uint32_t maximumMetadataBlockBytes = 1024 * 1024;
    // Number of RIFF/AIFF chunks, or FLAC metadata blocks, examined in one source.
    std::uint32_t maximumMetadataBlocks = 1024;
};

enum class Error : std::uint32_t {
    none,
    input_too_large,        // source exceeds maximumSourceBytes
    truncated,              // declared structure extends past the end of input
    wrong_magic,            // not RIFF/WAVE, not fLaC, or not FORM/AIFF or FORM/AIFC
    unsupported_container,  // RF64, BW64, Sony Wave64 or big-endian RIFX
    unsupported_encoding,   // compressed or otherwise non-PCM WAV format tag / subformat or AIFF-C compressionType
    unsupported_float,      // IEEE float samples (WAV, AIFF-C fl32/fl64): rejected, never quantized
    unsupported_bit_depth,  // container or significant bits outside the admitted set
    invalid_header,         // internally inconsistent sizes, alignment or rates
    invalid_layout,         // missing, duplicate or misordered chunks/blocks, trailing bytes
    invalid_sample_padding, // WAV/AIFF padding bits below the significant bits are not zero
    channel_limit,          // zero channels, more than maximumChannels, or > 8 for FLAC
    sample_rate_limit,      // outside [minimumSampleRate, maximumSampleRate] or FLAC's range
    sample_limit,           // frames * channels exceeds maximumTotalSamples
    metadata_limit,         // chunk/block size or count limit exceeded
    output_limit,           // encoded result would exceed maximumOutputBytes
    invalid_metadata,       // malformed channel-mask comment or metadata block
    corrupt_stream,         // FLAC frame/CRC/sync error or frame parameters disagree
    checksum_mismatch,      // decoded audio disagrees with the STREAMINFO MD5
    length_mismatch,        // decoded frame count disagrees with STREAMINFO
    invalid_audio,          // canonical Audio is inconsistent (size, range, fields)
    allocation_failed,
    codec_failed,           // libFLAC rejected a configuration or failed internally
    verify_failed,          // libFLAC's encoder self-verification found a mismatch
    unsupported_sample_rate,// AIFF 80-bit extended rate is not an exact positive integer (fraction, negative, NaN, infinity)
    chained_stream,         // Ogg: a further chained link (new BOS pages after every stream of the first link ended)
    unsupported_channel_mapping, // Opus mapping family other than 0/1 (2, 3, 255, ...) or an inadmissible mapping table
    resampling_required,    // lossy encoder: sample rate is not one the format codes natively; no resampling is performed
    invalid_option          // lossy encoder: quality/bitrate control outside the documented range
};

struct Issue {
    Error code = Error::none;
    std::uint64_t byteOffset = 0; // best-effort source offset; 0 when not attributable
};

// What a decode deliberately did not carry into the canonical model. Callers
// must not describe a conversion as metadata-preserving.
struct DecodeReport {
    std::uint32_t skippedChunks = 0;          // WAV chunks other than fmt/data (LIST, fact, bext, ...);
                                              // AIFF chunks other than COMM/SSND (FVER, MARK, INST, COMT, NAME, ...)
    std::uint32_t ignoredMetadataBlocks = 0;  // FLAC blocks other than STREAMINFO (tags, pictures, ...)
    bool ignoredComments = false;             // FLAC comments other than the channel mask
    // Lossy sources (Vorbis, Opus, MP3).
    bool lossy = false;                       // the source codec is lossy: decoded PCM is not the original signal
    std::uint32_t droppedComments = 0;        // Vorbis comment / OpusTags entries (incl. METADATA_BLOCK_PICTURE art), not carried
    std::uint32_t skippedTags = 0;            // MP3 ID3v2, ID3v1 and APEv2 tags skipped, not carried
    std::uint32_t skippedStreams = 0;         // other Ogg logical streams multiplexed beside the decoded one
    // Leading encoder delay (Opus pre-skip, MP3 LAME-tag delay + 529 decoder delay,
    // Vorbis negative start granule) and trailing padding (final granule position,
    // LAME-tag padding) were removed using container data.
    bool encoderDelayTrimmed = false;
    // MP3 without a usable LAME/Lavc gapless tag: nothing was trimmed, so the output
    // still begins with the encoder + decoder delay and ends with padding.
    bool encoderDelayUnknown = false;
    std::uint64_t trimmedLeadingFrames = 0;   // frames removed at the start (container data)
    std::uint64_t trimmedTrailingFrames = 0;  // frames removed at the end (container data)
    std::uint32_t silencedFrames = 0;         // MP3 leading frames whose bit reservoir precedes the stream: emitted as silence
    std::uint32_t originalSampleRate = 0;     // Opus: OpusHead input sample rate (informational; output is always 48 kHz)
};

enum class Mp3RateControl : std::uint32_t { cbr = 1, vbr = 2 };

// Explicit quality controls of the lossy encoders. Out-of-range values are
// Error::invalid_option, never clamped. Every encoder accepts 1 or 2 channels.
struct LossyOptions {
    // Vorbis: libvorbis VBR quality q = -0.1 + 1.1 * vorbisQuality / 100, i.e.
    // 0 -> -0.1 (lowest, ~45 kbps stereo 44.1 kHz), 50 -> 0.45, 100 -> 1.0 (~500 kbps).
    std::uint32_t vorbisQuality = 50;          // 0..100
    // Opus: target bitrate of the whole stream, unconstrained VBR, complexity 10,
    // application AUDIO, 20 ms frames.
    std::uint32_t opusBitrateKbps = 128;       // 6..510
    // MP3 (LAME): CBR at mp3BitrateKbps (a legal Layer III bitrate for the MPEG
    // version implied by the sample rate) or VBR (new/mtrh) at quality mp3VbrQuality
    // (LAME -V: 0 best .. 9 smallest). Joint stereo for 2 channels, mono for 1.
    Mp3RateControl mp3RateControl = Mp3RateControl::cbr;
    std::uint32_t mp3BitrateKbps = 192;
    std::uint32_t mp3VbrQuality = 2;           // 0..9
};

struct DecodeResult {
    Issue issue;
    Audio audio;
    DecodeReport report;
    explicit operator bool() const noexcept { return issue.code == Error::none; }
};

struct EncodeResult {
    Error error = Error::none;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};

struct ConvertResult {
    Issue decodeIssue;
    Error encodeError = Error::none;
    std::uint32_t sampleRate = 0, channels = 0, bitsPerSample = 0;
    std::uint64_t frames = 0;
    DecodeReport report;
    // The source declared a channel mask that the destination (AIFF) cannot store.
    bool droppedChannelMask = false;
    // The destination is a lossy encoder (Vorbis, Opus, MP3): the output is an
    // approximation, never a reversible conversion.
    bool lossyOutput = false;
    // Both source and destination are lossy: a second generation of coding loss
    // applies on top of the source's; quality is never restored.
    bool generationLoss = false;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept {
        return decodeIssue.code == Error::none && encodeError == Error::none;
    }
};

// RIFF/WAVE, WAVE_FORMAT_PCM or WAVE_FORMAT_EXTENSIBLE with the PCM subformat;
// 8/16/24/32-bit containers. Unknown chunks are skipped within limits.
[[nodiscard]] DecodeResult DecodeWav(std::span<const std::byte>, const Limits& = {}) noexcept;
// Native FLAC (not Ogg FLAC), decoded with libFLAC's stream callbacks over memory.
// Every frame CRC is checked; a non-zero STREAMINFO MD5 must match.
[[nodiscard]] DecodeResult DecodeFlac(std::span<const std::byte>, const Limits& = {}) noexcept;
// WAVE_FORMAT_PCM (16-byte fmt) when no mask is declared and bitsPerSample is
// 8/16/24/32; WAVE_FORMAT_EXTENSIBLE otherwise. Only fmt and data chunks are written.
[[nodiscard]] EncodeResult EncodeWav(const Audio&, const Limits& = {}) noexcept;
// Deterministic FLAC: compression level 5, encoder verification on, MD5 on,
// STREAMINFO + one VORBIS_COMMENT (libFLAC vendor string, plus the channel
// mask comment when declared), no SEEKTABLE, no PADDING, no other metadata.
[[nodiscard]] EncodeResult EncodeFlac(const Audio&, const Limits& = {}) noexcept;
// FORM/AIFF, or FORM/AIFC with compressionType NONE/'twos' (big-endian),
// 'sowt' (little-endian) or 'raw ' (unsigned 8-bit). sampleSize 4..32 in
// 1..4-byte left-justified containers; padding bits must be zero. The 80-bit
// sample rate must be an exact integer. COMM and SSND in any order; other
// chunks skipped within limits. channelMask is always std::nullopt.
[[nodiscard]] DecodeResult DecodeAiff(std::span<const std::byte>, const Limits& = {}) noexcept;
// Deterministic FORM/AIFF: COMM (18 bytes, exact extended rate, real
// sampleSize) and SSND (offset 0, blockSize 0, big-endian left-justified
// samples, even padding) only. Any channelMask is not representable and dropped.
[[nodiscard]] EncodeResult EncodeAiff(const Audio&, const Limits& = {}) noexcept;
// Ogg Vorbis (Vorbis I in Ogg). One logical stream: the first Vorbis stream of
// the first link; other multiplexed streams are skipped (report.skippedStreams),
// a chained second link is Error::chained_stream. Output: 16-bit, the stream's
// rate; 3..8 channels are mapped from Vorbis order to WAVE order with the
// standard channelMask. Granule positions trim the start and end.
[[nodiscard]] DecodeResult DecodeVorbis(std::span<const std::byte>, const Limits& = {}) noexcept;
// Ogg Opus (RFC 7845), channel mapping family 0 (1-2 ch) or 1 (1-8 ch, mapped to
// WAVE order). Decoded at 48 kHz to 16 bits; pre-skip and output gain applied;
// final granule position trims the end. report.originalSampleRate = OpusHead rate.
[[nodiscard]] DecodeResult DecodeOpus(std::span<const std::byte>, const Limits& = {}) noexcept;
// MPEG-1/2/2.5 Layer III. ID3v2 (start), ID3v1 and APEv2 (end) are skipped and
// counted; every other byte must be consecutive frames of one version, rate and
// mono/stereo kind; protected frames' CRC-16 is checked. A Xing/Info first frame
// is not audio; with a LAME/Lavf/Lavc extension its delay and padding drive
// gapless trimming (start = delay + 529), otherwise nothing is trimmed and
// report.encoderDelayUnknown is set. Free-format and Layer I/II are unsupported_encoding.
[[nodiscard]] DecodeResult DecodeMp3(std::span<const std::byte>, const Limits& = {}) noexcept;
// Deterministic Ogg Vorbis: libvorbisenc VBR at LossyOptions::vorbisQuality, fixed
// serial number, comment header carrying only libvorbis's vendor string.
[[nodiscard]] EncodeResult EncodeVorbis(const Audio&, const LossyOptions& = {}, const Limits& = {}) noexcept;
// Deterministic Ogg Opus: 8/12/16/24/48 kHz input only (other rates are
// resampling_required), fixed serial, OpusHead pre-skip = encoder lookahead,
// final granule = pre-skip + exact input length, OpusTags with the libopus version only.
[[nodiscard]] EncodeResult EncodeOpus(const Audio&, const LossyOptions& = {}, const Limits& = {}) noexcept;
// Deterministic MP3 via LAME: input rate must be an MPEG Layer III rate (8000,
// 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000), no resampling, a
// LAME Info/Xing tag frame with encoder delay and padding, no ID3 tags.
[[nodiscard]] EncodeResult EncodeMp3(const Audio&, const LossyOptions& = {}, const Limits& = {}) noexcept;
// Decode with `source`, re-encode with `destination` under the same limits.
[[nodiscard]] ConvertResult Convert(Container source, Container destination,
    std::span<const std::byte>, const Limits& = {}) noexcept;
[[nodiscard]] ConvertResult Convert(Container source, Container destination,
    std::span<const std::byte>, const LossyOptions&, const Limits& = {}) noexcept;
[[nodiscard]] bool IsLossy(Container) noexcept;

[[nodiscard]] Error ValidateAudio(const Audio&, const Limits& = {}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
// Exact libFLAC identity compiled into this build, e.g. "reference libFLAC 1.5.0 20250211".
[[nodiscard]] std::string_view FlacVendorString() noexcept;
// Exact identities of the lossy codec libraries compiled into this build.
[[nodiscard]] std::string_view VorbisVendorString() noexcept; // "Xiph.Org libVorbis 1.3.7"
[[nodiscard]] std::string_view OpusVersionString() noexcept;  // "libopus 1.5.2"
[[nodiscard]] std::string_view LameVersionString() noexcept;  // "3.100"
} // namespace arken::audio::v1
