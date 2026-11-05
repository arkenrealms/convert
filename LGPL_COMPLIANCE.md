# LGPL distribution checklist for `arken-convert-commands`

**Status: this is a checklist prepared from the licence texts in the pinned sources. It is
not legal advice, and no one has reviewed or signed off on it.** Nothing in this repository
establishes that the obligations below are met for any particular distribution. Whoever
distributes a binary is responsible for completing the open items, and should have the
result reviewed by someone qualified to give that advice.

The licence texts quoted and referred to here are reproduced verbatim in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), which also lists every component, its
exact pinned identity and every modification.

## 1. What this applies to

The root build (`CMakeLists.txt`, `lib.manifest.json`) produces one executable,
`arken-convert-commands`, that **statically** links three LGPL libraries:

| Library | Version | Licence | Recipe | Modified | Why it is in the binary |
| --- | --- | --- | --- | --- | --- |
| libheif | 1.23.7 | LGPL-3.0-or-later | `libheif-1.23.7-hevc-av1-v1` | yes (6 exact-file replacements; build files, one added `#include`, Wasm-only conditionals) | HEIC decode, still-AVIF decode and encode (`ARKEN_IMAGE_CODECS_V1_HEIF` ON); not admitted by any tool |
| libde265 | 1.1.3 | LGPL-3.0-or-later | `libde265-1.1.3-v2` | yes (2 exact-file replacements; build files only) | HEVC decoding for libheif |
| LAME (libmp3lame) | 3.100 | LGPL-2.0-or-later | `lame-3.100-v1` | no source changes (selected files, explicit compile definitions) | MP3 encoding (`wav_to_mp3`, `flac_to_mp3`, `convert_audio` to `mp3`) |

Everything else in the binary (first-party code, stb, skcms, libwebp, libFLAC, libogg,
libvorbis, Opus, minimp3, nlohmann/json, libaom) is under MIT, BSD, CC0 or the AOM terms
and is, in the LGPL's vocabulary, part of the "Application" (LGPL-3.0) or the "work that
uses the Library" (LGPL-2.0). The C library, C++ standard library and libm are linked
dynamically from the operating system.

Because the libraries are statically linked, the shared-library route (LGPL-3.0 §4(d)(1))
is not available, and LGPL-2.0 §6 applies to the whole executable. Version 2.0 of the
Library GPL, which LAME ships, has no shared-library alternative in §6 at all.

## 2. Obligations

### 2.1 libheif and libde265: LGPL-3.0 §4 "Combined Works"

The binary may be conveyed under terms of the distributor's choice provided those terms
"effectively do not restrict modification of the portions of the Library contained in the
Combined Work and reverse engineering for debugging such modifications", and provided the
distributor does each of the following:

- [ ] **§4(a) Notice.** Prominent notice with each copy that libheif and libde265 are used
  and are covered by the LGPL-3.0.
- [ ] **§4(b) Licence texts.** A copy of the GNU GPL-3.0 and of the LGPL-3.0 with the
  binary. (Both are in libheif's `COPYING`, reproduced in full in
  `THIRD_PARTY_NOTICES.md`.)
- [ ] **§4(c) Runtime notices.** Only if the program displays copyright notices while
  running. The server currently reports only its name and version in the MCP
  `initialize` response (`serverInfo`); if it ever displays copyright notices, the
  libheif and libde265 notices and a pointer to the licence texts must be among them.
- [ ] **§4(d)(0) Corresponding source and relinking.** Convey the *Minimal Corresponding
  Source* of each library (the exact source of the linked version, including the
  modifications) under the LGPL, and the *Corresponding Application Code* in a form that
  lets the user recombine or relink the Application with a modified libheif or libde265,
  "in the manner specified by section 6 of the GNU GPL for conveying Corresponding
  Source" (accompanying the binary, a written offer valid for at least three years,
  equivalent access from the same place as the binary, and so on).
- [ ] **§4(e) Installation Information.** Only where GPL-3.0 §6 would require it (object
  code in or with a "User Product", i.e. a consumer device). Not applicable to a plain
  software download; check if the binary is ever shipped on a device.
