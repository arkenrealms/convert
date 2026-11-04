#include "arken_audio_codecs_v1/api.hpp"
#include <new>

namespace arken::audio::v1 {
Error ValidateAudio(const Audio& audio, const Limits& limits) noexcept {
    if (audio.channels == 0 || audio.channels > limits.maximumChannels) return Error::channel_limit;
    if (audio.bitsPerSample < 4 || audio.bitsPerSample > 32) return Error::unsupported_bit_depth;
    if (audio.sampleRate == 0 || audio.sampleRate < limits.minimumSampleRate
        || audio.sampleRate > limits.maximumSampleRate) return Error::sample_rate_limit;
    if (audio.frames > limits.maximumTotalSamples / audio.channels) return Error::sample_limit;
    if (audio.samples.size() != audio.frames * audio.channels) return Error::invalid_audio;
    const std::int64_t high = (std::int64_t(1) << (audio.bitsPerSample - 1)) - 1;
    const std::int64_t low = -high - 1;
    for (const auto sample : audio.samples)
        if (sample < low || sample > high) return Error::invalid_audio;
    return Error::none;
}

namespace {
bool Known(Container container) noexcept {
    return container == Container::wav || container == Container::flac || container == Container::aiff;
}
DecodeResult Decode(Container container, std::span<const std::byte> bytes, const Limits& limits) noexcept {
    switch (container) {
    case Container::wav: return DecodeWav(bytes, limits);
    case Container::flac: return DecodeFlac(bytes, limits);
    case Container::aiff: return DecodeAiff(bytes, limits);
    }
    DecodeResult unknown;
    unknown.issue.code = Error::invalid_audio;
    return unknown;
}
EncodeResult Encode(Container container, const Audio& audio, const Limits& limits) noexcept {
    switch (container) {
    case Container::wav: return EncodeWav(audio, limits);
    case Container::flac: return EncodeFlac(audio, limits);
    case Container::aiff: return EncodeAiff(audio, limits);
    }
    return {Error::invalid_audio, {}};
}
} // namespace

ConvertResult Convert(Container source, Container destination, std::span<const std::byte> bytes,
    const Limits& limits) noexcept {
    ConvertResult result;
    if (!Known(source) || !Known(destination)) {
        result.encodeError = Error::invalid_audio;
        return result;
    }
    auto decoded = Decode(source, bytes, limits);
    result.decodeIssue = decoded.issue;
    result.report = decoded.report;
    if (!decoded) return result;
    result.sampleRate = decoded.audio.sampleRate;
    result.channels = decoded.audio.channels;
    result.bitsPerSample = decoded.audio.bitsPerSample;
    result.frames = decoded.audio.frames;
    auto encoded = Encode(destination, decoded.audio, limits);
    result.encodeError = encoded.error;
    if (encoded) {
        result.droppedChannelMask = destination == Container::aiff && decoded.audio.channelMask.has_value();
        result.bytes = std::move(encoded.bytes);
    }
    return result;
}

std::string_view ErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "no error";
    case Error::input_too_large: return "audio source exceeds the admitted byte limit";
    case Error::truncated: return "audio source is truncated";
    case Error::wrong_magic: return "audio source is not the requested container";
    case Error::unsupported_container: return "RF64, BW64, Wave64, RIFX and Ogg FLAC containers are not supported";
    case Error::unsupported_encoding: return "only integer PCM is supported; compressed WAV and AIFF-C encodings are rejected";
    case Error::unsupported_float: return "floating-point WAV and AIFF-C samples are rejected rather than quantized";
    case Error::unsupported_bit_depth: return "bit depth is outside the supported integer PCM profile";
    case Error::invalid_header: return "audio header fields are inconsistent";
    case Error::invalid_layout: return "audio chunk or metadata layout is invalid";
    case Error::invalid_sample_padding: return "WAV or AIFF padding bits below the significant sample bits are not zero";
    case Error::channel_limit: return "channel count is outside the admitted range";
    case Error::sample_rate_limit: return "sample rate is outside the admitted range";
    case Error::sample_limit: return "audio exceeds the admitted sample count";
    case Error::metadata_limit: return "audio metadata exceeds the admitted size or count";
    case Error::output_limit: return "encoded audio would exceed the admitted output size";
    case Error::invalid_metadata: return "audio metadata is malformed";
    case Error::corrupt_stream: return "FLAC stream is corrupt";
    case Error::checksum_mismatch: return "decoded audio does not match the stream MD5 checksum";
    case Error::length_mismatch: return "decoded sample count does not match the declared total";
    case Error::invalid_audio: return "audio samples or parameters are inconsistent";
    case Error::allocation_failed: return "audio allocation failed";
    case Error::codec_failed: return "the FLAC codec failed";
    case Error::verify_failed: return "FLAC encoder verification found a mismatch";
    case Error::unsupported_sample_rate: return "AIFF sample rate is not an exact positive integer";
    }
    return "unknown audio error";
}
} // namespace arken::audio::v1
