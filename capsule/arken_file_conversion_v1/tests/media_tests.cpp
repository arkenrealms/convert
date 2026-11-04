// Standalone audio and document owners: exact sample preservation, reported metadata,
// explicit limits, typed rejections and the codec owner's reason on every failure.
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace c = arken::file_conversion::v1;
namespace audio = arken::audio::v1;
namespace document = arken::document::v1;
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
}
}
int main() {
    try {
        AudioCases();
        DocumentCases();
        std::cout << "standalone audio and document conversion cases passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
