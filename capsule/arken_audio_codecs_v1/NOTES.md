# arken_audio_codecs_v1

Status: native codec milestone (handoff sections 9 and 10, plan-task d85e2c6cc4a2f945).
It covers the WAV, FLAC and AIFF/AIFF-C codec owner, the lossy Ogg Vorbis,
Ogg Opus and MP3 codecs, and their native qualification.
The lossy contract is in "Lossy codecs" below.

Standalone MCP: exposed as `convert_audio` (`wav`, `flac`, `aiff`, `vorbis`,
`opus`, `mp3`), `wav_to_flac`, `flac_to_wav`, `aiff_to_wav`, `aiff_to_flac`,
`wav_to_aiff`, `flac_to_aiff` and (commands 49-55, `audio_kind` vorbis=4,
opus=5, mp3=6) `wav_to_mp3`, `mp3_to_wav`, `wav_to_opus`, `opus_to_wav`,
`wav_to_vorbis`, `vorbis_to_wav` and `flac_to_mp3`, with the `LossyOptions`
controls as `vorbis_quality`, `opus_bitrate_kbps`, `mp3_rate_control`,
`mp3_bitrate_kbps` and `mp3_vbr_quality` (defaults unchanged), through
`ConvertStandaloneAudio` in `arken_file_conversion_v1` (component
`standalone`) with its own profile: 64 MiB source and output, 32 Mi samples,
1-8 channels, up to 384 kHz, 8 MiB / 256 metadata blocks. The "remaining steps"
section at the end is kept as the integration record; the integrated profile
differs from its suggested 8 MiB figures. AIFF uses the same profile; the
result reports `channel_mask_dropped` and counts skipped AIFF chunks in
`skipped_metadata` (commands 45-48; `audio_kind` aiff=3). The lossy formats use
the same profile, where the 32 Mi sample budget is the binding limit (about
6 min 20 s of 44.1 kHz stereo); the integration differs from the steps below
in two ways: each encoder tool has its own request model carrying only its
format's controls (`vorbis_file_request`, `opus_file_request`,
`mp3_file_request`) instead of one `lossy_file_request`, and the result also
reports `trimmed_leading_frames`, `trimmed_trailing_frames` and
`silenced_frames`. Integration finding, fixed in the codec: LAME 3.100
(`InitVbrTag`) writes its Info/LAME tag only into a CBR frame of at least
`sideinfo_len` (4-byte header + 17/32 MPEG-1, 9/17 MPEG-2/2.5 side information
bytes, mono/stereo) + 156 bytes, and otherwise silently omits it, which
previously surfaced as `codec_failed` from the empty `lame_get_lametag_frame`.
`EncodeMp3` now applies the same frame-size arithmetic before encoding and
rejects such a bitrate with the typed `bitrate_too_low_for_gapless_tag`, whose
message names the minimum CBR bitrate per rate and channel count (mono/stereo
kbps: 48 kHz 64/64, 44.1 kHz 56/64, 32 kHz 40/48, 24 kHz 64/64, 22.05 kHz
56/56, 16 kHz 40/40, 12 and 11.025 kHz 32/32, 8 kHz 24/24;
`Mp3MinimumCbrBitrateKbps(rate, channels)` returns it). The tag is never
dropped. `CompressedAudio.Mp3GaplessFloor` checks every legal bitrate on both
sides of the boundary at all nine rates and both channel counts; the
conversion owner reports the floor for the source and the stdio proof checks
the typed reason. Browser execution, a UI and
a streaming file path are still not provided and need separate qualification.

## Supported profile

Every entry point (`api.hpp`) takes one complete in-memory byte span or an
`Audio` value and returns a complete result or a typed `Error`. The codec opens
no files, uses no stdio, starts no threads and keeps no global state.

Canonical model (`Audio`): interleaved `int32` samples, right-justified and
sign-extended, `bitsPerSample` significant bits (4..32), sample rate, channel
count, frame count and an optional raw WAVE_FORMAT_EXTENSIBLE `channelMask`.
Integer PCM is carried exactly; nothing is dithered, rescaled, resampled,
downmixed or reordered (the lossy formats are the documented exception: see
"Lossy codecs" for their 16-bit output and the Vorbis/Opus multichannel
order mapping). 8-bit WAV's unsigned storage (and AIFF-C `raw `)
maps to signed by subtracting 128; 8-bit AIFF is already signed.

WAV read (`DecodeWav`):

- RIFF/WAVE, little-endian. `fmt ` as WAVE_FORMAT_PCM (`0x0001`, 16/18+ bytes)
  or WAVE_FORMAT_EXTENSIBLE (`0xFFFE`, >= 40 bytes, cbSize >= 22) with the
  KSDATAFORMAT_SUBTYPE_PCM GUID.
- Containers of 8, 16, 24 and 32 bits; EXTENSIBLE `wValidBitsPerSample` from 4
  up to the container width. Padding bits below the valid bits must be zero.
- The channel mask is kept bit-for-bit (including 0 and bits beyond the
  channel count). WAVE_FORMAT_PCM yields no mask (`std::nullopt`).
- Other chunks (LIST, fact, bext, id3, cue, ...) are skipped within the
  metadata limits and counted in `DecodeReport::skippedChunks`.

WAV write (`EncodeWav`): only `fmt ` and `data`. WAVE_FORMAT_PCM with a 16-byte
`fmt ` when no mask is declared and `bitsPerSample` is 8/16/24/32; otherwise
WAVE_FORMAT_EXTENSIBLE with the container rounded up to a multiple of 8, the
valid bits, and the declared mask (0 when none was declared, because
EXTENSIBLE must carry one; 0 means "no speaker assignment" and invents no
positions). Odd data sizes get the RIFF pad byte.

FLAC read (`DecodeFlac`): native FLAC streams through
`FLAC__stream_decoder_init_stream` with memory read/EOF callbacks.

- A bounded pre-scan of the metadata chain runs before libFLAC allocates:
  optional ID3v2 prefix (syncsafe, counted as one block), `fLaC`, STREAMINFO
  first and exactly 34 bytes, block type 127 rejected, block size and count
  limits, and STREAMINFO parameters checked against the limits.
- libFLAC responds only to STREAMINFO and VORBIS_COMMENT. Every other block
  (SEEKTABLE, APPLICATION, CUESHEET, PICTURE, ...) is skipped and counted in
  `ignoredMetadataBlocks`; comments other than the channel mask set
  `ignoredComments`. Tags, pictures and cue sheets are therefore **not**
  carried into WAV.
- Any decoder error callback (lost sync, bad header, frame CRC mismatch,
  unparseable stream, bad metadata, out of bounds) rejects the whole input.
- Every frame must match STREAMINFO channels, bits and rate; every sample must
  fit the declared bits. A known STREAMINFO total must equal the decoded frame
  count; an unknown total (0) is accepted but bounded by `maximumTotalSamples`
  while decoding. MD5 checking is on; a non-zero STREAMINFO MD5 that disagrees
  is `checksum_mismatch`.
- The channel mask is read from a `WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x...`
  comment (case-insensitive field name, 1-8 hex digits), the convention of the
  reference `flac` tool. Malformed or duplicate values are `invalid_metadata`.

AIFF read (`DecodeAiff`):

- `FORM`/`AIFF`, or `FORM`/`AIFC` whose COMM `compressionType` is `NONE` or
  `twos` (big-endian PCM), `sowt` (little-endian PCM, Apple/QuickTime) or
  `raw ` (unsigned 8-bit offset binary; any other width is
  `unsupported_bit_depth`). `fl32`/`FL32`/`fl64`/`FL64` are
  `unsupported_float` (never quantized); `ulaw`, `alaw`, `ima4`, `MAC3`,
  `GSM `, `in24`, lowercase `none` and every other type are
  `unsupported_encoding`. The FORM type must be exactly `AIFF` or `AIFC`.
