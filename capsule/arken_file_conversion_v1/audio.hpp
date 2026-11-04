#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_audio_codecs_v1/api.hpp"

namespace arken::file_conversion::v1 {
namespace audio = arken::audio::v1;
// Standalone native MCP audio profile: WAV, FLAC and AIFF/AIFF-C losslessly, and Vorbis in
// Ogg, Opus in Ogg and MP3 as explicitly lossy formats, in every direction. Resident, not
// streaming. Lossless: integer PCM only; AIFF-C is read (NONE, twos, sowt, raw) and AIFF is
// written (COMM + SSND). Float (fl32/fl64), mu-law, A-law and other compressed AIFF-C, and
// non-integer AIFF sample rates, are rejected like float WAV. Lossy: decoded to 16-bit PCM
// with container-declared encoder delay and padding trimmed; encoded from 1-2 channels at
// the format's own sample rates only (never resampled) with the explicit controls of
// audio::LossyOptions. It is a separate admission from the image profiles and from any
// browser or hosted audio surface, none of which this declaration authorizes.
//
// 64 MiB of source admits about 6 min 20 s of 16-bit 44.1 kHz stereo WAV (176,400 B/s);
// the 8 MiB image limit would admit only ~47 s. The sample budget is the same duration:
// 32 Mi samples is exactly a 64 MiB 16-bit WAV, so flac_to_wav of 16-bit audio meets the
// output limit and the decode limit together. Peak memory of one call is about
// source (64 MiB) + canonical int32 PCM (128 MiB) + output (64 MiB) + libFLAC's fixed
// per-stream state and its verification decoder, i.e. roughly 256 MiB plus a few MiB.
//
// Lossy decoding is bounded by the sample budget, not by the file size: 32 Mi samples is
// about 6 min 20 s of 44.1 kHz stereo or 5 min 49 s of 48 kHz stereo (every Opus stream
// decodes at 48 kHz), while a 64 MiB MP3 or Ogg file can hold hours. The codec checks the
// budget against the container's declared length (final granule, counted MP3 frames)
// before decoding and again while decoding, so a longer source is rejected with
// sample_limit, never decoded in part.
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
    // The codec owner's reason: the decode issue, else the encode error (invalid_option for
    // an out-of-range or illegal lossy control).
    audio::Error codecError = audio::Error::none;
    // The decoded PCM that was encoded: every lossy source decodes to 16 bits and every Opus
    // source to 48 kHz. For Opus output, a decoder plays the stream at 48 kHz with
    // frames x 48000 / sampleRate frames.
    std::uint32_t sampleRate = 0, channels = 0, bitsPerSample = 0;
    std::uint64_t frames = 0;
    // What the source carried that the output does not: WAV chunks other than fmt/data,
    // AIFF chunks other than COMM/SSND (MARK, INST, COMT, NAME, AUTH, ANNO, APPL, ID3, ...),
    // FLAC metadata blocks other than STREAMINFO, and MP3 ID3v2/ID3v1/APEv2 tags (also
    // counted in tagsSkipped).
    std::uint32_t skippedMetadata = 0;
    // FLAC comments other than the channel mask, or any Vorbis comment / OpusTags entry
    // (counted in commentsDropped), were not carried.
    bool ignoredComments = false;
    // AIFF has no channel-mask field: a mask declared by a WAV/FLAC source was not written.
    // Channel order is kept either way.
    bool channelMaskDropped = false;
    // Lossy labels: the source codec is lossy (decoded PCM is not the original signal), the
    // output is lossy (an approximation, never reversible), or both (a second generation of
    // coding loss on top of the source's; quality is never restored).
    bool lossySource = false, lossyOutput = false, generationLoss = false;
    // Vorbis comment / OpusTags entries (including METADATA_BLOCK_PICTURE album art), MP3
    // tags and other multiplexed Ogg logical streams that were not carried.
    std::uint32_t commentsDropped = 0, tagsSkipped = 0, streamsSkipped = 0;
    // Encoder delay and padding of a lossy source were removed using container data (Opus
    // pre-skip and final granule, Vorbis granules, MP3 LAME/Lavc tag); frames removed.
    bool encoderDelayTrimmed = false;
    // MP3 without a LAME/Lavc gapless tag: nothing was trimmed, so the output begins with
    // the encoder and decoder delay and ends with the encoder's padding.
    bool encoderDelayUnknown = false;
    std::uint64_t trimmedLeadingFrames = 0, trimmedTrailingFrames = 0;
    // MP3 leading frames whose bit reservoir precedes the stream (a cut file), emitted as silence.
    std::uint32_t silencedFrames = 0;
    // Opus source: the input sample rate OpusHead declares (informational; decoded at 48 kHz,
    // nothing is resampled back to it).
    std::uint32_t originalSampleRate = 0;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] audio::Limits StandaloneAudioLimits() noexcept;
// Decode `source` and re-encode as `destination` under StandaloneAudioLimits().
// Lossless destinations preserve integer samples, rate, channel count and order exactly, and
// any declared channel mask too unless the destination is AIFF (channelMaskDropped); float,
// compressed WAV/AIFF-C, RF64/W64 and Ogg FLAC are rejected, never approximated.
// Lossy destinations use `options` with the codec's defaults (Vorbis quality 50, Opus
// 128 kbps, MP3 CBR 192 kbps; MP3 VBR quality 2). As with the image options, a control that
// differs from its default is invalid_options unless the destination is its format, and
// mp3BitrateKbps other than 192 requires CBR, mp3VbrQuality other than 2 requires VBR. An
// out-of-range value, or an MP3 bitrate the sample rate's MPEG version does not define, is
// invalid_options with codecError invalid_option. Opus encodes 8/12/16/24/48 kHz and MP3
// the nine Layer III rates only (resampling_required otherwise), from 1-2 channels
// (channel_limit otherwise).
[[nodiscard]] AudioResult ConvertStandaloneAudio(audio::Container source, audio::Container destination,
    std::span<const std::byte>, const audio::LossyOptions& options = {}) noexcept;
[[nodiscard]] std::string_view AudioErrorMessage(Error) noexcept;
}
