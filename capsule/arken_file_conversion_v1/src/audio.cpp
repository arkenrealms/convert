#include "arken_file_conversion_v1/audio.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
AudioResult Failure(Error error, audio::Error reason = audio::Error::none) {
    AudioResult result; result.error = error; result.codecError = reason; return result;
}
bool Known(audio::Container container) noexcept {
    switch (container) {
    case audio::Container::wav: case audio::Container::flac: case audio::Container::aiff:
    case audio::Container::vorbis: case audio::Container::opus: case audio::Container::mp3: return true;
    }
    return false;
}
// Like the image options: a control is meaningful only for its own output (and the MP3
// bitrate only for CBR, the VBR quality only for VBR); a changed value elsewhere is rejected
// rather than ignored. Ranges with a fixed bound are checked here, before any decoding; the
// MP3 bitrate set depends on the sample rate's MPEG version and is checked by the encoder.
Error OptionMisuse(audio::Container destination, const audio::LossyOptions& options, audio::Error& reason) noexcept {
    const audio::LossyOptions defaults;
    const bool mp3 = options.mp3RateControl == audio::Mp3RateControl::cbr || options.mp3RateControl == audio::Mp3RateControl::vbr;
    if (!mp3) { reason = audio::Error::invalid_option; return Error::invalid_options; }
    if (destination != audio::Container::vorbis && options.vorbisQuality != defaults.vorbisQuality) return Error::invalid_options;
    if (destination != audio::Container::opus && options.opusBitrateKbps != defaults.opusBitrateKbps) return Error::invalid_options;
    if (destination != audio::Container::mp3 && (options.mp3RateControl != defaults.mp3RateControl
        || options.mp3BitrateKbps != defaults.mp3BitrateKbps || options.mp3VbrQuality != defaults.mp3VbrQuality))
        return Error::invalid_options;
    if (options.mp3RateControl == audio::Mp3RateControl::vbr && options.mp3BitrateKbps != defaults.mp3BitrateKbps) return Error::invalid_options;
    if (options.mp3RateControl == audio::Mp3RateControl::cbr && options.mp3VbrQuality != defaults.mp3VbrQuality) return Error::invalid_options;
    if (options.vorbisQuality > 100 || options.opusBitrateKbps < 6 || options.opusBitrateKbps > 510 || options.mp3VbrQuality > 9) {
        reason = audio::Error::invalid_option; return Error::invalid_options;
    }
    return Error::none;
}
}
audio::Limits StandaloneAudioLimits() noexcept {
    audio::Limits limits;
    limits.maximumSourceBytes = maximumAudioSourceBytes;
    limits.maximumOutputBytes = maximumAudioOutputBytes;
    // Binding limit for lossy sources (see audio.hpp): a compressed file's size says little
    // about its decoded length.
    limits.maximumTotalSamples = maximumAudioSamples;
    limits.maximumChannels = maximumAudioChannels;
    limits.minimumSampleRate = 1;
    limits.maximumSampleRate = maximumAudioSampleRate;
    limits.maximumMetadataBlockBytes = maximumAudioMetadataBlockBytes;
    limits.maximumMetadataBlocks = maximumAudioMetadataBlocks;
    return limits;
}
AudioResult ConvertStandaloneAudio(audio::Container source, audio::Container destination,
                                   std::span<const std::byte> bytes, const audio::LossyOptions& options) noexcept {
    if (bytes.empty() || bytes.size() > maximumAudioSourceBytes) return Failure(Error::input_limit);
    if (!Known(source)) return Failure(Error::input_format_unavailable);
    if (!Known(destination)) return Failure(Error::output_format_unavailable);
    audio::Error optionReason = audio::Error::none;
    if (const auto misuse = OptionMisuse(destination, options, optionReason); misuse != Error::none)
        return Failure(misuse, optionReason);
    try {
        auto converted = audio::Convert(source, destination, bytes, options, StandaloneAudioLimits());
        if (converted.decodeIssue.code != audio::Error::none) {
            const auto code = converted.decodeIssue.code;
            return Failure(code == audio::Error::input_too_large ? Error::input_limit
                : code == audio::Error::allocation_failed ? Error::allocation_failed : Error::decode_failed, code);
        }
        if (converted.encodeError != audio::Error::none) {
            const auto code = converted.encodeError;
            return Failure(code == audio::Error::allocation_failed ? Error::allocation_failed
                : code == audio::Error::invalid_option ? Error::invalid_options : Error::encode_failed, code);
        }
        const auto& report = converted.report;
        AudioResult result;
        result.sampleRate = converted.sampleRate; result.channels = converted.channels;
        result.bitsPerSample = converted.bitsPerSample; result.frames = converted.frames;
        result.skippedMetadata = report.skippedChunks + report.ignoredMetadataBlocks + report.skippedTags;
        result.ignoredComments = report.ignoredComments || report.droppedComments > 0;
        result.channelMaskDropped = converted.droppedChannelMask;
        result.lossySource = report.lossy; result.lossyOutput = converted.lossyOutput;
        result.generationLoss = converted.generationLoss;
        result.commentsDropped = report.droppedComments; result.tagsSkipped = report.skippedTags;
        result.streamsSkipped = report.skippedStreams;
        result.encoderDelayTrimmed = report.encoderDelayTrimmed; result.encoderDelayUnknown = report.encoderDelayUnknown;
        result.trimmedLeadingFrames = report.trimmedLeadingFrames; result.trimmedTrailingFrames = report.trimmedTrailingFrames;
        result.silencedFrames = report.silencedFrames; result.originalSampleRate = report.originalSampleRate;
        result.bytes = std::move(converted.bytes);
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view AudioErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Audio converted";
    case Error::input_limit: return "Input must contain between 1 byte and 64 MiB";
    case Error::invalid_options: return "Audio options are invalid for the selected output";
    case Error::decode_failed: return "Audio encoding is invalid, unsupported or exceeds the profile limits";
    case Error::encode_failed: return "Audio output could not be produced for this source in the selected format and profile limits";
    default: return ErrorMessage(error);
    }
}
}