- COMM: 18 bytes for AIFF (longer is tolerated), at least 22 for AIFC; an
  AIFC `compressionName` Pascal string, when present, must fit the chunk.
  `numChannels` 1..`maximumChannels` (signed field; <= 0 is `channel_limit`),
  `sampleSize` 4..32 (AIFF permits 1..32; 1..3 cannot be represented by the
  canonical model and are `unsupported_bit_depth`), container =
  ceil(sampleSize / 8) bytes, samples left-justified, padding bits below
  `sampleSize` must be zero (`invalid_sample_padding`, as for WAV).
- `sampleRate` is the 80-bit IEEE 754 extended value
  (significand x 2^(exponent - 16383 - 63)). It must be an exact positive
  integer: fractions (44100.5), values below 1, negative values, NaN and
  infinity are `unsupported_sample_rate`; zero, values of 2^32 or more and
  values outside `[minimumSampleRate, maximumSampleRate]` are
  `sample_rate_limit`. Nothing is rounded. An unnormal significand (integer
  bit clear) is accepted when its exact value is an integer.
- SSND: `offset` leading bytes are skipped (any content); `blockSize` is the
  writer's alignment hint and does not move samples. The remaining SSND bytes
  must equal `numSampleFrames x channels x container` exactly, otherwise
  `invalid_header` (frame-count mismatch, partial frame, offset past data).
- COMM and SSND may appear in either order (AIFF 1.3). Missing or duplicate
  COMM, duplicate SSND, or missing SSND while COMM declares frames is
  `invalid_layout`; SSND may be absent only when `numSampleFrames` is 0
  (as AIFF permits), giving empty audio.
- Every other chunk (`FVER`, `MARK`, `INST`, `COMT`, `NAME`, `AUTH`, `(c) `,
  `ANNO`, `APPL`, `ID3 `, `MIDI`, `AESD`, ...) is skipped within the metadata
  limits and counted in `DecodeReport::skippedChunks`. Chunk pad bytes are
  required; bytes after the FORM extent are `invalid_layout`.
- `channelMask` is always `std::nullopt`: AIFF has no mask field. Channel
  order is the file's order and is never remapped.

AIFF write (`EncodeAiff`): `FORM`/`AIFF` with exactly two chunks, COMM (18
bytes: channels, frames, the real `bitsPerSample` as `sampleSize`, the exact
extended rate) and SSND (offset 0, blockSize 0, big-endian samples
left-justified in the next multiple-of-8 container, pad byte when the data
size is odd). The extended rate of a positive integer is exact: exponent
16383 + floor(log2 rate), significand = rate << (63 - floor(log2 rate)).
`frames` must fit 32 bits and the FORM size 2^31 - 1 (AIFF sizes are signed),
otherwise `output_limit`. AIFF cannot store a channel mask: `EncodeAiff`
drops it, and `Convert` sets `ConvertResult::droppedChannelMask` when the
decoded source declared one (WAV EXTENSIBLE, or FLAC's mask comment).

FLAC write (`EncodeFlac`), see "Determinism" for the exact configuration.
1..8 channels, 4..32 bits, 1..1,048,575 Hz (FLAC__MAX_SAMPLE_RATE), at most
2^36-1 frames.

`Convert(source, destination, bytes, [options,] limits)` composes decode and
encode under one `Limits` for any pair of `wav`, `flac`, `aiff`, `vorbis`,
`opus` and `mp3` (including same-container re-encoding) and reports the
decode issue, encode error, stream parameters (of the decoded audio: an Opus
source reports 48 kHz), `DecodeReport`, `droppedChannelMask`, `lossyOutput`
(destination is Vorbis/Opus/MP3) and `generationLoss` (source and destination
both lossy). An unknown container value is rejected (`encodeError =
invalid_audio`). `LossyOptions` defaults apply when no options are given.

## Lossy codecs (Vorbis in Ogg, Opus in Ogg, MP3)

"OGG" is a container, not a codec: the API never offers an ambiguous Ogg
choice. `Container::vorbis` is Vorbis I in Ogg (`audio/ogg; codecs=vorbis`,
`.ogg`), `Container::opus` is Opus in Ogg per RFC 7845 (`audio/ogg;
codecs=opus`, `.opus`), `Container::mp3` is an MPEG-1/2/2.5 Layer III
elementary stream (`audio/mpeg`, `.mp3`). Lossy output is never described as
reversible: `ConvertResult::lossyOutput` labels every conversion into a lossy
format and `generationLoss` every lossy-to-lossy conversion (quality is never
restored); `DecodeReport::lossy` labels every lossy source.

### Decoding (all three)

- Output is canonical 16-bit PCM (`bitsPerSample = 16`). Float codec output
  (Vorbis, Opus) is converted by one pure function: `clamp(floor(x * 32768 +
  0.5), -32768, 32767)` in double precision; no dither, no noise shaping, no
  soft clipping. MP3 uses minimp3's own int16 output (round to nearest with
  saturation). 16 bits is the declared depth because every pinned decoder's
  integer path and every downstream PCM consumer of these files is 16-bit;
  more bits would only carry codec noise.
- Channels: 1 and 2 keep `channelMask = nullopt` (as WAVE_FORMAT_PCM). Vorbis
  and Opus mapping family 1 with 3..8 channels are reordered from Vorbis order
  into WAVE order and given the standard mask: 3 `0x7`, 4 `0x33`, 5 `0x37`,
  5.1 `0x3F`, 6.1 `0x70F`, 7.1 `0x63F`. This is a deliberate mapping (the
  only remap in the capsule), proven by the FFmpeg 5.1 interop case.
- Encoder delay and padding are removed using container data only, and
  reported (`encoderDelayTrimmed`, `trimmedLeadingFrames`,
  `trimmedTrailingFrames`):
  - Vorbis: the final granule position trims the end (at most one long
    block). A first audio page whose granule is below the frames decoded
    through it trims the start, except when it is also the last page, where
    it trims the end (Vorbis I A.2; libvorbis does the same). Vorbis has no
    encoder delay.
  - Opus: always decoded at 48 kHz; OpusHead pre-skip removed, OpusHead
    output gain (Q7.8 dB) applied by libopus, final granule trims the end
    (at most 120 ms). `originalSampleRate` reports OpusHead's input rate;
    nothing is resampled back to it.
  - MP3: a first frame carrying a Xing/Info tag is not audio. With a
    LAME/Lavf/Lavc extension whose tag CRC is valid (LAME's convention: CRC
    of the bytes before the field; FFmpeg's: first 190 bytes, field zeroed,
    zero-extended), `start = delay + 529`, `kept = frames x samplesPerFrame -
    delay - padding` (529 = Layer III decoder delay, the convention FFmpeg and
    minimp3_ex use). A Xing frame count must equal the counted audio frames
    (`length_mismatch`). Without such a tag nothing is trimmed and
    `encoderDelayUnknown` is set: the output then begins with the encoder and
    decoder delay (1105 frames for LAME 3.100) and ends with padding.
- Metadata is never carried and only counted: Vorbis comments and OpusTags
  entries (`droppedComments`, including `METADATA_BLOCK_PICTURE` album art),
  MP3 ID3v2 (start, repeated), ID3v1 and APEv2 (end) (`skippedTags`). Album
  art, ReplayGain, chapters and cue data are not promised.
- Ogg demuxing (libogg page sync and CRC, then libogg packet assembly over the
  admitted pages): the first bytes must be a page; every byte must belong to a
  page with a valid CRC (no resynchronisation, no trailing bytes); page
  version 0; all BOS pages first (grouped streams). The first logical stream
  of the requested codec is decoded and other streams are skipped and counted
  (`skippedStreams`); a BOS page after data pages is `chained_stream` when
  every stream of the first link has ended (chained files are rejected
  explicitly) and `invalid_layout` otherwise; a page of an unknown or ended
  serial is `invalid_layout`; granule positions never decrease; the decoded
  stream must end with an EOS page (`truncated` otherwise); page-sequence gaps
  and partial packets are `corrupt_stream`/`truncated`. An Ogg file whose
  first stream is the other admitted codec is `wrong_magic`; Ogg without
  Vorbis/Opus (FLAC, Speex, Theora) is `unsupported_encoding`.
