#include "arken_file_conversion_v1/audio.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
AudioResult Failure(Error error, audio::Error reason = audio::Error::none) {
    AudioResult result; result.error = error; result.codecError = reason; return result;
}
bool Known(audio::Container container) noexcept {
    // Vorbis, Opus and MP3 (Container 4-6 in the codec's plan) are not admitted here.
    return container == audio::Container::wav || container == audio::Container::flac || container == audio::Container::aiff;
}
}
audio::Limits StandaloneAudioLimits() noexcept {
    audio::Limits limits;
    limits.maximumSourceBytes = maximumAudioSourceBytes;
    limits.maximumOutputBytes = maximumAudioOutputBytes;
    limits.maximumTotalSamples = maximumAudioSamples;
    limits.maximumChannels = maximumAudioChannels;
    limits.minimumSampleRate = 1;
    limits.maximumSampleRate = maximumAudioSampleRate;
    limits.maximumMetadataBlockBytes = maximumAudioMetadataBlockBytes;
    limits.maximumMetadataBlocks = maximumAudioMetadataBlocks;
    return limits;
}
AudioResult ConvertStandaloneAudio(audio::Container source, audio::Container destination,
                                   std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > maximumAudioSourceBytes) return Failure(Error::input_limit);
    if (!Known(source)) return Failure(Error::input_format_unavailable);
    if (!Known(destination)) return Failure(Error::output_format_unavailable);
    try {
        auto converted = audio::Convert(source, destination, bytes, StandaloneAudioLimits());
        if (converted.decodeIssue.code != audio::Error::none) {
            const auto code = converted.decodeIssue.code;
            return Failure(code == audio::Error::input_too_large ? Error::input_limit
                : code == audio::Error::allocation_failed ? Error::allocation_failed : Error::decode_failed, code);
        }
        if (converted.encodeError != audio::Error::none)
            return Failure(converted.encodeError == audio::Error::allocation_failed
                ? Error::allocation_failed : Error::encode_failed, converted.encodeError);
        AudioResult result;
        result.sampleRate = converted.sampleRate; result.channels = converted.channels;
        result.bitsPerSample = converted.bitsPerSample; result.frames = converted.frames;
        result.skippedMetadata = converted.report.skippedChunks + converted.report.ignoredMetadataBlocks;
        result.ignoredComments = converted.report.ignoredComments;
        result.channelMaskDropped = converted.droppedChannelMask;
        result.bytes = std::move(converted.bytes);
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view AudioErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Audio converted";
    case Error::input_limit: return "Input must contain between 1 byte and 64 MiB";
    case Error::decode_failed: return "Audio encoding is invalid, unsupported or exceeds the profile limits";
    case Error::encode_failed: return "Audio output could not be produced within the profile limits";
    default: return ErrorMessage(error);
    }
}
}
