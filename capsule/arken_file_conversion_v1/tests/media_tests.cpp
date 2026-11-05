// Standalone audio, document, table and markup owners: exact sample preservation, exact
// text, reported metadata and exclusions, explicit limits, typed rejections and the codec
// owner's reason on every failure.
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
#include "arken_file_conversion_v1/markup.hpp"
#include "arken_file_conversion_v1/tables.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include "arken_document_extraction_v1/test_support/package_fixtures.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace c = arken::file_conversion::v1;
namespace audio = arken::audio::v1;
namespace document = arken::document::v1;
namespace tabular = arken::document::v1::tabular;
namespace markup = arken::markup::v1;
namespace {
void Check(bool passed, std::string_view message) { if (!passed) throw std::runtime_error(std::string(message)); }
std::string Text(const std::vector<std::byte>& bytes) { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }
void Put32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[at + std::size_t(i)] = std::byte((value >> (8 * i)) & 0xff);
}
audio::Audio Signal(std::uint32_t rate, std::uint32_t channels, std::uint32_t bits, std::uint64_t frames,
                    std::optional<std::uint32_t> mask = std::nullopt) {
    audio::Audio value{rate, channels, bits, mask, frames, {}};
    const std::int64_t high = (std::int64_t(1) << (bits - 1)) - 1, low = -high - 1;
    std::uint32_t seed = 2463534242u;
    for (std::uint64_t i = 0; i < frames * channels; ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        std::int64_t sample = std::int64_t(seed % std::uint32_t(high + 1)) - std::int64_t(seed & 1u) * (high + 1);
        if (i % 97 == 0) { sample = high; }
        if (i % 89 == 0) { sample = low; }
        if (i < channels) { sample = 0; }
        value.samples.push_back(std::int32_t(sample));
    }
    return value;
}
void AudioCases() {
    using audio::Container;
    for (const auto& source : {Signal(44100, 2, 16, 4410), Signal(48000, 1, 24, 2400), Signal(96000, 6, 24, 960, 0x3fu),
                               Signal(8000, 1, 8, 800), Signal(22050, 2, 32, 512), Signal(44100, 2, 20, 700)}) {
        const auto wav = audio::EncodeWav(source); Check(bool(wav), "WAV fixture");
        const auto flac = c::ConvertStandaloneAudio(Container::wav, Container::flac, wav.bytes);
        Check(bool(flac) && flac.codecError == audio::Error::none, "wav_to_flac failed");
        Check(flac.sampleRate == source.sampleRate && flac.channels == source.channels
            && flac.bitsPerSample == source.bitsPerSample && flac.frames == source.frames, "wav_to_flac stream report");
        Check(flac.skippedMetadata == 0 && !flac.ignoredComments, "nothing was skipped");
        // 12/20-bit WAV is WAVE_FORMAT_EXTENSIBLE and so always declares a mask (0 when none).
        const auto declared = audio::DecodeWav(wav.bytes).audio.channelMask;
        const auto decoded = audio::DecodeFlac(flac.bytes);
        Check(bool(decoded) && decoded.audio.samples == source.samples && decoded.audio.channelMask == declared,
            "FLAC samples or channel mask differ from the WAV source");
        const auto back = c::ConvertStandaloneAudio(Container::flac, Container::wav, flac.bytes);
        Check(bool(back) && back.bytes == wav.bytes, "flac_to_wav did not restore the exact WAV");
        Check(c::ConvertStandaloneAudio(Container::wav, Container::flac, wav.bytes).bytes == flac.bytes, "FLAC output not deterministic");
    }
    // Chunks the output does not carry are counted, never silently claimed preserved.
    auto tagged = audio::EncodeWav(Signal(44100, 2, 16, 100)).bytes;
    const std::string list("LIST\x0c\x00\x00\x00INFOISFT\x00\x00\x00\x00", 20);
    for (char ch : list) tagged.push_back(std::byte(ch));
    Put32(tagged, 4, std::uint32_t(tagged.size() - 8));
    const auto reported = c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, tagged);
    Check(bool(reported) && reported.skippedMetadata == 1, "skipped LIST chunk not reported");
    // IEEE float WAV is rejected, never quantized.
    std::vector<std::byte> floatWav(48);
    const char header[] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x03\0\x01\0\x40\x1f\0\0\0\x7d\0\0\x04\0\x20\0data\x04\0\0\0";
    std::memcpy(floatWav.data(), header, 44); Put32(floatWav, 4, 40);
    const auto rejected = c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, floatWav);
    Check(rejected.error == c::Error::decode_failed && rejected.codecError == audio::Error::unsupported_float
        && rejected.bytes.empty(), "float WAV not rejected as unsupported_float");
    const auto wrong = c::ConvertStandaloneAudio(audio::Container::flac, audio::Container::wav,
        audio::EncodeWav(Signal(8000, 1, 16, 10)).bytes);
    Check(wrong.error == c::Error::decode_failed && wrong.codecError == audio::Error::wrong_magic, "WAV passed as FLAC");
    Check(c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, {}).error == c::Error::input_limit, "empty audio");
    std::vector<std::byte> over(c::maximumAudioSourceBytes + 1);
    Check(c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, over).error == c::Error::input_limit, "oversized audio");
    Check(c::ConvertStandaloneAudio(audio::Container(9), audio::Container::flac, floatWav).error == c::Error::input_format_unavailable,
        "unknown audio container");
    // The sample budget is separate from the byte limit: a 40 MiB 8-bit mono WAV holds
    // 40 Mi samples, more than the 32 Mi budget, and is rejected before decoding.
    auto huge = audio::EncodeWav(Signal(8000, 1, 8, 4)).bytes;
    const std::uint32_t samples = 40u * 1024u * 1024u;
    huge.resize(44 + samples, std::byte{0x80});
    Put32(huge, 4, std::uint32_t(huge.size() - 8)); Put32(huge, 40, samples);
    const auto budget = c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, huge);
    Check(budget.error == c::Error::decode_failed && budget.codecError == audio::Error::sample_limit && budget.bytes.empty(),
        "WAV above the sample budget accepted");
    // A data chunk that declares more than the file holds is rejected, never read short.
    auto truncated = audio::EncodeWav(Signal(44100, 2, 16, 4)).bytes;
    Put32(truncated, 40, 1000);
    Check(c::ConvertStandaloneAudio(audio::Container::wav, audio::Container::flac, truncated).codecError == audio::Error::truncated,
        "truncated WAV accepted");
    Check(c::AudioErrorMessage(c::Error::input_limit).find("64 MiB") != std::string_view::npos, "audio limit message");
}
void DocumentCases() {
    using document::OutputFormat; using document::SourceFormat;
    const auto docx = docx_fixture::Bytes(docx_fixture::Docx(
        R"(<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Notes</w:t></w:r></w:p>)"
        R"(<w:p><w:r><w:t xml:space="preserve">Plain </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>bold</w:t></w:r><w:r><w:t>*</w:t></w:r></w:p>)"
        R"(<w:tbl><w:tr><w:tc><w:p><w:r><w:t>a</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc></w:tr>)"
        R"(<w:tr><w:tc><w:p><w:r><w:t>1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>2</w:t></w:r></w:p></w:tc></w:tr></w:tbl>)"));
    const auto text = c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::text, docx);
    Check(bool(text) && Text(text.bytes) == "Notes\n\nPlain bold*\n\na\tb\n1\t2\n", "docx_to_txt bytes: " + Text(text.bytes));
    Check(text.paragraphs == 1 && text.headings == 1 && text.listItems == 0 && text.tables == 1 && text.tableRows == 2,
        "docx block report");
    const auto markdown = c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::markdown, docx);
    Check(bool(markdown) && Text(markdown.bytes) == "# Notes\n\nPlain **bold**\\*\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n",
        "docx_to_markdown bytes: " + Text(markdown.bytes));
    const auto rtf = docx_fixture::Bytes(R"({\rtf1\ansi{\fonttbl{\f0 Arial;}}{\header Skipped\par}\pard\outlinelevel0 Title\par\pard caf\'e9 {\i it}\par\pard{\pntext\tab}Item\par})");
    const auto rtfText = c::ConvertStandaloneDocument(SourceFormat::rtf, OutputFormat::text, rtf);
    Check(bool(rtfText) && Text(rtfText.bytes) == "Title\n\ncaf\xc3\xa9 it\n\n- Item\n", "rtf_to_txt bytes: " + Text(rtfText.bytes));
    Check(rtfText.headings == 1 && rtfText.paragraphs == 1 && rtfText.listItems == 1, "rtf block report");
    const auto rtfMarkdown = c::ConvertStandaloneDocument(SourceFormat::rtf, OutputFormat::markdown, rtf);
    Check(bool(rtfMarkdown) && Text(rtfMarkdown.bytes) == "# Title\n\ncaf\xc3\xa9 *it*\n\n- Item\n", "rtf_to_markdown bytes: " + Text(rtfMarkdown.bytes));
    const auto encrypted = c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::text, docx_fixture::Bytes(docx_fixture::EncryptedDocx()));
    Check(encrypted.error == c::Error::decode_failed && encrypted.documentError == document::Error::docx_encrypted
        && encrypted.bytes.empty(), "encrypted DOCX not rejected");
    const auto macro = c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::text,
        docx_fixture::Bytes(docx_fixture::Docx("<w:p/>", {{"word/vbaProject.bin", "x"}})));
    Check(macro.documentError == document::Error::docx_macro_enabled, "macro-enabled DOCX not rejected");
    const auto notRtf = c::ConvertStandaloneDocument(SourceFormat::rtf, OutputFormat::text, docx);
    Check(notRtf.error == c::Error::decode_failed && notRtf.documentError == document::Error::rtf_not_rtf, "DOCX passed as RTF");
    Check(c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::text, {}).error == c::Error::input_limit, "empty document");
    std::vector<std::byte> over(c::maximumDocumentSourceBytes + 1);
    Check(c::ConvertStandaloneDocument(SourceFormat::rtf, OutputFormat::text, over).error == c::Error::input_limit, "oversized document");
    Check(c::ConvertStandaloneDocument(SourceFormat(7), OutputFormat::text, rtf).error == c::Error::input_format_unavailable, "unknown source");
    Check(c::ConvertStandaloneDocument(SourceFormat::rtf, OutputFormat(7), rtf).error == c::Error::output_format_unavailable, "unknown output");
    // A main part over the 16 MiB per-member bound is rejected before inflation.
    const std::string body = "<w:p><w:r><w:t>" + std::string(c::maximumDocumentEntryExpandedBytes, 'x') + "</w:t></w:r></w:p>";
    const auto large = c::ConvertStandaloneDocument(SourceFormat::docx, OutputFormat::text, docx_fixture::Bytes(docx_fixture::Docx(body)));
    Check(large.error == c::Error::input_limit && large.documentError == document::Error::zip_size_limit, "16 MiB member bound");
    const auto options = c::StandaloneDocumentOptions();
    Check(options.extract.maximumSourceBytes == c::maximumDocumentSourceBytes
        && options.serialize.maximumOutputBytes == c::maximumDocumentOutputBytes, "document limits");
    // The DocumentOptions form is the same conversion for DOCX/RTF.
    Check(c::ConvertStandaloneDocument({c::DocumentInput::docx, OutputFormat::markdown, false}, docx).bytes == markdown.bytes,
        "DocumentOptions DOCX form differs");
    Check(c::ConvertStandaloneDocument({c::DocumentInput::rtf, OutputFormat::text, true}, rtf).error == c::Error::invalid_options,
        "notes option accepted for RTF");
}
// A float (fl32) AIFF-C: FORM/AIFC with FVER, COMM (1 ch, 1 frame, 32 bit, 8000 Hz, fl32) and SSND.
std::vector<std::byte> FloatAifc() {
    std::string b;
    const auto be = [&](std::uint32_t v, int bytes) { for (int i = bytes - 1; i >= 0; --i) b.push_back(char((v >> (8 * i)) & 0xff)); };
    b += "FORM"; be(68, 4); b += "AIFC";
    b += "FVER"; be(4, 4); be(0xA2805140u, 4);
    b += "COMM"; be(24, 4); be(1, 2); be(1, 4); be(32, 2);
    b += std::string("\x40\x0b\xfa\x00\x00\x00\x00\x00\x00\x00", 10); b += "fl32"; be(0, 2);
    b += "SSND"; be(12, 4); be(0, 4); be(0, 4); be(0x3f800000u, 4);
    return docx_fixture::Bytes(b);
}
void AiffCases() {
    using audio::Container;
    for (const auto& source : {Signal(44100, 2, 16, 4410), Signal(48000, 1, 24, 2400), Signal(22050, 2, 32, 512), Signal(8000, 1, 8, 800)}) {
        const auto wav = audio::EncodeWav(source); Check(bool(wav), "WAV fixture");
        const auto aiff = c::ConvertStandaloneAudio(Container::wav, Container::aiff, wav.bytes);
        Check(bool(aiff) && aiff.sampleRate == source.sampleRate && aiff.channels == source.channels
            && aiff.bitsPerSample == source.bitsPerSample && aiff.frames == source.frames && !aiff.channelMaskDropped, "wav_to_aiff report");
        const auto decoded = audio::DecodeAiff(aiff.bytes);
        Check(bool(decoded) && decoded.audio.samples == source.samples, "AIFF samples differ from the WAV source");
        const auto back = c::ConvertStandaloneAudio(Container::aiff, Container::wav, aiff.bytes);
        Check(bool(back) && back.bytes == wav.bytes, "aiff_to_wav did not restore the exact WAV");
        const auto flac = c::ConvertStandaloneAudio(Container::aiff, Container::flac, aiff.bytes);
        Check(bool(flac) && audio::DecodeFlac(flac.bytes).audio.samples == source.samples, "aiff_to_flac lost samples");
        const auto again = c::ConvertStandaloneAudio(Container::flac, Container::aiff, flac.bytes);
        Check(bool(again) && again.bytes == aiff.bytes, "flac_to_aiff did not restore the exact AIFF");
    }
    // AIFF has no mask: a 5.1 mask from WAV is reported as dropped, channel order kept.
    const auto surround = Signal(96000, 6, 24, 960, 0x3fu);
    const auto dropped = c::ConvertStandaloneAudio(Container::wav, Container::aiff, audio::EncodeWav(surround).bytes);
    Check(bool(dropped) && dropped.channelMaskDropped && audio::DecodeAiff(dropped.bytes).audio.samples == surround.samples,
        "mask drop not reported");
    const auto kept = c::ConvertStandaloneAudio(Container::wav, Container::flac, audio::EncodeWav(surround).bytes);
    Check(bool(kept) && !kept.channelMaskDropped, "mask reported dropped for FLAC output");
    const auto floatAifc = c::ConvertStandaloneAudio(Container::aiff, Container::wav, FloatAifc());
    Check(floatAifc.error == c::Error::decode_failed && floatAifc.codecError == audio::Error::unsupported_float && floatAifc.bytes.empty(),
        "float AIFF-C not rejected as unsupported_float");
    Check(c::ConvertStandaloneAudio(Container::aiff, Container::wav, audio::EncodeWav(Signal(8000, 1, 16, 4)).bytes).codecError
        == audio::Error::wrong_magic, "WAV passed as AIFF");
    Check(c::ConvertStandaloneAudio(Container(7), Container::wav, FloatAifc()).error == c::Error::input_format_unavailable,
        "an unadmitted container (7) was accepted");
    Check(c::ConvertStandaloneAudio(Container::aiff, Container(0), FloatAifc()).error == c::Error::output_format_unavailable,
        "an unadmitted output container (0) was accepted");
}
// A two-tone 16-bit programme (no extrema: lossy codecs are compared by length and labels).
audio::Audio Tone(std::uint32_t rate, std::uint32_t channels, std::uint64_t frames) {
    audio::Audio value{rate, channels, 16, std::nullopt, frames, {}};
    for (std::uint64_t i = 0; i < frames; ++i)
        for (std::uint32_t c = 0; c < channels; ++c) {
            const double t = double(i) / rate;
            value.samples.push_back(std::int32_t(9000.0 * std::sin(6.283185307179586 * (440.0 + 110.0 * c) * t)
                + 3000.0 * std::sin(6.283185307179586 * 1250.0 * t)));
        }
    return value;
}
// Byte length of the MPEG-1 Layer III frame at `at` (the Info frame LAME writes first).
std::size_t Mpeg1FrameBytes(const std::vector<std::byte>& bytes, std::size_t at) {
    static constexpr std::uint32_t kbps[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static constexpr std::uint32_t rates[] = {44100, 48000, 32000};
    const auto b1 = std::uint32_t(bytes.at(at + 1)), b2 = std::uint32_t(bytes.at(at + 2));
    Check(std::uint32_t(bytes[at]) == 0xff && (b1 & 0xfe) == 0xfa && (b2 >> 4) >= 1 && (b2 >> 4) <= 14 && ((b2 >> 2) & 3) < 3,
        "expected an MPEG-1 Layer III frame header");
    return 144u * kbps[b2 >> 4] * 1000u / rates[(b2 >> 2) & 3] + ((b2 >> 1) & 1);
}
void LossyCases() {
    using audio::Container;
    const auto limits = c::StandaloneAudioLimits();
    const auto programme = Tone(48000, 2, 48000);
    const auto wav = audio::EncodeWav(programme).bytes;
    const auto flac = audio::EncodeFlac(programme).bytes;
    const auto aiff = audio::EncodeAiff(programme).bytes;
    const Container lossy[] = {Container::vorbis, Container::opus, Container::mp3};
    const Container lossless[] = {Container::wav, Container::flac, Container::aiff};
    std::vector<std::byte> encoded[7];
    // Every lossless source into every lossy format: labelled lossy, identical to the codec's
    // own encoder (the owner adds no processing), and decoding back gives the exact length.
    for (const auto to : lossy) {
        const auto direct = to == Container::vorbis ? audio::EncodeVorbis(programme, {}, limits)
            : to == Container::opus ? audio::EncodeOpus(programme, {}, limits) : audio::EncodeMp3(programme, {}, limits);
        Check(bool(direct), "lossy reference encode");
        for (const auto from : lossless) {
            const auto& source = from == Container::wav ? wav : from == Container::flac ? flac : aiff;
            const auto out = c::ConvertStandaloneAudio(from, to, source);
            Check(bool(out) && out.lossyOutput && !out.lossySource && !out.generationLoss && out.bytes == direct.bytes
                && out.sampleRate == 48000 && out.channels == 2 && out.bitsPerSample == 16 && out.frames == 48000,
                "lossless to lossy conversion or labels");
        }
        encoded[std::size_t(to)] = direct.bytes;
    }
    // Every lossy source into every format, lossy ones carrying generation loss.
    for (const auto from : lossy) {
        for (const auto to : {Container::wav, Container::flac, Container::aiff, Container::vorbis, Container::opus, Container::mp3}) {
            const auto out = c::ConvertStandaloneAudio(from, to, encoded[std::size_t(from)]);
            const bool lossyTo = to == Container::vorbis || to == Container::opus || to == Container::mp3;
            Check(bool(out) && out.lossySource && out.lossyOutput == lossyTo && out.generationLoss == lossyTo
                && out.bitsPerSample == 16 && out.sampleRate == 48000 && out.channels == 2 && out.frames == 48000 && !out.bytes.empty(),
                "lossy source conversion, length or labels");
            // Vorbis has no encoder delay: only the end may be trimmed; Opus and LAME MP3 trim both ends.
            Check(out.encoderDelayTrimmed == (from != Container::vorbis || out.trimmedTrailingFrames > 0) && !out.encoderDelayUnknown
                && (from == Container::vorbis || out.trimmedLeadingFrames > 0), "encoder delay report");
            Check(out.originalSampleRate == (from == Container::opus ? 48000u : 0u), "Opus original rate report");
        }
        const auto wavOut = c::ConvertStandaloneAudio(from, Container::wav, encoded[std::size_t(from)]);
        const auto decoded = audio::DecodeWav(wavOut.bytes);
        Check(bool(decoded) && decoded.audio.frames == 48000 && decoded.audio.bitsPerSample == 16, "lossy to WAV output");
        if (from == Container::mp3) Check(wavOut.trimmedLeadingFrames == 576 + 529, "LAME delay plus decoder delay trimmed");
        if (from == Container::opus) Check(wavOut.trimmedLeadingFrames == 312, "Opus pre-skip trimmed");
    }
    // Explicit controls: each changes the output and is accepted only for its own format.
    const auto withOptions = [&](Container to, auto set) { audio::LossyOptions o; set(o); return c::ConvertStandaloneAudio(Container::wav, to, wav, o); };
    const auto vorbisLow = withOptions(Container::vorbis, [](auto& o) { o.vorbisQuality = 0; });
    const auto vorbisHigh = withOptions(Container::vorbis, [](auto& o) { o.vorbisQuality = 100; });
    Check(vorbisLow && vorbisHigh && vorbisLow.bytes.size() < encoded[4].size() && encoded[4].size() < vorbisHigh.bytes.size(),
        "vorbis_quality does not order the output size");
    const auto opusLow = withOptions(Container::opus, [](auto& o) { o.opusBitrateKbps = 6; });
    const auto opusHigh = withOptions(Container::opus, [](auto& o) { o.opusBitrateKbps = 510; });
    Check(opusLow && opusHigh && opusLow.bytes.size() < encoded[5].size() && encoded[5].size() < opusHigh.bytes.size(),
        "opus_bitrate_kbps does not order the output size");
    // MP3 CBR floor: LAME writes its gapless Info/LAME tag only into a frame of at least
    // 4 + side information + 156 bytes. The codec rejects a smaller legal bitrate before
    // encoding with bitrate_too_low_for_gapless_tag (the tag is never dropped); the owner
    // reports it as invalid_options with the floor for this source. The floors (mono,
    // stereo) are pinned exactly against the documented table (tool descriptions, README).
    // VBR at every quality and every rate is accepted.
    struct Floor { std::uint32_t rate, mono, stereo; };
    for (const auto floor : {Floor{48000, 64, 64}, Floor{44100, 56, 64}, Floor{32000, 40, 48}, Floor{24000, 64, 64},
                             Floor{22050, 56, 56}, Floor{16000, 40, 40}, Floor{12000, 32, 32}, Floor{11025, 32, 32}, Floor{8000, 24, 24}}) {
        const bool mpeg1 = floor.rate >= 32000;
        const std::vector<std::uint32_t> legal = mpeg1 ? std::vector<std::uint32_t>{32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}
            : std::vector<std::uint32_t>{8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
        for (const std::uint32_t channels : {1u, 2u}) {
            const auto source = audio::EncodeWav(Tone(floor.rate, channels, 2400)).bytes;
            const auto minimum = channels == 1 ? floor.mono : floor.stereo;
            for (const auto kbps : legal) {
                audio::LossyOptions o; o.mp3BitrateKbps = kbps;
                const auto out = c::ConvertStandaloneAudio(Container::wav, Container::mp3, source, o);
                Check(kbps >= minimum ? bool(out) && audio::DecodeMp3(out.bytes).audio.frames == 2400
                    : out.error == c::Error::invalid_options && out.codecError == audio::Error::bitrate_too_low_for_gapless_tag
                        && out.minimumMp3BitrateKbps == minimum && out.sampleRate == floor.rate && out.channels == channels && out.bytes.empty(),
                    "MP3 CBR floor changed at " + std::to_string(floor.rate) + " Hz, " + std::to_string(channels) + " ch, "
                    + std::to_string(kbps) + " kbps");
            }
            for (std::uint32_t q = 0; q <= 9; ++q) {
                audio::LossyOptions o; o.mp3RateControl = audio::Mp3RateControl::vbr; o.mp3VbrQuality = q;
                const auto out = c::ConvertStandaloneAudio(Container::wav, Container::mp3, source, o);
                Check(bool(out) && audio::DecodeMp3(out.bytes).audio.frames == 2400, "MP3 VBR rejected at a Layer III rate");
            }
        }
    }
    const auto mp3Low = withOptions(Container::mp3, [](auto& o) { o.mp3BitrateKbps = 64; });
    const auto mp3High = withOptions(Container::mp3, [](auto& o) { o.mp3BitrateKbps = 320; });
    const auto mp3Vbr = withOptions(Container::mp3, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl::vbr; o.mp3VbrQuality = 0; });
    const auto mp3VbrDefault = withOptions(Container::mp3, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl::vbr; });
    Check(mp3Low && mp3High && mp3Vbr && mp3VbrDefault, "MP3 rate controls rejected");
    Check(mp3Low.bytes.size() < encoded[6].size() && encoded[6].size() < mp3High.bytes.size()
        && mp3Vbr.bytes != encoded[6] && mp3VbrDefault.bytes != mp3Vbr.bytes, "MP3 rate controls do not change the output");
    Check(audio::DecodeMp3(mp3Vbr.bytes).audio.frames == 48000 && audio::DecodeMp3(mp3Low.bytes).audio.frames == 48000, "MP3 option outputs keep the length");
    const auto rejected = [&](Container to, auto set, audio::Error reason, std::string_view what) {
        const auto out = withOptions(to, set);
        Check(out.error == c::Error::invalid_options && out.codecError == reason && out.bytes.empty(), what);
    };
    rejected(Container::vorbis, [](auto& o) { o.vorbisQuality = 101; }, audio::Error::invalid_option, "vorbis_quality 101 accepted");
    rejected(Container::opus, [](auto& o) { o.opusBitrateKbps = 5; }, audio::Error::invalid_option, "opus_bitrate_kbps 5 accepted");
    rejected(Container::opus, [](auto& o) { o.opusBitrateKbps = 511; }, audio::Error::invalid_option, "opus_bitrate_kbps 511 accepted");
    rejected(Container::mp3, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl::vbr; o.mp3VbrQuality = 10; },
        audio::Error::invalid_option, "mp3_vbr_quality 10 accepted");
    rejected(Container::mp3, [](auto& o) { o.mp3BitrateKbps = 200; }, audio::Error::invalid_option, "illegal MP3 bitrate accepted");
    rejected(Container::mp3, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl(3); }, audio::Error::invalid_option, "unknown MP3 rate control");
    rejected(Container::mp3, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl::vbr; o.mp3BitrateKbps = 128; },
        audio::Error::none, "mp3_bitrate_kbps accepted with VBR");
    rejected(Container::mp3, [](auto& o) { o.mp3VbrQuality = 4; }, audio::Error::none, "mp3_vbr_quality accepted with CBR");
    rejected(Container::mp3, [](auto& o) { o.vorbisQuality = 20; }, audio::Error::none, "vorbis option accepted for MP3");
    rejected(Container::vorbis, [](auto& o) { o.opusBitrateKbps = 64; }, audio::Error::none, "opus option accepted for Vorbis");
    rejected(Container::opus, [](auto& o) { o.mp3RateControl = audio::Mp3RateControl::vbr; }, audio::Error::none, "mp3 option accepted for Opus");
    rejected(Container::flac, [](auto& o) { o.mp3BitrateKbps = 128; }, audio::Error::none, "mp3 option accepted for FLAC");
    rejected(Container::wav, [](auto& o) { o.vorbisQuality = 0; }, audio::Error::none, "vorbis option accepted for WAV");
    // MPEG-2 (16 kHz) defines 8-160 kbps: 192 (the default) and 320 are illegal there.
    const auto narrow = audio::EncodeWav(Tone(16000, 1, 16000)).bytes;
    const auto narrowMp3 = c::ConvertStandaloneAudio(Container::wav, Container::mp3, narrow);
    Check(narrowMp3.error == c::Error::invalid_options && narrowMp3.codecError == audio::Error::invalid_option, "192 kbps at 16 kHz accepted");
    audio::LossyOptions mpeg2; mpeg2.mp3BitrateKbps = 64;
    const auto narrowOk = c::ConvertStandaloneAudio(Container::wav, Container::mp3, narrow, mpeg2);
    Check(bool(narrowOk) && audio::DecodeMp3(narrowOk.bytes).audio.frames == 16000, "MPEG-2 MP3 at 64 kbps");
    // No resampling: 44.1 kHz is not an Opus rate; MP3 and Vorbis encode it at its own rate.
    const auto cd = audio::EncodeWav(Tone(44100, 2, 44100)).bytes;
    const auto cdOpus = c::ConvertStandaloneAudio(Container::wav, Container::opus, cd);
    Check(cdOpus.error == c::Error::encode_failed && cdOpus.codecError == audio::Error::resampling_required && cdOpus.bytes.empty(),
        "44.1 kHz to Opus not rejected as resampling_required");
    const auto cdMp3 = c::ConvertStandaloneAudio(Container::wav, Container::mp3, cd);
    const auto cdBack = c::ConvertStandaloneAudio(Container::mp3, Container::wav, cdMp3.bytes);
    Check(cdMp3 && cdBack && cdBack.frames == 44100 && cdBack.sampleRate == 44100 && cdBack.encoderDelayTrimmed, "44.1 kHz MP3 round trip length");
    const auto cdVorbis = c::ConvertStandaloneAudio(Container::wav, Container::vorbis, cd);
    Check(cdVorbis && c::ConvertStandaloneAudio(Container::vorbis, Container::flac, cdVorbis.bytes).frames == 44100, "44.1 kHz Vorbis round trip length");
    Check(c::ConvertStandaloneAudio(Container::wav, Container::mp3, audio::EncodeWav(Tone(96000, 2, 960)).bytes).codecError
        == audio::Error::resampling_required, "96 kHz to MP3 not rejected");
    // Opus from 16 kHz decodes at 48 kHz with three times the frames and reports the input rate.
    const auto wide = c::ConvertStandaloneAudio(Container::wav, Container::opus, narrow);
    const auto wideBack = c::ConvertStandaloneAudio(Container::opus, Container::wav, wide.bytes);
    Check(wide && wideBack && wideBack.sampleRate == 48000 && wideBack.frames == 48000 && wideBack.originalSampleRate == 16000,
        "16 kHz Opus decode rate, length or original rate");
    // Encoding is 1-2 channels; multichannel Vorbis/Opus is decode-only.
    const auto surround = audio::EncodeWav(Tone(48000, 6, 480)).bytes;
    for (const auto to : lossy) {
        const auto out = c::ConvertStandaloneAudio(Container::wav, to, surround);
        Check(out.error == c::Error::encode_failed && out.codecError == audio::Error::channel_limit, "6-channel lossy encode accepted");
    }
    // Chained Ogg is rejected explicitly.
    for (const auto from : {Container::vorbis, Container::opus}) {
        auto chained = encoded[std::size_t(from)];
        chained.insert(chained.end(), encoded[std::size_t(from)].begin(), encoded[std::size_t(from)].end());
        const auto out = c::ConvertStandaloneAudio(from, Container::wav, chained);
        Check(out.error == c::Error::decode_failed && out.codecError == audio::Error::chained_stream, "chained Ogg not rejected");
    }
    // An ID3v2 tag is skipped and counted; samples are identical.
    std::vector<std::byte> tagged;
    for (const int b : std::initializer_list<int>{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 13, 'T', 'I', 'T', '2', 0, 0, 0, 3, 0, 0, 0, 'h', 'i'})
        tagged.push_back(std::byte(b));
    tagged.insert(tagged.end(), encoded[6].begin(), encoded[6].end());
    const auto withTag = c::ConvertStandaloneAudio(Container::mp3, Container::wav, tagged);
    Check(withTag && withTag.tagsSkipped == 1 && withTag.skippedMetadata == 1
        && withTag.bytes == c::ConvertStandaloneAudio(Container::mp3, Container::wav, encoded[6]).bytes, "ID3v2 tag not skipped and counted");
    // Without LAME's Info frame nothing is trimmed: encoder_delay_unknown, whole frames decoded.
    const std::vector<std::byte> untagged(encoded[6].begin() + std::ptrdiff_t(Mpeg1FrameBytes(encoded[6], 0)), encoded[6].end());
    const auto raw = c::ConvertStandaloneAudio(Container::mp3, Container::wav, untagged);
    Check(raw && raw.encoderDelayUnknown && !raw.encoderDelayTrimmed && raw.frames > 48000 + 1105 && raw.frames % 1152 == 0,
        "MP3 without a LAME tag: delay must be reported unknown and nothing trimmed");
    // Mislabelled input names the codec's reason.
    Check(c::ConvertStandaloneAudio(Container::opus, Container::wav, encoded[4]).codecError == audio::Error::wrong_magic, "Vorbis passed as Opus");
    Check(c::ConvertStandaloneAudio(Container::mp3, Container::wav, wav).error == c::Error::decode_failed, "WAV passed as MP3");
    // Determinism through the owner.
    Check(c::ConvertStandaloneAudio(Container::flac, Container::mp3, flac).bytes == encoded[6]
        && c::ConvertStandaloneAudio(Container::wav, Container::opus, wav).bytes == encoded[5], "lossy output not deterministic");
}
void TextDocumentCases() {
    using document::OutputFormat; using c::DocumentInput;
    const auto convert = [](DocumentInput input, OutputFormat output, const std::string& bytes, bool notes = false) {
        return c::ConvertStandaloneDocument({input, output, notes}, docx_fixture::Bytes(bytes));
    };
    const auto pptx = package_fixture::Pptx({{package_fixture::PptxShape("<p:ph type=\"title\"/>", package_fixture::PptxPara("Plan"))
        + package_fixture::PptxShape("<p:ph idx=\"1\"/>", package_fixture::PptxPara("First") + package_fixture::PptxPara("Sub", "<a:pPr lvl=\"1\"/>"))
        + "<p:pic><p:nvPicPr><p:cNvPr id=\"5\" name=\"p\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill/><p:spPr/></p:pic>",
        package_fixture::PptxPara("Say hi")}});
    const auto slides = convert(DocumentInput::pptx, OutputFormat::text, pptx);
    Check(bool(slides) && Text(slides.bytes) == "Slide 1\n\nPlan\n\n- First\n  - Sub\n", "pptx_to_txt bytes: " + Text(slides.bytes));
    Check(slides.report.slides == 1 && slides.report.excluded.images == 1 && slides.report.excluded.notes == 1
        && slides.report.notesIncluded == 0 && slides.headings == 2 && slides.listItems == 2, "pptx report");
    const auto slidesMd = convert(DocumentInput::pptx, OutputFormat::markdown, pptx, true);
    Check(bool(slidesMd) && Text(slidesMd.bytes) == "# Slide 1\n\n## Plan\n\n- First\n  - Sub\n\n**Notes:** Say hi\n",
        "pptx_to_markdown with notes: " + Text(slidesMd.bytes));
    Check(slidesMd.report.notesIncluded == 1 && slidesMd.report.excluded.notes == 0, "pptx notes report");
    const auto odt = package_fixture::Odt("<text:h text:outline-level=\"1\">Title</text:h>"
        "<text:p>Body <text:a xlink:href=\"https://e.test/\">link</text:a><text:note text:note-class=\"footnote\"><text:note-citation>1</text:note-citation>"
        "<text:note-body><text:p>Foot</text:p></text:note-body></text:note></text:p>");
    const auto body = convert(DocumentInput::odt, OutputFormat::markdown, odt);
    Check(bool(body) && Text(body.bytes) == "# Title\n\nBody [link](https://e.test/)\n" && body.report.excluded.notes == 1,
        "odt_to_markdown: " + Text(body.bytes));
    Check(Text(convert(DocumentInput::odt, OutputFormat::text, odt).bytes) == "Title\n\nBody link (https://e.test/)\n", "odt_to_txt");
    const auto odp = package_fixture::Odp("<draw:page draw:name=\"p1\"><draw:frame presentation:class=\"title\"><draw:text-box><text:p>Hello</text:p>"
        "</draw:text-box></draw:frame><presentation:notes><draw:frame presentation:class=\"notes\"><draw:text-box><text:p>Psst</text:p>"
        "</draw:text-box></draw:frame></presentation:notes></draw:page>");
    const auto pages = convert(DocumentInput::odp, OutputFormat::text, odp, true);
    Check(bool(pages) && Text(pages.bytes) == "Slide 1\n\nHello\n\nNotes: Psst\n" && pages.report.slides == 1, "odp_to_txt: " + Text(pages.bytes));
    const auto epub = package_fixture::Epub({"<h1>Intro</h1><blockquote><p>Q</p></blockquote><pre><code class=\"language-sh\">ls -l</code></pre>"
        "<hr/><p>End <img src=\"a.png\" alt=\"x\"/></p><script>x()</script>"});
    const auto book = convert(DocumentInput::epub, OutputFormat::markdown, epub);
    Check(bool(book) && Text(book.bytes) == "# Intro\n\n> Q\n\n```sh\nls -l\n```\n\n---\n\nEnd\n", "epub_to_markdown: " + Text(book.bytes));
    Check(book.blockQuotes == 1 && book.codeBlocks == 1 && book.thematicBreaks == 1 && book.report.chapters == 1
        && book.report.excluded.images == 1 && book.report.excluded.scripts == 1, "epub report");
    Check(Text(convert(DocumentInput::epub, OutputFormat::text, epub).bytes) == "Intro\n\nQ\n\nls -l\n\n\nEnd\n", "epub_to_txt");
    // Typed rejections carry the extractor's reason.
    const auto encrypted = convert(DocumentInput::odt, OutputFormat::text, package_fixture::EncryptedOdt());
    Check(encrypted.error == c::Error::decode_failed && encrypted.textDocumentError == document::TextDocumentError::odf_encrypted
        && encrypted.bytes.empty(), "encrypted ODT");
    Check(convert(DocumentInput::epub, OutputFormat::text, package_fixture::DrmEpub()).textDocumentError == document::TextDocumentError::epub_drm,
        "DRM EPUB");
    Check(convert(DocumentInput::odt, OutputFormat::text, package_fixture::Ods("<table:table table:name=\"T\"/>")).textDocumentError
        == document::TextDocumentError::odf_wrong_kind, "ODS given as ODT");
    Check(convert(DocumentInput::pptx, OutputFormat::text, docx_fixture::EncryptedDocx()).textDocumentError
        == document::TextDocumentError::pptx_encrypted, "encrypted PPTX");
    Check(convert(DocumentInput::odt, OutputFormat::text, odt, true).error == c::Error::invalid_options, "notes option for ODT");
    Check(convert(DocumentInput::epub, OutputFormat::text, epub, true).error == c::Error::invalid_options, "notes option for EPUB");
    Check(c::ConvertStandaloneDocument({DocumentInput(7), OutputFormat::text, false}, docx_fixture::Bytes(odt)).error
        == c::Error::input_format_unavailable, "unknown document input");
    const auto settings = c::StandaloneTextDocumentOptions(false);
    Check(settings.extract.zip.maximumEntryExpandedBytes == c::maximumDocumentEntryExpandedBytes && settings.maximumItems == 4096
        && settings.maximumTableCells == 1'000'000 && !settings.includeNotes, "text document limits");
    // A part above the 16 MiB per-member bound is an input limit.
    const auto large = convert(DocumentInput::odt, OutputFormat::text,
        package_fixture::Odt("<text:p>" + std::string(c::maximumDocumentEntryExpandedBytes, 'x') + "</text:p>"));
    Check(large.error == c::Error::input_limit && large.textDocumentError == document::TextDocumentError::zip_size_limit, "ODT member bound");
}
c::TableOptions T(tabular::TableFormat input, tabular::TableFormat output) {
    c::TableOptions options; options.input = input; options.output = output; return options;
}
c::MarkupOptions M(markup::Format input, markup::Format output) {
    c::MarkupOptions options; options.input = input; options.output = output; return options;
}
void TableCases() {
    using tabular::TableFormat;
    const auto convert = [](c::TableOptions options, const std::string& bytes) {
        return c::ConvertStandaloneTable(options, docx_fixture::Bytes(bytes));
    };
    const std::string csv = "name,qty\nwidget,2\n\"a,b\",3\n";
    const auto arrays = convert(T(TableFormat::csv, TableFormat::json), csv);
    Check(bool(arrays) && Text(arrays.bytes) == "[\n[\"name\",\"qty\"],\n[\"widget\",2],\n[\"a,b\",3]\n]\n", "csv_to_json arrays: " + Text(arrays.bytes));
    Check(arrays.report.rows == 3 && arrays.report.columns == 2 && arrays.report.raggedRows == 0, "csv report");
    auto objects = T(TableFormat::csv, TableFormat::json); objects.json = tabular::JsonShape::objects;
    const auto keyed = convert(objects, csv);
    Check(bool(keyed) && Text(keyed.bytes) == "[\n{\"name\":\"widget\",\"qty\":2},\n{\"name\":\"a,b\",\"qty\":3}\n]\n", "csv_to_json objects: " + Text(keyed.bytes));
    Check(Text(convert(T(TableFormat::csv, TableFormat::tsv), csv).bytes) == "name\tqty\nwidget\t2\na,b\t3\n", "csv_to_tsv");
    auto semicolon = T(TableFormat::json, TableFormat::csv); semicolon.delimiter = ';';
    Check(Text(convert(semicolon, Text(arrays.bytes)).bytes) == "name;qty\nwidget;2\na,b;3\n", "json_to_csv with ;");
    const auto ragged = convert(T(TableFormat::csv, TableFormat::tsv), "a,b,c\n1\n");
    Check(bool(ragged) && ragged.report.raggedRows == 1 && Text(ragged.bytes) == "a\tb\tc\n1\t\t\n", "ragged rows padded and reported");
    // Workbooks: first visible sheet by default, index or name on request; formulas and merges reported.
    const auto xlsx = package_fixture::Xlsx({{"Hidden", "hidden", "<sheetData><row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>secret</t></is></c></row></sheetData>"},
        {"Data", "", "<sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
                     "<row r=\"2\"><c r=\"A2\"><v>1.5</v></c><c r=\"B2\"><f>A2*2</f><v>3</v></c></row></sheetData>"
                     "<mergeCells count=\"1\"><mergeCell ref=\"A1:B1\"/></mergeCells>"}}, {"x", "y"});
    const auto sheet = convert(T(TableFormat::xlsx, TableFormat::csv), xlsx);
    Check(bool(sheet) && Text(sheet.bytes) == "x,y\n1.5,3\n", "xlsx_to_csv: " + Text(sheet.bytes));
    Check(sheet.report.sheets.size() == 2 && sheet.report.selectedSheet == 1 && sheet.report.formulas == 1
        && sheet.report.mergedRanges.size() == 1 && sheet.report.sheets[0].state == tabular::SheetState::hidden, "xlsx report");
    auto byIndex = T(TableFormat::xlsx, TableFormat::csv); byIndex.sheet = {tabular::SheetSelection::index, 0, {}};
    Check(Text(convert(byIndex, xlsx).bytes) == "secret\n", "hidden sheet by index");
    auto byName = T(TableFormat::xlsx, TableFormat::json); byName.sheet = {tabular::SheetSelection::name, 0, "Data"};
    Check(Text(convert(byName, xlsx).bytes) == "[\n[\"x\",\"y\"],\n[1.5,3]\n]\n", "sheet by name");
    byName.sheet.name = "Nope";
    const auto missing = convert(byName, xlsx);
    Check(missing.error == c::Error::decode_failed && missing.tableError == tabular::Error::sheet_not_found, "missing sheet");
    const auto ods = package_fixture::Ods("<table:table table:name=\"S\"><table:table-row><table:table-cell office:value-type=\"string\"><text:p>k</text:p>"
        "</table:table-cell><table:table-cell office:value-type=\"float\" office:value=\"7\"><text:p>7</text:p></table:table-cell></table:table-row></table:table>");
    const auto spreadsheet = convert(T(TableFormat::ods, TableFormat::csv), ods);
    Check(bool(spreadsheet) && Text(spreadsheet.bytes) == "k,7\n" && spreadsheet.report.sheets.size() == 1, "ods_to_csv: " + Text(spreadsheet.bytes));
    // Rejections: non-table JSON, macro-enabled and encrypted workbooks, options for another format.
    const auto notTable = convert(T(TableFormat::json, TableFormat::csv), "{\"a\":[1]}");
    Check(notTable.error == c::Error::decode_failed && notTable.tableError == tabular::Error::json_not_table, "non-table JSON");
    const auto macro = convert(T(TableFormat::xlsx, TableFormat::csv),
        package_fixture::Xlsx({{"S", "", "<sheetData/>"}}, {}, "application/vnd.ms-excel.sheet.macroEnabled.main+xml"));
    Check(macro.tableError == tabular::Error::package_macro_enabled, "macro-enabled XLSX");
    Check(convert(T(TableFormat::xlsx, TableFormat::csv), docx_fixture::EncryptedDocx()).tableError == tabular::Error::package_encrypted, "encrypted XLSX");
    Check(convert(T(TableFormat::csv, TableFormat::csv), csv).error == c::Error::invalid_options, "identical formats");
    auto bad = T(TableFormat::json, TableFormat::tsv); bad.delimiter = ';';
    Check(convert(bad, "[[1]]").error == c::Error::invalid_options, "delimiter without a CSV side");
    bad = T(TableFormat::csv, TableFormat::tsv); bad.json = tabular::JsonShape::objects;
    Check(convert(bad, csv).error == c::Error::invalid_options, "json shape without JSON output");
    bad = T(TableFormat::csv, TableFormat::json); bad.sheet = {tabular::SheetSelection::index, 1, {}};
    Check(convert(bad, csv).error == c::Error::invalid_options, "sheet selector for CSV");
    bad = T(TableFormat::csv, TableFormat::json); bad.delimiter = '|';
    Check(convert(bad, csv).error == c::Error::invalid_options, "unknown delimiter");
    Check(convert(T(TableFormat::xlsx, TableFormat::xlsx), xlsx).error == c::Error::output_format_unavailable, "XLSX output");
    // Limits are explicit and the cell cap is enforced before allocation.
    const auto read = c::StandaloneTableOptions({}).read;
    Check(read.maximumSourceBytes == c::maximumTableSourceBytes && read.table.maximumCells == 2'000'000
        && read.zip.maximumEntryExpandedBytes == c::maximumTableEntryExpandedBytes
        && c::StandaloneTableOptions({}).write.maximumOutputBytes == c::maximumTableOutputBytes, "table limits");
    const auto far = convert(T(TableFormat::xlsx, TableFormat::csv),
        package_fixture::Xlsx({{"S", "", "<sheetData><row r=\"1048576\"><c r=\"XFD1048576\"><v>1</v></c></row></sheetData>"}}));
    Check(far.error == c::Error::input_limit && far.tableError == tabular::Error::cell_limit, "dense cell cap");
    std::vector<std::byte> over(c::maximumTableSourceBytes + 1);
    Check(c::ConvertStandaloneTable({}, over).error == c::Error::input_limit, "oversized table");
}
void MarkupCases() {
    using markup::Format;
    const auto convert = [](c::MarkupOptions options, std::string_view text) {
        return c::ConvertStandaloneMarkup(options, docx_fixture::Bytes(text));
    };
    const auto page = convert(M(Format::html, Format::markdown),
        "<nav><a href=\"/\">Home</a></nav><h1>T</h1><p>a <b>b</b> <a href=\"javascript:x()\">j</a></p><script>s()</script>");
    Check(bool(page) && Text(page.bytes) == "Home\n\n# T\n\na **b** j\n", "html_to_markdown: " + Text(page.bytes));
    Check(page.html.scriptStyleDropped == 1 && page.html.linksAsText == 2, "html report");
    auto noNav = M(Format::html, Format::markdown); noNav.dropNavigation = true;
    const auto article = convert(noNav, "<nav><a href=\"/\">Home</a></nav><h1>T</h1>");
    Check(bool(article) && Text(article.bytes) == "# T\n" && article.html.navigationDropped == 1, "drop navigation: " + Text(article.bytes));
    const auto charset = convert(M(Format::html, Format::markdown), "<meta charset=\"windows-1252\"><p>x");
    Check(charset.error == c::Error::decode_failed && charset.markupError == markup::Error::unsupported_charset, "charset-declared HTML");
    const auto html = convert(M(Format::markdown, Format::html), "# T\n\n[ok](https://e.test/) [bad](javascript:x) <b>raw</b>\n");
    Check(bool(html) && html.markdown.links == 2 && html.markdown.unsafeLinks == 1 && html.markdown.htmlEscaped == 2, "markdown report: links "
        + std::to_string(html.markdown.links) + " unsafe " + std::to_string(html.markdown.unsafeLinks) + " escaped "
        + std::to_string(html.markdown.htmlEscaped) + " " + Text(html.bytes));
    Check(Text(html.bytes).find("<script") == std::string::npos && Text(html.bytes).find("javascript:") == std::string::npos
        && Text(html.bytes).find("&lt;b&gt;raw") != std::string::npos, "markdown_to_html is inert: " + Text(html.bytes));
    auto full = M(Format::markdown, Format::html); full.fullDocument = true;
    Check(Text(convert(full, "x").bytes).rfind("<!DOCTYPE html>", 0) == 0, "full document");
    const std::string_view srt = "1\n00:00:01,000 --> 00:00:02,500\nHello, world.\n\n";
    const auto vtt = convert(M(Format::srt, Format::vtt), srt);
    Check(bool(vtt) && Text(vtt.bytes) == "WEBVTT\n\n00:00:01.000 --> 00:00:02.500\nHello, world.\n\n" && vtt.subtitles.cues == 1, "srt_to_vtt: " + Text(vtt.bytes));
    const auto back = convert(M(Format::vtt, Format::srt), "WEBVTT\n\nid\n00:00:01.000 --> 00:00:02.500 align:start\nHello, world.\n\n");
    Check(bool(back) && Text(back.bytes) == srt && back.subtitles.settingsDropped == 1 && back.subtitles.identifiersDropped == 1,
        "vtt_to_srt: " + Text(back.bytes));
    const auto malformed = convert(M(Format::srt, Format::vtt), "1\n00:00:01,000 --> nope\nx\n\n");
    Check(malformed.error == c::Error::decode_failed && malformed.markupError == markup::Error::subtitle_malformed_timestamp && malformed.line == 2,
        "malformed SRT timestamp");
    Check(convert(M(Format::html, Format::srt), "x").error == c::Error::output_format_unavailable, "html -> srt admitted");
    Check(convert(M(Format::srt, Format::srt), srt).error == c::Error::output_format_unavailable, "srt -> srt admitted");
    auto misuse = M(Format::srt, Format::vtt); misuse.dropNavigation = true;
    Check(convert(misuse, srt).error == c::Error::invalid_options, "drop_navigation for subtitles");
    misuse = M(Format::html, Format::markdown); misuse.fullDocument = true;
    Check(convert(misuse, "x").error == c::Error::invalid_options, "full_document for Markdown output");
    std::vector<std::byte> over(c::maximumMarkupSourceBytes + 1, std::byte{'a'});
    Check(c::ConvertStandaloneMarkup(M(Format::markdown, Format::html), over).error == c::Error::input_limit, "oversized markup");
    // Output beyond 8 MiB is rejected, never cut: 4 MiB of '<' becomes 16 MiB of "&lt;".
    const auto grown = c::ConvertStandaloneMarkup(M(Format::markdown, Format::html),
        std::vector<std::byte>(4u * 1024u * 1024u, std::byte{'<'}));
    Check(grown.error == c::Error::encode_failed && grown.markupError == markup::Error::output_limit && grown.bytes.empty(), "markup output limit");
}
}
int main() {
    try {
        AudioCases();
        DocumentCases();
        AiffCases();
        LossyCases();
        TextDocumentCases();
        TableCases();
        MarkupCases();
        std::cout << "standalone audio, document, table and markup conversion cases passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