- Vorbis: three header packets through libvorbis (`invalid_header` for the
  identification header, `invalid_metadata` for comment/setup), 1..8
  channels, every audio packet must synthesise (`corrupt_stream`).
- Opus: OpusHead version major 0, channel count >= 1; family 0 (1-2
  channels), family 1 (1-8 channels, stream/coupled counts and every table
  entry checked); families 2, 3 (ambisonics), 255 and reserved values are
  `unsupported_channel_mapping`. OpusTags lengths are checked
  (`invalid_metadata`). Empty audio packets and libopus decode errors are
  `corrupt_stream`. Final granule below pre-skip is `corrupt_stream`.
- MP3: every byte between the tags is a consecutive Layer III frame with one
  MPEG version, sample rate and mono/stereo kind (changes are
  `corrupt_stream`); free format and Layer I/II are `unsupported_encoding`;
  reserved version/layer/bitrate/rate/emphasis values are `wrong_magic` on the
  first frame and `corrupt_stream` later; protected frames' CRC-16 (header
  bytes 2-3 and side information) must match. minimp3 is called with exactly
  one validated frame at a time. Leading frames whose bit reservoir points
  before the stream (a cut file) are emitted as silence and counted
  (`silencedFrames`); the same condition after a decoded frame is
  `corrupt_stream`. MP3 has no mandatory checksum: a corrupted unprotected
  frame can still decode (to canonical audio of the declared length).

### Encoding and quality contract

All three encoders accept 1 or 2 channels of any canonical bit depth (4..32;
converted to float `s / 2^(bits-1)`), check `ValidateAudio`, never resample
(`resampling_required` names the rates), reject out-of-range controls with
`invalid_option` (never clamped), and enforce `maximumOutputBytes` on every
page or LAME buffer (`output_limit`, no partial bytes). More than 2 channels
is `channel_limit` (channel-order mapping on encode is not implemented).

| | Vorbis in Ogg | Opus in Ogg | MP3 (LAME) |
| --- | --- | --- | --- |
| Control | `vorbisQuality` 0..100 (default 50) -> libvorbis VBR quality `-0.1 + 1.1 q / 100` | `opusBitrateKbps` 6..510 (default 128), unconstrained VBR | `mp3RateControl` CBR with `mp3BitrateKbps` (default 192; MPEG-1 rates: 32..320, MPEG-2/2.5 rates: 8..160, legal Layer III values only) or VBR (mtrh) with `mp3VbrQuality` 0..9 (LAME -V, default 2) |
| Fixed settings | serial `0x41524B56`, comment header = libvorbis vendor string only, headers flushed so audio starts on a new page, libogg page boundaries | serial `0x41524B4F`, application AUDIO, complexity 10, 20 ms frames, signal/bandwidth/channels auto, FEC/DTX/loss 0, LSB depth = clamp(bits, 8, 24), OpusHead version 1, gain 0, family 0, OpusTags = `libopus 1.5.2` vendor only | algorithm quality 2, joint stereo (mono for 1 ch), no error protection, no ReplayGain analysis, no ID3, Info/Xing + LAME tag written in place of LAME's placeholder first frame |
| Sample rates | any rate libvorbis has a mode for (tested 8000..48000; otherwise `sample_rate_limit`) | 8000, 12000, 16000, 24000, 48000 only | 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 only |
| Delay/padding written | none (Vorbis has none); final granule = exact input length | pre-skip = encoder lookahead = 312 at 48 kHz (also for lower input rates); final granule = pre-skip + input frames x 48000/rate | LAME tag: encoder delay 576, padding to whole frames |
| What a decoder sees | exactly the input frames at the input rate (any Vorbis decoder honouring granules) | exactly input frames x 48000/rate at 48 kHz (RFC 7845 decoders) | exactly the input frames when the decoder honours the LAME tag (Arken, FFmpeg); otherwise +1105 leading frames and trailing padding |
| Measured, 4 s programme, mono/stereo | q20: 45/65 kbps; q70: 85/166 kbps | 64k: 66/72 kbps; 160k: 162/177 kbps | CBR 128: 130 kbps; V0: 102/177 kbps |

Quality evidence (`CompressedAudio.Quality`, waveform SNR of the decoded
programme - speech-like voiced sound, a music chord, transients, silence -
against the source, frames 0..3 s): Vorbis q20 >= 12.0 / q70 >= 24.9 dB,
Opus 64k >= 12.3 / 160k >= 16.1 dB, MP3 CBR 128 >= 15.2 / V0 >= 23.9 dB
(measured minima over mono and stereo; test floors are 3 dB lower: 9/21,
9/13, 12/20). The higher setting is never worse, the decoded silence segment
stays within 8 LSB, and the cross-correlation lag against the source is 0.
Waveform SNR is a regression floor, not a perceptual-quality claim
(perceptual codecs, Opus especially, do not preserve waveforms at high
frequencies); no listening test was performed in this environment.

Determinism: for a given build, each encoder's bytes are a pure function of
the `Audio` and `LossyOptions` (fixed serials, no time, no randomness: libopus
`rand()` calls are FUZZING-only and FUZZING is not defined, LAME and libvorbis
use none). Tests encode twice with unrelated work between and compare bytes,
and show `Convert(wav -> x)` equals `EncodeX`. Exact bytes across compilers,
CPUs, libm implementations or the future Wasm build are **not** promised:
libvorbis's `vorbis_ftoi` uses SSE2 `cvtsd2si` on x86-64 and `floor(x+0.5)`
elsewhere, and all three encoders use float transcendental functions.

Not promised: metadata or album art preservation, ReplayGain, gapless
playback in players that ignore the LAME tag, bit-exact output across
platforms, any perceptual quality level, multichannel encoding, resampling,
chained or multiplexed Ogg output, seeking/streaming.

## Explicit rejections (typed `Error`)

| Input | Error |
| --- | --- |
| RF64, BW64, Sony Wave64 (`riff` GUID), big-endian RIFX, Ogg FLAC (`OggS`) | `unsupported_container` |
| IEEE float (tag 3 or EXTENSIBLE float subtype) | `unsupported_float` (never quantized) |
| A-law, mu-law, ADPCM, any other tag or EXTENSIBLE subtype/GUID | `unsupported_encoding` |
| Container not 8/16/24/32, or fewer than 4 valid bits | `unsupported_bit_depth` |
| Wrong block align, average bytes/s, cbSize, valid bits 0 or above container, partial frame in `data` | `invalid_header` |
| Missing/duplicate `fmt `/`data`, `data` before `fmt `, bytes after the RIFF extent, first FLAC block not STREAMINFO | `invalid_layout` |
| Non-zero padding bits below `wValidBitsPerSample` | `invalid_sample_padding` |
| Declared structure past the end, missing pad byte, truncated metadata | `truncated` |
| FLAC CRC/sync/frame parameter errors, out-of-range decoded samples, trailing garbage | `corrupt_stream` |
| STREAMINFO MD5 disagreement | `checksum_mismatch` |
| STREAMINFO total disagreeing with decoded frames | `length_mismatch` |
| Malformed channel-mask comment, block type 127, non-syncsafe ID3v2 size | `invalid_metadata` |
| Not `FORM` with type `AIFF`/`AIFC` | `wrong_magic` |
| AIFF-C `fl32`/`FL32`/`fl64`/`FL64` | `unsupported_float` |
| AIFF-C `ulaw`, `alaw`, `ima4`, `MAC3`, `MAC6`, `GSM `, `in24`, any other compressionType | `unsupported_encoding` |
| AIFF `sampleSize` outside 4..32, `raw ` wider than 8 bits | `unsupported_bit_depth` |
| AIFF rate fractional, < 1, negative, NaN or infinite | `unsupported_sample_rate` |
| AIFF rate zero, >= 2^32 or outside the profile | `sample_rate_limit` |
| AIFF COMM too short, compressionName past COMM, SSND < 8 bytes, SSND offset past data, SSND data != COMM frames | `invalid_header` |
| Missing/duplicate COMM, duplicate SSND, missing SSND with frames > 0, bytes after FORM | `invalid_layout` |
| Ogg: not starting with a page, or first stream of the other admitted codec; MP3: first frame header invalid | `wrong_magic` |
| Ogg without a Vorbis/Opus stream; MP3 free format, Layer I/II | `unsupported_encoding` |
| Ogg: a second chained link | `chained_stream` |
| Ogg: BOS after data of an open stream, duplicate BOS serial, page of unknown/ended serial | `invalid_layout` |
| Ogg: CRC mismatch, lost sync, page version != 0, sequence gap, decreasing granule; Vorbis/Opus packet errors; MP3 frame CRC, parameter change, minimp3 rejection | `corrupt_stream` |
| Ogg: partial page, missing EOS, partial packet; MP3: partial frame | `truncated` |
| Opus family 2/3/255/reserved, bad stream counts or table entries | `unsupported_channel_mapping` |
| OpusHead major version != 0, family 0 with > 2 channels; Vorbis identification header | `invalid_header` |
| Vorbis comment/setup header, OpusTags lengths, APEv2 size, non-syncsafe ID3v2 size | `invalid_metadata` |
| Granule range not covered by decoded audio or end trim too long; Xing frame count mismatch; LAME delay/padding exceeding the stream | `length_mismatch` |
| Encoding: Opus rate not 8/12/16/24/48 kHz, MP3 rate not a Layer III rate | `resampling_required` |
| Encoding: a `LossyOptions` control out of range or an illegal MP3 bitrate | `invalid_option` |
| Encoding: a legal MP3 CBR bitrate whose frames cannot hold the Info/LAME gapless tag (checked before encoding; the tag is never dropped) | `bitrate_too_low_for_gapless_tag` (message names the minimum per rate and channel count) |
| Encoding lossy with more than 2 channels | `channel_limit` |