- [ ] **Modification notices (GPL-3.0 §5(a), via the LGPL).** The modified library sources
  must carry prominent notices that they were modified and a relevant date. The
  replacement files under `patches/generated/` say so in comments for the CMake files, but
  `libheif/box.cc`, `box.h` and `file.cc` carry no "modified" notice, and none carries a
  date. The source bundle should include a statement listing the modified files and
  dates (the patch list in `THIRD_PARTY_NOTICES.md` plus the git history of
  `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/`, last changed
  2026-11-04).
- [ ] **No additional restrictions.** The distribution terms (licence agreement, terms of
  service, technical measures) must not forbid modifying the LGPL parts or reverse
  engineering for debugging such modifications.

### 2.2 LAME: Library GPL 2.0 §6

LAME's `COPYING` is the GNU Library General Public License version 2 (June 1991); the
source headers allow "any later version". Under §6 the executable may be distributed under
terms of the distributor's choice provided "the terms permit modification of the work for
the customer's own use and reverse engineering for debugging such modifications", and:

- [ ] **Notice.** "Prominent notice with each copy of the work that the Library is used in
  it and that the Library and its use are covered by this License."
- [ ] **Licence text.** "You must supply a copy of this License" (LAME's `COPYING`,
  reproduced in full in `THIRD_PARTY_NOTICES.md`).
- [ ] **Runtime notices.** As in 2.1(c): only if the program displays copyright notices.
- [ ] **One of §6(a)–(d):** (a) accompany the binary with the complete machine-readable
  source of LAME *and* the complete "work that uses the Library" as object code and/or
  source, so that the user can modify LAME and relink; (b) a written offer, valid for at
  least three years, to provide those materials for no more than the cost of
  distribution; (c) equivalent access to those materials from the same place the binary
  is offered; or (d) verify the user already has them.
- [ ] **Data and utility programs.** "For an executable, the required form of the 'work
  that uses the Library' must include any data and utility programs needed for
  reproducing the executable from it" (compilers and OS components that normally ship
  with the OS are excepted).
- [ ] **No additional restrictions** (§10 of that licence): no further restrictions on
  the recipients' exercise of the rights granted.

LAME's separate `LICENSE` file is an FAQ note, not licence terms. It recommends linking
LAME "as separate library" and asks that modifications be released back to the project;
LAME is unmodified here, and static linking is permitted by the LGPL if the relinking
materials above are provided.

## 3. What this repository already provides

| Obligation | Already in the repository | Notes |
| --- | --- | --- |
| Notices and licence texts | `THIRD_PARTY_NOTICES.md`: verbatim `COPYING` of libheif (LGPL-3.0 + GPL-3.0), libde265 (first two lines, rest identical), LAME (`COPYING`, `LICENSE`) and all other components | Must be shipped *with the binary*, see section 4 |
| Exact upstream sources | Recipes pin each archive by URL and sha256: libheif `f7483e818503ea6224c8863e4a56ac02c7089021a98472c811c12aa19faf2475`, libde265 `554228bd17788c99a7e63b37ab5634722190e6e2bf60c1dcb01cef328e133905`, LAME `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`; the SDK refuses any archive that does not match and verifies the declared licence files | The archives themselves are not in the repository; they are fetched from upstream |
| The modifications | Exact-file replacements under `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/` (`libheif-1.23.7/`, `libheif-1.23.7-av1/`, `libde265-1.1.3/`, `libde265-1.1.3-v2/`), each pinned by its own sha256 and by the sha256 of the upstream file it replaces | Each replacement directory also carries the upstream `COPYING` |
| Scripts to build the library | Recipe JSON (adapter, CMake options, compiled file lists, compile definitions), the SDK package graph in `sdk/arken_sdk_dev/cmake/` that fetches, verifies, patches and builds them | Together these are a reproducible recipe for the exact library build |
| Corresponding Application Code / "work that uses the Library" | The complete first-party source of the server and the recipes for every other dependency are in this repository; building the root project with a modified library archive in place of the pinned one relinks the executable | Only satisfies the obligation if that source (at the exact commit used for the binary) is actually conveyed to the recipient |

In short: pinned archives + exact-file replacements + recipes + this repository's source at
the build commit = the corresponding source and the relinking materials. What is missing is
the act of conveying them, plus the items below.

