# arken_audio_codecs_v1

Status: native codec milestone (handoff section 9, plan-task d85e2c6cc4a2f945).
It covers the WAV and FLAC codec owner and its native qualification.

Standalone MCP: exposed as `convert_audio`, `wav_to_flac` and `flac_to_wav`
through `ConvertStandaloneAudio` in `arken_file_conversion_v1` (component
`standalone`) with its own profile: 64 MiB source and output, 32 Mi samples,
1-8 channels, up to 384 kHz, 8 MiB / 256 metadata blocks. The "remaining steps"
section at the end is kept as the integration record; the integrated profile
differs from its suggested 8 MiB figures. Browser execution, a UI and a
streaming file path are still not provided and need separate qualification.

## Supported profile

Every entry point (`api.hpp`) takes one complete in-memory byte span or an
`Audio` value and returns a complete result or a typed `Error`. The codec opens
no files, uses no stdio, starts no threads and keeps no global state.

Canonical model (`Audio`): interleaved `int32` samples, right-justified and
sign-extended, `bitsPerSample` significant bits (4..32), sample rate, channel
count, frame count and an optional raw WAVE_FORMAT_EXTENSIBLE `channelMask`.
Integer PCM is carried exactly; nothing is dithered, rescaled, resampled,
downmixed or reordered. 8-bit WAV's unsigned storage maps to signed by
subtracting 128.

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

FLAC write (`EncodeFlac`), see "Determinism" for the exact configuration.
1..8 channels, 4..32 bits, 1..1,048,575 Hz (FLAC__MAX_SAMPLE_RATE), at most
2^36-1 frames.

`Convert(source, destination, bytes, limits)` composes decode and encode under
one `Limits` and reports the decode issue, encode error, stream parameters and
`DecodeReport`.

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

Every strict prefix of a valid WAV, and a sweep of prefixes of a valid FLAC, is
rejected by the tests; none decodes as shorter audio.

## Limits (`Limits`, defaults)

| Field | Default | Applies to |
| --- | --- | --- |
| `maximumSourceBytes` | 256 MiB | encoded input |
| `maximumOutputBytes` | 512 MiB | encoded output (checked before writing WAV, on every libFLAC write) |
| `maximumTotalSamples` | 64 Mi (= 256 MiB of int32) | frames x channels, before allocation |
| `maximumChannels` | 8 | both containers (FLAC itself also caps at 8) |
| `minimumSampleRate` .. `maximumSampleRate` | 1 .. 768,000 Hz | both containers |
| `maximumMetadataBlockBytes` | 1 MiB | each non-`data` RIFF chunk, each FLAC block, the ID3v2 prefix |
| `maximumMetadataBlocks` | 1024 | RIFF chunks / FLAC blocks per source |

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

`EncodeWav` is a pure byte layout and is byte-identical for equal input.

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
`Version`, and `FfmpegInterop` (registered only when the host has `ffmpeg`:
FFmpeg decodes Arken WAV/FLAC to identical PCM, and Arken decodes FFmpeg's
WAV with LIST chunk and FFmpeg's FLAC to identical PCM; FFmpeg is a reference
oracle, never a dependency).

Not yet covered: the Xiph/RFC 9639 conformance corpus, real recorded music
files, cancellation (the API is a single bounded call with no cancellation
hook), and any browser build.

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