Every strict prefix of a valid WAV or AIFF, and a sweep of prefixes of a valid
FLAC, is rejected by the tests; none decodes as shorter audio.

## Limits (`Limits`, defaults)

| Field | Default | Applies to |
| --- | --- | --- |
| `maximumSourceBytes` | 256 MiB | encoded input |
| `maximumOutputBytes` | 512 MiB | encoded output (checked before writing WAV, on every libFLAC write) |
| `maximumTotalSamples` | 64 Mi (= 256 MiB of int32) | frames x channels, before allocation |
| `maximumChannels` | 8 | every container (FLAC itself also caps at 8) |
| `minimumSampleRate` .. `maximumSampleRate` | 1 .. 768,000 Hz | every container |
| `maximumMetadataBlockBytes` | 1 MiB | each non-`data` RIFF chunk, each non-`SSND` AIFF chunk (COMM included), each FLAC block, the ID3v2 prefix |
| `maximumMetadataBlocks` | 1024 | RIFF/AIFF chunks / FLAC blocks per source |

Lossy formats use the same `Limits`: `maximumSourceBytes` on input;
`maximumTotalSamples` is checked before decoding against the final Ogg granule
(Vorbis), final granule minus pre-skip (Opus) or the counted frames (MP3), and
again while decoding (raw decoded audio may exceed it by at most one codec
block before the stream is rejected); `maximumChannels`; the sample-rate
range (Opus output 48 kHz must be inside it); `maximumMetadataBlockBytes` for
every Vorbis header packet, OpusTags, each ID3v2 tag and an APEv2 tag;
`maximumMetadataBlocks` for the number of Ogg logical streams and of ID3v2
tags; `maximumOutputBytes` on every encoder write. Decoding holds the source,
the libogg page copy of one page, codec state and the decoded int32 PCM
(trimmed in place, no second copy); encoding holds the source PCM, one chunk
of float input and the output.

The implementation is resident, not streaming: one call holds the source, the
canonical PCM (4 bytes per sample) and the encoded output. Peak is roughly
source + 4 x samples + output plus libFLAC's fixed per-stream state (and its
verification decoder when encoding). Integrators must state their own smaller
profile; nothing here claims gigabyte streaming.

## libFLAC dependency

- Recipe: `sdk/arken_sdk_dev/external_packages/recipes/flac-1.5.0-v1.json`,
  adapter `cmake-source`, target `arken::external_flac_v1` (upstream `FLAC`).
- Source: `https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz`
  (latest xiph/flac release tag 1.5.0 at the time of pinning),
  sha256 `f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920`.
- Licence: libFLAC is BSD-3-Clause (`COPYING.Xiph`, sha256
  `7866ee98...6b2c`; `AUTHORS` declared too). The GPL programs, LGPL/GPL
  helpers (`src/share/*`), FDL docs and libFLAC++ are not built.
- Options: library only (`BUILD_PROGRAMS/EXAMPLES/TESTING/DOCS/UTILS/CXXLIBS`
  off, no man pages, pkg-config or CMake package files, static), `WITH_OGG`
  off (no libogg lookup, no Ogg sources compiled), `WITH_ASM` off (defines
  `FLAC__NO_ASM`: no CPUID, every SSE/AVX/NEON intrinsic file compiles empty,
  the portable C paths run on x86-64, arm64 and wasm alike), `WITH_AVX` off,
  `ENABLE_MULTITHREADING` off (no pthread), `ENABLE_64_BIT_WORDS` on,
  `WITH_FORTIFY_SOURCE` and `WITH_STACK_PROTECTOR` off so upstream adds no
  platform-specific hardening flags (note: Ubuntu's GCC still applies its own
  default `_FORTIFY_SOURCE`/stack protector natively).
- Component compile options: `-fno-associative-math -fno-reciprocal-math
  -ffp-contract=off`, appended after upstream's `-fassociative-math ...` so
  the encoder's floating-point LPC analysis is not reassociated or fused (same
  arithmetic policy as the libheif package).
- Exact-file replacement patches (`patches/generated/flac-1.5.0/`):
  - `CMakeLists.txt`: removes the cache default that forced the whole consumer
    to `CMAKE_BUILD_TYPE=Release`; removes the `git describe` probe so the
    vendor string is always the release identity
    `reference libFLAC 1.5.0 20250211`; probes iconv only when programs/tests
    are built; tolerates the absent helper targets.
  - `src/CMakeLists.txt`: defines `share/{getopt,grabbag,utf8,replaygain_*}`
    only when programs, tests, examples or utilities are built.
  - Verified result: the configured graph contains exactly one third-party
    target, `FLAC`, linking only `m`; no getopt, iconv, intl, libogg or
    Threads target or cache entry exists.
- Archive note: the release tarball contains three test files with UTF-8
  names. libarchive refuses them under the POSIX `C` locale, so the SDK
  (`ArkenExternalPackageSource.cmake`) lists and extracts archives in child
  processes with a UTF-8 `LC_ALL` when the host's effective `LC_CTYPE` is not
  UTF-8. Compositions need no locale workaround of their own.
- Browser note (not yet qualified): libFLAC's static archive still contains
  its file-based entry points (`fopen`, `fseeko`, `chmod`, `utimensat`,
  `unlink`, `stdin`/`stdout` in `stream_decoder.c`, `stream_encoder.c` and
  `metadata_iterators.c`). The capsule never calls them, but they are linked
  as part of those objects. A Wasm build will need the same explicit-denial
  platform adapter approach used for HEIF (handoff section 6), or a patch, and
  an import-list check.

## Lossy codec dependencies

All five are pinned through the external package graph (recipes in
`sdk/arken_sdk_dev/external_packages/recipes/`), built static with no tests,
docs, programs, shared libraries, SIMD, intrinsics, runtime CPU detection or
assembly, and `-ffp-contract=off` on every floating-point library. Each
archive was downloaded twice with identical SHA-256. The selected-sources
adapter is used for the four archives (explicit compiled file lists; no
upstream CMake/autotools runs, so no forced build type, Git probe, CPU probe
or generated config header), which is why libopus is not built through its
CMakeLists although it ships one.

