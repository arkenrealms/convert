#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Portable, memory-only WAV, FLAC and AIFF audio codecs. Every call takes a complete
// byte span and returns a complete result; nothing opens files, reads stdio,
// starts threads or keeps global state. Integer PCM only: precision is never
// reduced and floating-point sources are rejected, never quantized.
namespace arken::audio::v1 {

enum class Container : std::uint32_t { wav = 1, flac = 2, aiff = 3 };

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
//    Channel order in `samples` is always the source order; nothing is remapped.
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
    unsupported_sample_rate // AIFF 80-bit extended rate is not an exact positive integer (fraction, negative, NaN, infinity)
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
// Decode with `source`, re-encode with `destination` under the same limits.
[[nodiscard]] ConvertResult Convert(Container source, Container destination,
    std::span<const std::byte>, const Limits& = {}) noexcept;

[[nodiscard]] Error ValidateAudio(const Audio&, const Limits& = {}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
// Exact libFLAC identity compiled into this build, e.g. "reference libFLAC 1.5.0 20250211".
[[nodiscard]] std::string_view FlacVendorString() noexcept;
} // namespace arken::audio::v1