## 4. Distribution-time actions (not done by this repository)

- [ ] Ship `THIRD_PARTY_NOTICES.md` (or an equivalent notices file with the same licence
  texts) next to every copy of the binary, with a short prominent statement that the
  binary contains libheif, libde265 and LAME under the LGPL.
- [ ] Decide how source is conveyed (with the binary, from the same download location, or a
  written offer valid for at least three years) and put that offer or pointer next to the
  binary.
- [ ] Mirror the three LGPL source archives (and preferably all pinned archives) next to the
  binary or in the source bundle. Pointing only at the upstream URLs leaves the
  distributor responsible for their continued availability.
- [ ] Provide the relinking materials for the exact binary, either:
  - the repository at the exact build commit plus the toolchain description (compiler,
    CMake version, build type and the configure command), from which the user can rebuild
    with a modified library; or
  - the object files and static archives of everything except the LGPL library being
    replaced (the first-party capsule archives, the other third-party archives, and the
    `arken-convert-commands` link command from
    `capsules/arken_file_conversion_commands_v1/CMakeFiles/arken-convert-commands.dir/link.txt`
    in the build tree), so a modified `libheif.a`, `libde265.a` or
    `libarken_external_lame_3_100_v1_encoder.a` can be linked in their place.
- [ ] Add the modification statement for libheif and libde265 (files and dates) to the
  source bundle (section 2.1).
- [ ] Check that the distribution terms (EULA, terms of service, code signing or
  notarisation that would refuse a relinked binary) do not prohibit modification or
  reverse engineering for debugging. If a platform's signing prevents a relinked binary
  from running, assess whether that amounts to a restriction.
- [ ] For a browser (Wasm) build: the same obligations apply to the compiled module; this
  checklist has only considered the native server.
- [ ] Have the result reviewed. Record who reviewed it and when.

## 5. Distributing without the LGPL components

If the LGPL obligations are unwanted, the two groups can be removed. Doing so removes the
need for section 2 entirely (the remaining licences need only the notices).

### 5.1 Without HEIC/AVIF (libheif, libde265, and with them libaom)

- Set `ARKEN_IMAGE_CODECS_V1_HEIF` OFF in the root `CMakeLists.txt` (it is the capsule's
  default) and remove the `libheif-1.23.7-hevc-av1-v1` entry from the root
  `lib.manifest.json`, so the package is neither linked nor built.
- Confirm that the link line of `arken-convert-commands` no longer contains `libheif.a`,
  `libde265.a` or `libaom.a`.
- Tools that disappear: **none**. HEIC and AVIF are not admitted by any MCP tool, format
  value or conversion profile. What disappears is the in-process
  `DecodeImage`/`EncodeImage(Format::heif / Format::avif)` path and the native
  qualifications `ArkenImageCodecsV1.Heif`, `ArkenImageCodecsV1.HeifVersion`,
  `ArkenImageCodecsV1.Avif` and `ArkenImageCodecsV1.AvifVersion`. The separate dependency
  projects under `capsule/arken_image_codecs_v1/tests/heif_dependencies` and
  `tests/avif_dependencies` are unaffected and never part of the server.
- libaom (BSD-2-Clause + AOM Patent License) goes with it, since only libheif uses it.

### 5.2 Without MP3 encoding (LAME)

- There is **no build switch** for this today: `arken_audio_codecs_v1` links
  `arken::external_lame_v1` unconditionally and its manifest requires the
  `lame-3.100-v1` recipe. Removing LAME is a code change (an option in the audio capsule
  that drops the recipe, the encoder in `src/mp3.cpp` and the MP3 output from the command
  schemas), not a configuration change.
- Tools affected: `wav_to_mp3` and `flac_to_mp3` disappear, and `convert_audio` loses `mp3`
  as an output format. MP3 *decoding* (`mp3_to_wav`, `convert_audio` from `mp3`) stays,
  because it uses minimp3 (CC0-1.0).

With both groups removed, the binary contains only MIT, BSD, CC0 and Unlicense-alternative
code, and `THIRD_PARTY_NOTICES.md` (minus the removed sections) is the remaining
obligation.