| Recipe | Licence (SPDX) | Source | SHA-256 | Licence files (SHA-256) |
| --- | --- | --- | --- | --- |
| `libogg-1.3.6-v1` | BSD-3-Clause | `github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.xz` | `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061` | `COPYING` d2ab5758..f814, `AUTHORS` 6fcb33d0..9506 |
| `libvorbis-1.3.7-v1` | BSD-3-Clause | `github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.xz` | `b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b` | `COPYING` ec1815db..f43a, `AUTHORS` f5cab9f1..96d3 |
| `opus-1.5.2-v1` | BSD-3-Clause | `github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz` | `65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1` | `COPYING` 01e1167d..9551, `AUTHORS` f9fd849e..438b |
| `lame-3.100-v1` | LGPL-2.0-or-later | `downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz` | `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e` | `COPYING` bfe4a52d..37fd, `LICENSE` 7a40cbd8..cae4 |
| `minimp3-ea99364-v1` | CC0-1.0 | tracked source `sdk/arken_sdk_dev/third_party/minimp3` (`minimp3.h`, `minimp3_ex.h`, `LICENSE`), upstream `github.com/lieff/minimp3` commit `ea99364f61c14656440e8d77e9c233ccf3124633` | tree `3c0017c43be58f1415a2bd7095684223369e00be3e68033364c23f282913d025` | `LICENSE` 6a1ee543..0e01 |

- xiph.org/downloads and downloads.xiph.org were not reachable from the
  qualification host (proxy 403); the GitHub release assets of the xiph
  repositories are the same release tarballs (digests match the published
  ones). libopus 1.5.2 was the newest release whose asset could be fetched;
  newer releases could not be enumerated from this host.
- libogg: sources `src/bitwise.c`, `src/framing.c`. The release ships
  `include/ogg/config_types.h.in` only; an exact-file replacement of
  `include/ogg/os_types.h` (`patches/generated/libogg-1.3.6/`) pins the C99
  `<stdint.h>` types configure/CMake would generate for Linux, macOS and Wasm.
- libvorbis: libvorbis (19 files from `lib/Makefile.am`) and libvorbisenc
  (`lib/vorbisenc.c`); no vorbisfile, no tools; `HAVE_ALLOCA_H`; depends on
  `libogg-1.3.6-v1` (linked through `arken::external_ogg_v1`).
- libopus: float build (`OPUS_BUILD`, `USE_ALLOCA`, `HAVE_LRINTF`,
  `HAVE_LRINT`, `ENABLE_HARDENING`, `DISABLE_DEBUG_FLOAT`,
  `PACKAGE_VERSION="1.5.2"`), the 137 C files of `CELT_SOURCES`,
  `SILK_SOURCES`, `SILK_SOURCES_FLOAT`, `OPUS_SOURCES` and
  `OPUS_SOURCES_FLOAT`; no `celt/x86`, `celt/arm`, `silk/x86`, `silk/arm`,
  `dnn` (no DRED/OSCE/deep PLC), no custom modes. Without
  `OPUS_X86_MAY_HAVE_*`/`OPUS_HAVE_RTCD` the portable C paths run on every
  host (the fixed-point build was not chosen: the float encoder is the
  reference-quality path; determinism comes from the fixed configuration and
  `-ffp-contract=off`, not from integer arithmetic).
- LAME (MP3 encode outcome: **included**): the 20 `libmp3lame` C files of
  `libmp3lame_la_SOURCES` (including `mpglib_interface.c`, which compiles to
  stubs without `HAVE_MPGLIB`); no frontend, no mpglib decoder, no
  `vector/xmm_quantize_sub.c`, no NASM, no `DECODE_ON_THE_FLY`. LAME's
  release has no CMake and its `config.h` comes from configure; the
  selected-sources patch mechanism can only replace existing files, so the
  hand-pinned configuration is the recipe's reviewed `compileDefinitions`
  list (`STDC_HEADERS`, `HAVE_ERRNO_H`, `HAVE_FCNTL_H`, `HAVE_INTTYPES_H`,
  `HAVE_STDINT_H`, `HAVE_LIMITS_H`, `HAVE_STDLIB_H`, `HAVE_STRING_H`,
  `HAVE_STRCHR`, `HAVE_MEMCPY`, `ieee754_float32_t=float`,
  `LAME_LIBRARY_BUILD`) with `HAVE_CONFIG_H` left undefined (every upstream
  `#include <config.h>` is guarded by it). `TAKEHIRO_IEEE754_HACK` and
  `USE_FAST_LOG` stay undefined (portable arithmetic); with this
  configuration libmp3lame has no mutable global state (its only mutable
  statics are under `DEBUG`, `USE_FAST_LOG` or `HAVE_MPGLIB`). The library's
  report callbacks are set to a no-op so it never writes to stderr. GCC 13
  warns `-Wstringop-overflow` in `quantize.c` (a 15-element `frameBits`
  passed to a parameter declared `[16]`); every index is bounded by
  `vbr_max_bitrate_index <= 14`, so no access is out of bounds.
  Licence: LGPL-2.0-or-later, statically linked. Distributing a binary
  requires offering LAME's source and allowing relinking with a modified
  LAME (e.g. by providing the capsule's object files or its source, which
  this repository does); integrators that cannot meet that must build
  without MP3 encoding. Patents on MP3 have expired.
- minimp3: one first-party C file (`src/minimp3.c`) defines
  `MINIMP3_IMPLEMENTATION`; `MINIMP3_NO_SIMD` and `MINIMP3_ONLY_MP3` come
  from the interface target, so Layer I/II and SSE/NEON code are compiled
  out. The pinned commit includes upstream's March 2026 fixes for
  out-of-bounds reads in VBR-tag parsing; Arken parses the Xing/LAME tag
  itself and calls only `mp3dec_init`/`mp3dec_decode_frame`.

## Determinism

`EncodeFlac` writes, for a given `Audio` and a given build:

- compression level 5 (blocksize 4096, max LPC order 8, apodization
  `tukey(5e-1)`, mid/side stereo with loose mode off, max residual partition
  order 5), one thread, encoder verification on
  (libFLAC decodes its own output and fails with `verify_failed` on any
  mismatch), MD5 on (libFLAC default; the setter is not public API);
- streamable subset when bits are 8/12/16/20/24/32 and
  `FLAC__format_sample_rate_is_subset(rate)`, non-subset otherwise (a pure
  function of the input parameters);
- exactly two metadata blocks: STREAMINFO (rewritten at finish through the
  in-memory seek callback, so totals, frame sizes and MD5 are final) and one
  VORBIS_COMMENT carrying libFLAC's vendor string and, only when a mask is
  declared, `WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x%04X`. No SEEKTABLE, no
  PADDING, no other block. libFLAC itself adds no padding.

Tests encode twice (with unrelated work in between) and compare bytes, and
show `Convert(wav->flac)` equals `EncodeFlac`. The same digest was observed in
a GCC 13 Release build and a GCC 13 `-O0` ASan/UBSan build. Byte identity
across compilers, libm implementations or the future Wasm build is **not**
claimed (the LPC window uses `cosf`/`exp`); the lossless contract is decoded
sample equality, which the tests check everywhere.

`EncodeWav` and `EncodeAiff` are pure byte layouts and are byte-identical for
equal input (no timestamps, names or version chunks).

AIFF round-trip byte identity that holds and is tested: AIFF->WAV->AIFF and
AIFF->FLAC->AIFF for every AIFF this writer can produce (all bit depths 4..32,
because AIFF ignores the zero mask WAV materializes for non-aligned depths),
and `Convert(wav|flac -> aiff)` equals `EncodeAiff` of the same samples. WAV
or FLAC with a declared mask -> AIFF -> back loses the mask (reported).

Round-trip byte identity that holds and is tested: WAV->FLAC->WAV for every
WAV this writer can produce, and FLAC->WAV->FLAC when bits are 8/16/24/32 or a
mask is declared. For 12/20-bit FLAC without a mask, WAV must use EXTENSIBLE
and records mask 0, so the second FLAC adds a mask comment: samples are
identical, bytes are not.

## Native qualification

`tests/` is a standalone CMake project that resolves the capsule through the
module graph (`lib.manifest.json` -> `arken_audio_codecs_v1` ->
`flac-1.5.0-v1`).

```sh
cmake -S capsule/arken_audio_codecs_v1/tests -B build-audio
cmake --build build-audio -j2
ctest --test-dir build-audio -j2
```

