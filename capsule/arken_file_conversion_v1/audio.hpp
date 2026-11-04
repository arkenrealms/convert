#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_audio_codecs_v1/api.hpp"

namespace arken::file_conversion::v1 {
namespace audio = arken::audio::v1;
// Standalone native MCP audio profile: WAV, FLAC and AIFF/AIFF-C in every direction,
// integer PCM only, resident (not streaming). AIFF-C is read (NONE, twos, sowt, raw); AIFF
// is written (COMM + SSND). Float (fl32/fl64), mu-law, A-law and other compressed AIFF-C,
// and non-integer AIFF sample rates, are rejected like float WAV. It is a separate admission from the image profiles and from any browser or
// hosted audio surface, none of which this declaration authorizes.
//
// 64 MiB of source admits about 6 min 20 s of 16-bit 44.1 kHz stereo WAV (176,400 B/s);
// the 8 MiB image limit would admit only ~47 s. The sample budget is the same duration:
// 32 Mi samples is exactly a 64 MiB 16-bit WAV, so flac_to_wav of 16-bit audio meets the
// output limit and the decode limit together. Peak memory of one call is about
// source (64 MiB) + canonical int32 PCM (128 MiB) + output (64 MiB) + libFLAC's fixed
// per-stream state and its verification decoder, i.e. roughly 256 MiB plus a few MiB.
inline constexpr std::uint32_t maximumAudioSourceBytes = 64u * 1024u * 1024u;
inline constexpr std::uint32_t maximumAudioOutputBytes = 64u * 1024u * 1024u;
inline constexpr std::uint64_t maximumAudioSamples = 32ull * 1024u * 1024u; // frames x channels
inline constexpr std::uint32_t maximumAudioChannels = 8;
inline constexpr std::uint32_t maximumAudioSampleRate = 384000;
// Embedded cover art is a FLAC PICTURE block and often exceeds 1 MiB. Skipped blocks and
// chunks are never copied, so a larger per-block bound costs nothing beyond the source.
inline constexpr std::uint32_t maximumAudioMetadataBlockBytes = 8u * 1024u * 1024u;
inline constexpr std::uint32_t maximumAudioMetadataBlocks = 256;

struct AudioResult {
    Error error = Error::none;
    // The codec owner's reason: the decode issue, else the encode error.
    audio::Error codecError = audio::Error::none;
    std::uint32_t sampleRate = 0, channels = 0, bitsPerSample = 0;
    std::uint64_t frames = 0;
    // What the source carried that the output does not: WAV chunks other than fmt/data,
    // AIFF chunks other than COMM/SSND (MARK, INST, COMT, NAME, AUTH, ANNO, APPL, ID3, ...),
    // FLAC metadata blocks other than STREAMINFO, and FLAC comments other than the mask.
    std::uint32_t skippedMetadata = 0;
    bool ignoredComments = false;
    // AIFF has no channel-mask field: a mask declared by a WAV/FLAC source was not written.
    // Channel order is kept either way.
    bool channelMaskDropped = false;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] audio::Limits StandaloneAudioLimits() noexcept;
// Decode `source` and re-encode as `destination` under StandaloneAudioLimits(). Integer
// samples, rate, channel count and order are preserved exactly, and any declared channel
// mask too unless the destination is AIFF (channelMaskDropped); float, compressed WAV/AIFF-C,
// RF64/W64 and Ogg FLAC are rejected, never approximated.
[[nodiscard]] AudioResult ConvertStandaloneAudio(audio::Container source, audio::Container destination,
    std::span<const std::byte>) noexcept;
[[nodiscard]] std::string_view AudioErrorMessage(Error) noexcept;
}