Checks: `WavRoundTrip` (340 cases: mono/stereo, 8/16/24/32-bit, 8-192 kHz,
silence, impulse, extrema, clipping-adjacent, sine, noise, mixed; plus
20/24, 12/16, 4/8, 28/32 EXTENSIBLE and a hand-authored reference header),
`FlacRoundTrip` (255 cases including 12/20/32-bit and stream-layout checks),
`ChannelMask` (5.1, 7.1, quad, explicit 0, high bit, >2 channels without mask,
order preservation), `Malformed`, `Limits`, `Determinism`, `LongSignal`
(three minutes of 24-bit stereo at 48 kHz, 8,640,000 frames), `Conversion`,
`Version`, the AIFF checks below, and `FfmpegInterop` (registered only when
the host has `ffmpeg`: FFmpeg decodes Arken WAV/FLAC to identical PCM, and
Arken decodes FFmpeg's WAV with LIST chunk and FFmpeg's FLAC to identical PCM;
FFmpeg is a reference oracle, never a dependency).

AIFF checks: `AiffRate` (hand-computed extended encodings for 8000, 11025,
22050, 44100, 48000, 88200, 96000, 192000, 1, 7, 11127, 44101, 768000 and
4294967295 Hz, checked for encoding, decoding and round trip; where the host
`long double` is x87 80-bit, every rate 1..200,000 Hz plus a stride to
768 kHz is compared with the hardware's own encoding; fractional, negative,
NaN, infinite, zero, tiny, 2^32, 2^40 and largest-finite rates are typed
errors), `AiffRoundTrip` (392 generated cases: mono/stereo, 8/16/24/32-bit,
7 rates, silence/impulse/extrema/clipping/sine/noise/mixed, each AIFF->WAV->AIFF
and AIFF->FLAC->AIFF byte-identical and through `Convert`; 4/12/17/20/28/31-bit
sampleSize; 6-channel order; empty; hand-authored references for 8/12/16/24/32
bit), `AiffVariants` (AIFC NONE/twos/sowt incl. FVER and compressionName,
sowt and twos at 8/20/24/32 bits, `raw `, float and compressed rejections,
nine unknown chunk types counted, COMM after SSND, SSND offset/blockSize,
zero-frame file without SSND, padding bits big- and little-endian),
`AiffMalformed` (magic, every strict prefix, trailing bytes, layout, header,
frame-count mismatches, channel and bit-depth fields, pad bytes, FORM size,
and a single-byte corruption sweep that may only yield canonically valid
audio), `AiffLimits` (source, samples incl. a 2^32-1 frame claim, channels,
rates, chunk size/count with SSND exempt, output) and `AiffDeterminism`
(encode twice, Convert from WAV/FLAC equals EncodeAiff and vice versa,
mask drop reported). `FfmpegInterop` additionally: FFmpeg (`-f aiff`) decodes
Arken's 8/16/24/32-bit and 11127 Hz AIFF to identical PCM and `ffprobe` reads
the exact rate and channel count; Arken decodes FFmpeg's FORM/AIFF at the same
depths/rates, FFmpeg's AIFC `sowt` and `raw ` (both with FVER) to identical
PCM and checks rate/channels/bits; FFmpeg's AIFC `fl32`/`fl64` are rejected
as `unsupported_float` and `ulaw`/`alaw` as `unsupported_encoding`.
All of these also passed under a GCC 13 `-O0` ASan/UBSan build (LongSignal
excluded for time; it exercises no AIFF code).

Not yet covered: the Xiph/RFC 9639 conformance corpus, real recorded music
files, AIFF files from Apple tools (Logic, GarageBand, `afconvert`) or Pro
Tools rather than FFmpeg, cancellation (the API is a single bounded call with no cancellation
hook), and any browser build.

### Lossy qualification

`tests/compressed_audio_tests.cpp` (`arken-compressed-audio-tests`) and
`tests/lossy_interop.cmake`, registered as `CompressedAudio.*`:

- `Quality`: Vorbis, Opus, MP3 x mono/stereo x two settings on 4 s of
  programme material: exact frame count, 16-bit/rate/channel parameters,
  canonical audio, lossy flag, cross-correlation lag 0, SNR floors (see the
  quality contract), higher setting not worse, decoded silence within 8 LSB.
- `Gapless`: a 1 s 440 Hz tone at 44.1 kHz through MP3 CBR and VBR (mono and
  stereo) decodes to exactly 44100 frames with the LAME-tag delay (576 + 529
  leading frames) and padding trimmed; Vorbis 44100 frames (end trim only);
  Opus 48000 frames with pre-skip 312 trimmed; MP3 programme material at lag
  0; the same MP3 without its Info frame keeps every decoded frame, reports
  `encoderDelayUnknown` and shows the 1105-frame lag; Opus from 16 kHz decodes
  to 48 kHz with exactly 3x the frames and `originalSampleRate` 16000;
  single-page Vorbis/Opus streams trim the end, not the start; 0, 1, 575,
  1153 and 4097-frame inputs round-trip to exactly that length in all three.
- `Determinism`: identical bytes twice for each encoder; `Convert(wav -> x)`
  equals `EncodeX`; decoding is repeatable.
- `Reports`: wav->lossy sets `lossyOutput` only; lossy->wav sets
  `report.lossy` and 16 bits; every lossy->lossy pair (9) sets
  `generationLoss`; lossy->FLAC/AIFF; 24-bit FLAC->MP3; 44.1 kHz FLAC->Opus is
  `resampling_required`; two OpusTags comments (one a picture) are counted.
- `OggStructure`: wrong codec / non-Ogg / Ogg-as-FLAC errors; chained Vorbis
  and chained Opus (`chained_stream`); a grouped Vorbis+Opus file decodes each
  stream identically to its standalone file with `skippedStreams` 1; BOS after
  data; missing EOS; a missing page; foreign serial; final granule -100
  trims 100 frames, +5000 is `length_mismatch`; all Vorbis granules -300 trim
  300 leading frames; OpusHead pre-skip +480 shifts the same samples; output
  gain -6.02 dB halves the amplitude; version 0x10, family 255, family 2,
  out-of-range table entry and family 0 with 3 channels rejected; family 1
  with an explicit stereo table equals family 0 and a swapped table swaps
  channels.
- `Malformed`: for each format every strict prefix (stride 1 for the first
  4 KiB, then 7) is rejected with no samples; a single-bit flip every 13 bytes
  is always rejected for Ogg (page CRC) and for MP3 either rejected or decoded
  to canonical audio; trailing bytes rejected; MP3 free format and Layer II
  are `unsupported_encoding`; ID3v2 (with an APIC frame), APEv2 and ID3v1 are
  skipped and counted with identical samples; non-syncsafe ID3v2 size; Xing
  frame-count mismatch; a mid-stream sample-rate change.
- `Limits`: source, sample, channel, output (exact limit accepted, one byte
  less rejected with no bytes) and header-packet limits per format; 3-channel
  encode; non-canonical input; every option boundary; Opus at 44.1/32/96/11.025
  kHz and MP3 at 96/88.2/7.999/64 kHz are `resampling_required`; MP3 and
  Vorbis round trips at all nine MP3 rates and Opus at all five Opus rates
  return exactly the input length.
- `Mp3GaplessFloor`: at all nine MP3 rates, mono and stereo, every legal CBR
  bitrate below the minimum is `bitrate_too_low_for_gapless_tag` with no bytes
  (also through `Convert`) and every one at or above it encodes with the LAME
  tag and decodes to the exact length; `Mp3MinimumCbrBitrateKbps` matches the
  table; VBR 9 is accepted everywhere; an illegal bitrate stays
  `invalid_option`; the message names the minimums.
- `Version`: `Xiph.Org libVorbis 1.3.7; libopus 1.5.2; LAME 3.100`.
- `FfmpegInterop` (FFmpeg 6.1.1 on the host as an independent oracle; its
  native Vorbis, Opus and MP3 decoders): FFmpeg decodes Arken's Vorbis, Opus,
  MP3 CBR and MP3 VBR (mono and stereo) to exactly the source frame count at
  lag 0 with SNR >= 10 dB (observed 14.9-21.5 dB); Arken decodes FFmpeg's
  libvorbis q5, libopus 128k, libmp3lame 192k and -q:a 2 output (with FFmpeg's
  ID3v2 and Xing/Lavc tag) to exactly the source frame count at lag 0 with
  SNR >= 10 dB (observed 14.9-21.8 dB); Arken decodes FFmpeg's 5.1 Vorbis and
  5.1 Opus (family 1) with each WAVE-order channel matching its own source
  chirp (Vorbis 36.6-39.8 dB, Opus 38.3-44.4 dB per main channel; LFE 14.9 and
  23.8 dB, band-limited by the encoders) and mask `0x3F`.

Sanitizers: the whole suite (25 tests) also passed in a GCC 13 `-O0`
ASan/UBSan build. ASan reported nothing. UBSan (in recover mode) reported
only upstream code: left shifts of negative or overflowing values in
libvorbis (`floor1.c:820`, `psy.c:319`, `sharedbook.c:425`) and LAME
(`VbrTag.c:263-280`, `bitstream.c:181`, `quantize.c:898`), which GCC defines
as two's-complement shifts, and libvorbis `psy.c:347` reading
`noiseoff[j][17]` of a `[17]` row with weight `del = 0` (inside the
`vorbis_info_psy` struct; the read value never contributes). None is in
first-party code; none is patched.

Not covered for the lossy formats: listening tests, files from encoders other
than libvorbis/libopus/LAME (e.g. Apple, Fraunhofer, aoTuV), Ogg files with
Skeleton or Theora streams (handled by the skip logic but not exercised),
MP3 with CRC protection (no available encoder produced one; the CRC check is
implemented but untested), MPEG-2.5 files from an independent encoder (LAME
round trips at 8/11.025/12 kHz only), 6.1/7.1 decode, and the browser build.

## Remaining steps to expose wav_to_flac and flac_to_wav in the standalone MCP

1. Conversion owner (`capsule/arken_file_conversion_v1`):
   - add an `audio` component to `capsule.manifest.json` with
     `public_dependencies: ["arken_audio_codecs_v1"]` and a
     `cmake/Audio.cmake` that creates `arken_file_conversion_audio_v1` from a
     new `src/audio.cpp` (mirroring `ArkenFileConversionImages.cmake`);
   - in `api.hpp`, next to `ConvertStandaloneImage`, declare the standalone
     audio profile and owner function, e.g.
     `enum class AudioFormat {wav, flac};`
     `struct AudioOptions {AudioFormat input, output;};`
     `struct AudioResult {Error error; audio::Issue decodeIssue; audio::Error encodeError; std::uint32_t sampleRate, channels, bitsPerSample; std::uint64_t frames; std::vector<std::byte> bytes;};`
     `[[nodiscard]] AudioResult ConvertStandaloneAudio(std::span<const std::byte>, const AudioOptions&) noexcept;`
     implemented with `audio::Convert` and an explicit `audio::Limits`
     (suggested first profile: source and output bytes equal to the existing
     8 MiB `maximumSourceBytes`/`maximumOutputBytes`, `maximumTotalSamples`
     2 Mi so the int32 copy stays at 8 MiB, channels 8, 1..384,000 Hz,
     metadata 1 MiB / 256 blocks). Note 8 MiB is only ~47 s of 16-bit stereo
     44.1 kHz WAV; a larger audio profile is a separate, measured decision.
     Map decode failures to `Error::decode_failed`, encode failures to
     `Error::encode_failed`, `input_too_large` to `Error::input_limit`.
2. Command schema
   (`capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`):
   - add `value wav @value_id(...)` and `value flac @value_id(...)` only if a
     generic audio command is wanted; image `input_kind`/`output_kind` must
     not gain audio values (they are cast to `image::Format`). Prefer a new
     `enum audio_kind` and `model audio_conversion_result` with `output_path`,
     `media_type`, `sample_rate`, `channels`, `bits_per_sample`, `frames`,
     `output_bytes`, `sha256`, `durable` (frames needs a u64 semantic type if
     the profile ever exceeds 2^32-1 frames);
   - add `command wav_to_flac @command_id(17) { request file_request; response audio_conversion_result; errors command_error; ... }`
     and `command flac_to_wav @command_id(18) { ... }`.
3. Command layer (`src/commands.cpp`): route ids 17/18 to
   `ConvertStandaloneAudio`; media types `audio/wav` and `audio/flac`; error
   message `conversionMessage` extended with `audio::ErrorMessage`; tool
   descriptions must say: lossless integer PCM only, float/compressed WAV and
   RF64 rejected, tags/pictures/cue sheets and LIST chunks are not carried,
   channel mask preserved, no overwrite, the byte/sample limits. Update the
   capsule manifest (`private_dependencies` gains nothing new if the owner
   component carries the codec) and the server description/version.
4. Composition: root `lib.manifest.json` `module_components` selects the new
   `arken_file_conversion_v1` component; the root build must give the archive
   listing a UTF-8 locale (see Archive note); Waygate's refresh profile must
   list the new test target(s) and recipe/patch files.
5. Tests: command-level tests in `arken_file_conversion_commands_v1/tests`
   for both commands (success with digest, float WAV rejection, limit
   rejection, existing-output refusal), then a fresh Waygate prepare/qualify.

## Remaining steps to expose AIFF in the standalone MCP

The codec side is complete (`Container::aiff = 3`, `DecodeAiff`, `EncodeAiff`,
`Convert` in every direction, `Error::unsupported_sample_rate`,
`ConvertResult::droppedChannelMask`). Nothing outside this capsule has been
changed. To expose it:

1. Conversion owner (`capsule/arken_file_conversion_v1`):
   - `src/audio.cpp` `Known()` admits only wav/flac today; add
     `audio::Container::aiff`. `StandaloneAudioLimits()` needs no change.
   - `AudioResult`: add `bool channelMaskDropped` copied from
     `ConvertResult::droppedChannelMask` (WAV/FLAC with a mask -> AIFF), and
     update the `skippedMetadata` comment (it now also counts AIFF chunks
     other than COMM/SSND, through `report.skippedChunks`).
   - `audio.hpp` comments: "WAV <-> FLAC" becomes "WAV, FLAC and AIFF"; AIFF-C
     float/compressed rejections.
   - `tests/media_tests.cpp`: aiff<->wav/flac success with exact samples,
     AIFC `fl32` rejection (`codecError == unsupported_float`), non-integer
     rate (`unsupported_sample_rate`), mask-drop flag.
2. Command schema
   (`capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`):
   - `enum audio_kind`: add `value aiff @value_id(3);`. The command layer
     casts `audio_kind` to `audio::Container`, so the value id must stay 3.
   - `model audio_result`: add a `channel_mask_dropped` boolean field with the
     next free `@field_id`.
   - add `command aiff_to_wav`, `aiff_to_flac`, `wav_to_aiff`, `flac_to_aiff`
     with the next free `@command_id`s (25-28 on this branch, after
     `rtf_to_markdown @command_id(24)`; coordinate with other tracks adding
     commands), each `{ request file_request; response audio_result; errors
     command_error; idempotency none; streaming unary; targets cpp; }`, and
     regenerate the schema bindings.
3. Command layer (`src/commands.cpp`):
   - add four `audioPairs` entries (aiff->wav, aiff->flac, wav->aiff,
     flac->aiff) with descriptions stating: lossless integer PCM (4-32 bit),
     AIFF and AIFF-C (NONE, twos, sowt, raw) read; AIFF written (big-endian,
     COMM + SSND only); float (fl32/fl64), mu-law, A-law, IMA4 and other
     compressed AIFF-C rejected, never approximated; non-integer sample rates
     rejected; MARK/INST/COMT/NAME/AUTH/ANNO/APPL/ID3 chunks are not carried
     (skipped_metadata counts them); AIFF has no channel mask, so a mask from
     WAV/FLAC is dropped (channel_mask_dropped) while channel order is kept;
     64 MiB limits; relative source_path and new output_path.
   - media type: replace `to==flac?"audio/flac":"audio/wav"` with a switch
     giving `audio/aiff` for `Container::aiff` (and keep `audio/wav`,
     `audio/flac`).
   - `audioDescription` (`convert_audio`): input_format/output_format gain
     `aiff`.
   - set `result.channel_mask_dropped` from the owner result.
   - `conversionMessage` already falls back to `audio::ErrorMessage`, which
     covers `unsupported_sample_rate`.
4. Composition: no new dependency or recipe (AIFF is first-party code in
   this capsule); the server description/version and the Waygate refresh
   profile list the new commands and test names (`AudioCodecs.Aiff*`).
5. Tests in `arken_file_conversion_commands_v1/tests/commands.cpp`: the four
   new commands and `convert_audio` with `aiff` (success with digest and
   `media_type` `audio/aiff`, float AIFC rejection, existing-output refusal,
   limit rejection), then a fresh Waygate prepare/qualify.

## Remaining steps to expose the lossy formats in the standalone MCP (done; integration record)

The codec side is complete: `Container::vorbis = 4`, `opus = 5`, `mp3 = 6`,
`DecodeVorbis/Opus/Mp3`, `EncodeVorbis/Opus/Mp3`, `LossyOptions`,
`Convert(source, destination, bytes, options, limits)`, the report fields
(`lossy`, `droppedComments`, `skippedTags`, `skippedStreams`,
`encoderDelayTrimmed`, `encoderDelayUnknown`, `trimmedLeadingFrames`,
`trimmedTrailingFrames`, `silencedFrames`, `originalSampleRate`), the
result labels (`lossyOutput`, `generationLoss`) and the errors
(`chained_stream`, `unsupported_channel_mapping`, `resampling_required`,
`invalid_option`). Nothing outside this capsule and the recipes has been
changed. To expose them (after, or together with, the AIFF steps above):

1. Composition: the root build resolves the five new recipes through this
   capsule's manifest (no root `lib.manifest.json` change is needed, the
   capsule is already selected through `arken_file_conversion_v1`); the
   Waygate refresh profile must list the recipes, `patches/generated/libogg-1.3.6/`,
   `sdk/arken_sdk_dev/third_party/minimp3/` and the test names
   (`CompressedAudio.*`). LAME is LGPL-2.0-or-later and statically linked:
   the server's notices must carry `COPYING`/`LICENSE` of LAME next to the
   BSD notices of libogg/libvorbis/libopus and the CC0 notice of minimp3, and
   distribution must keep the relinking/source offer (see "Lossy codec
   dependencies").
2. Conversion owner (`capsule/arken_file_conversion_v1`):
   - `src/audio.cpp` `Known()`: admit `vorbis`, `opus`, `mp3` (and `aiff`).
   - `ConvertStandaloneAudio` gains `const audio::LossyOptions&` and calls the
     `Convert` overload that takes it. `StandaloneAudioLimits()` needs no
     change, but the descriptions must say that for lossy sources the binding
     limit is the 32 Mi sample budget, not the 64 MiB file size (about 6 min
     20 s of 44.1 kHz stereo or 5 min 49 s of 48 kHz stereo decoded audio;
     a 64 MiB MP3 is far longer and is rejected with `sample_limit`).
   - `AudioResult`: add `bool lossySource` (`report.lossy`), `bool lossyOutput`,
     `bool generationLoss`, `std::uint32_t commentsDropped`
     (`report.droppedComments`), `std::uint32_t tagsSkipped`
     (`report.skippedTags`), `std::uint32_t streamsSkipped`,
     `bool encoderDelayTrimmed`, `bool encoderDelayUnknown`,
     `std::uint32_t originalSampleRate`.
   - Map `resampling_required` and `invalid_option` (encode errors) and
     `chained_stream`/`unsupported_channel_mapping` (decode errors) through
     `codecError`; `AudioErrorMessage` already falls back to
     `audio::ErrorMessage`, whose texts name the cause.
3. Command schema
   (`capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`):
   - `enum audio_kind`: add `value vorbis @value_id(4); value opus
     @value_id(5); value mp3 @value_id(6);` (the command layer casts
     `audio_kind` to `audio::Container`, so the ids must stay 4/5/6; `aiff` is
     3). Keep the value names `vorbis` and `opus`, never `ogg`.
   - `enum mp3_rate_control`: `cbr @value_id(1)`, `vbr @value_id(2)`.
   - `model audio_request` and a new `model lossy_file_request` (source_path,
     output_path plus the options below) gain optional controls with the
     next free `@field_id`s: `vorbis_quality` (count 0..100, default 50),
     `opus_bitrate_kbps` (count 6..510, default 128), `mp3_rate_control`
     (default cbr), `mp3_bitrate_kbps` (count, default 192), `mp3_vbr_quality`
     (count 0..9, default 2). Options for another output format are rejected,
     as the image commands do.
   - `model audio_result`: `lossy_source`, `lossy_output`, `generation_loss`,
     `encoder_delay_trimmed`, `encoder_delay_unknown` (booleans),
     `comments_dropped_count`, `tags_skipped`, `streams_skipped`,
     `original_sample_rate` (counts); keep `comments_dropped` (FLAC).
   - Commands (next free `@command_id`s after the AIFF ones, coordinate with
     other tracks), each `{ request file_request|lossy_file_request; response
     audio_result; errors command_error; idempotency none; streaming unary;
     targets cpp; }`: `wav_to_mp3`, `mp3_to_wav`, `wav_to_opus`,
     `opus_to_wav`, `wav_to_vorbis`, `vorbis_to_wav`, `flac_to_mp3` (encoders
     take `lossy_file_request`, decoders `file_request`); `convert_audio`
     covers every other pair. Regenerate the bindings.
4. Command layer (`src/commands.cpp`):
   - `audioPairs` entries for the seven commands; media type by destination:
     `audio/ogg; codecs=vorbis` (vorbis), `audio/ogg; codecs=opus` (opus),
     `audio/mpeg` (mp3), alongside `audio/wav`, `audio/flac`, `audio/aiff`.
   - Descriptions must state the quality contract, e.g. for `wav_to_mp3`:
     "Lossy MP3 (LAME 3.100): CBR mp3_bitrate_kbps (default 192) or VBR
     mp3_vbr_quality 0-9; joint stereo; 1-2 channels; sample rate must be
     8-48 kHz MPEG Layer III rate, nothing is resampled; a LAME tag records
     encoder delay and padding so gapless decoders return the exact length;
     not reversible; tags are not written"; for `wav_to_opus`: "Ogg Opus,
     opus_bitrate_kbps 6-510 (default 128) VBR; input must be 8, 12, 16, 24
     or 48 kHz (44.1 kHz is rejected, not resampled); decoders play it at
     48 kHz with the exact length"; for `wav_to_vorbis`: "Ogg Vorbis,
     vorbis_quality 0-100 (default 50, libvorbis -0.1..1.0)"; for the
     decoders: "decoded to 16-bit PCM; encoder delay/pre-skip and padding
     removed from container data (encoder_delay_unknown for MP3 without a
     LAME tag); Opus always decodes at 48 kHz; tags, comments and album art
     are counted, not carried; chained Ogg and Opus mapping families other
     than 0/1 are rejected". `convert_audio` lists all six formats.
   - Set the new `audio_result` fields from the owner result.
5. Tests in `arken_file_conversion_commands_v1/tests/commands.cpp`: each new
   command (success with digest, media type, `lossy_output`/`lossy_source`,
   exact frame count for a 1 s 44.1 kHz tone through `wav_to_mp3` then
   `mp3_to_wav`), `wav_to_opus` of 44.1 kHz (`resampling_required`),
   out-of-range options (`invalid_option`), chained Ogg (`chained_stream`),
   existing-output refusal and limit rejection; then a fresh Waygate
   prepare/qualify. Browser/Wasm builds of libvorbis, libopus, LAME and
   minimp3 need their own qualification (stdio symbols in LAME's
   `lame_mp3_tags_fid`/`id3tag.c` and libogg-free code paths must be checked
   in the import list, as for libFLAC).
