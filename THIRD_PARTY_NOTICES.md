# Third-party notices

This file lists the third-party software compiled into the standalone MCP server binary
`arken-convert-commands` built by the root `CMakeLists.txt` of this repository, with the
exact pinned identity of each component, every modification applied to it, and the licence
texts copied verbatim from the pinned sources. The LGPL distribution obligations that follow
from statically linking libheif, libde265 and LAME are worked through in
[`LGPL_COMPLIANCE.md`](LGPL_COMPLIANCE.md).

How the set was determined: the root `lib.manifest.json` depends on
`arken_file_conversion_commands_v1`, whose `standalone` component pulls in
`arken_image_codecs_v1`, `arken_audio_codecs_v1`, `arken_document_extraction_v1` and
`arken_markup_conversion_v1`; their `capsule.manifest.json` files name the external package
recipes, and the root manifest adds the combined libheif recipe. The root `CMakeLists.txt`
sets `ARKEN_IMAGE_CODECS_V1_HEIF` ON, which links `arken::external_heif_v1` into
`arken_image_codecs_v1`. A Linux root build's link line for `arken-convert-commands`
contains `libheif.a`, `libde265.a`, `libaom.a`, `libFLAC.a` and the libwebp, libogg,
libvorbis, Opus and LAME archives; stb, skcms, minimp3 and nlohmann/json are header or
single-translation-unit sources compiled into first-party objects.

Digests: every licence file, patch file and vendored file digest below was recomputed with
SHA-256 against the files themselves (the pinned sources in the SDK package cache and the
files tracked in this repository). Source archives are not kept in the cache after
extraction, so an archive digest is the recipe's pin; for each archive the cache receipt
records the same archive digest and a source-tree digest, and that tree digest was
recomputed over the extracted tree and matches.

## Summary

| Component | Version | Licence (SPDX) | Used by | Linked into server binary |
| --- | --- | --- | --- | --- |
| stb_image / stb_image_write | stb_image 2.30, stb_image_write 1.16 (commit `013ac3be`) | MIT OR Unlicense (recipe: MIT) | image codecs, document extraction | yes |
| skcms | commit `f0bbebbe` | BSD-3-Clause | image codecs | yes |
| libwebp | 1.6.0 | BSD-3-Clause (+ patent grant) | image codecs | yes |
| libFLAC | 1.5.0 | BSD-3-Clause | audio codecs | yes |
| libogg | 1.3.6 | BSD-3-Clause | audio codecs | yes |
| libvorbis / libvorbisenc | 1.3.7 | BSD-3-Clause | audio codecs | yes |
| Opus (libopus) | 1.5.2 | BSD-3-Clause | audio codecs | yes |
| minimp3 | commit `ea99364f` | CC0-1.0 | audio codecs | yes |
| LAME (libmp3lame) | 3.100 | LGPL-2.0-or-later | audio codecs | yes (static) |
| nlohmann/json | 3.12.0 (commit `f8eee1bb`) | MIT | MCP protocol, schema runtime | yes |
| libheif | 1.23.7 | LGPL-3.0-or-later | image codecs (HEIC/AVIF, not admitted) | yes (static) |
| libde265 | 1.1.3 | LGPL-3.0-or-later | via libheif (HEVC) | yes (static) |
| libaom | 3.15.3 | BSD-2-Clause AND LicenseRef-AOM-Patent-License-1.0 | via libheif (AV1) | yes (static) |
| libheif HEVC-only / AV1-only instances (recipes `libheif-1.23.7-hevc-v1`, `libheif-1.23.7-av1-v1`), libde265 recipe v1 | same versions | as above | `tests/heif_dependencies`, `tests/avif_dependencies` | no (test-only) |
| Apple `patch` 2.0-12u11 (recipe `apple-patch-2.0-12u11-macos-v1`) | system tool | NOASSERTION (system) | build tool profile, macOS | no (not compiled or linked) |

Components marked "yes (static)" are statically linked archives built from the pinned
sources. HEIC and AVIF are compiled for development qualification only: no MCP tool,
format value or conversion profile admits them, but the code is present in the binary.

## stb_image and stb_image_write

- Upstream: https://github.com/nothings/stb
- Version: `stb_image.h` v2.30 and `stb_image_write.h` v1.16, at commit
  `013ac3beddff3dbffafd5177e7972067cd2b5083`.
- Pinned archive: `https://codeload.github.com/nothings/stb/tar.gz/013ac3beddff3dbffafd5177e7972067cd2b5083`,
  sha256 `b01aa93e1a968aed55f43e072c98ee401d2f20e897aabdb1a166c7166886ed11`
  (recipe `stb-image-2.30-v1`, header-only adapter).
- Licence: the `LICENSE` file offers a choice of MIT or public domain (Unlicense), i.e.
  `MIT OR Unlicense`; the recipe records `MIT`, the alternative this project relies on.
- Used by: `arken_image_codecs_v1` (PNG, JPEG, BMP, GIF decoding; PNG, JPEG, BMP and TGA
  encoding; zlib inflation for TIFF Deflate and JPEG for TIFF and BLP) and
  `arken_document_extraction_v1` (memory-only raw-deflate inflation of ZIP members for DOCX,
  XLSX, ODS, PPTX, ODT, ODP and EPUB).
- Modified: no. Headers are used as published.

`LICENSE` (sha256 `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`):

~~~~text
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
~~~~

## skcms

- Upstream: https://skia.googlesource.com/skcms
- Version: commit `f0bbebbebb50851813e4f11d3d4b9edd2878a056`.
- Pinned source: vendored in `sdk/arken_sdk_dev/third_party/skcms` (recipe
  `skcms-f0bbebb-v1`, tracked source). Source-tree identity (the SDK's sorted
  `file:<path>:<size>:<sha256>` digest) `42452e2c4f83ccad5e50c99eef00ec5f3a08e847dd373c34c7cbb59ae87896b5`,
  recomputed and matching.
- Licence: BSD-3-Clause.
- Used by: `arken_image_codecs_v1` (ICC profile parsing and RGB ICC to sRGB conversion).
- Modified: no recorded patches. The vendored tree is a subset of upstream files
  (`skcms.cc`, `src/skcms_public.h`, `src/skcms_internals.h`, `src/skcms_Transform.h`,
  `src/Transform_inl.h`, `src/skcms_TransformBaseline.cc`, `LICENSE`).

`sdk/arken_sdk_dev/third_party/skcms/LICENSE` (sha256 `e59bb5c5c6ba426a9ac4ba9fe667ad14c5166b12aa25be8af1d122b14fbe2e36`):

~~~~text
// Copyright (c) 2018 Google Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

--------------------------------------------------------------------------------
~~~~

## libwebp

- Upstream: https://chromium.googlesource.com/webm/libwebp
- Version: 1.6.0.
- Pinned archive: `https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz`,
  sha256 `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`
  (recipe `libwebp-1.6.0-v1`, selected-sources adapter).
- Licence: BSD-3-Clause, with Google's additional patent grant (`PATENTS`).
- Used by: `arken_image_codecs_v1` (still WebP decoding and lossless WebP encoding).
- Modified: yes, one exact-file replacement:
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/libwebp-1.6.0-histogram_enc.c` (sha256 `a14d6f26a7b6cc85c4e42728214d14000d9e7af4545f50a1bc3a0026fb0a917a`) replaces `src/enc/histogram_enc.c` (upstream sha256
    `b29e6185aeb75de958bfc53fc0cd638b471b2060e50b50d1c52ea060d4472d18`): adds an explicit
    `(HistogramIndex)` cast to one `GetPopulationInfo` argument so the file compiles
    without an implicit int-to-enum conversion under the project's strict warnings. No
    behavioural change.

`COPYING` (sha256 `5aec868f669e384a22372a4e8a1a6cd7d44c64cd451f960ca69cc170d1e13acf`):

~~~~text
Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

~~~~

`PATENTS` (sha256 `cc3273e0694ea5896145e0677699b53471b03ea43021ddc50e7923fbb9f5023c`):

~~~~text
Additional IP Rights Grant (Patents)
------------------------------------

"These implementations" means the copyrightable works that implement the WebM
codecs distributed by Google as part of the WebM Project.

Google hereby grants to you a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable (except as stated in this section) patent license to
make, have made, use, offer to sell, sell, import, transfer, and otherwise
run, modify and propagate the contents of these implementations of WebM, where
such license applies only to those patent claims, both currently owned by
Google and acquired in the future, licensable by Google that are necessarily
infringed by these implementations of WebM. This grant does not include claims
that would be infringed only as a consequence of further modification of these
implementations. If you or your agent or exclusive licensee institute or order
or agree to the institution of patent litigation or any other patent
enforcement activity against any entity (including a cross-claim or
counterclaim in a lawsuit) alleging that any of these implementations of WebM
or any code incorporated within any of these implementations of WebM
constitute direct or contributory patent infringement, or inducement of
patent infringement, then any patent rights granted to you under this License
for these implementations of WebM shall terminate as of the date such
litigation is filed.
~~~~

`AUTHORS` (sha256 `f251be89fd442bd9d701360730aea163cc8e2076ff03528f59267dc2c4f8b60a`):

~~~~text
Contributors:
- Aidan O'Loan (aidanol at gmail dot com)
- Alan Browning (browning at google dot com)
- Alexandru Ardelean (ardeleanalex at gmail dot com)
- Anuraag Agrawal (anuraaga at gmail dot com)
- Arthur Eubanks (aeubanks at google dot com)
- Brian Ledger (brianpl at google dot com)
- Charles Munger (clm at google dot com)
- Cheng Yi (cyi at google dot com)
- Christian Duvivier (cduvivier at google dot com)
- Christopher Degawa (ccom at randomderp dot com)
- Clement Courbet (courbet at google dot com)
- devtools-clrobot at google dot com (devtools-clrobot@google dot com)
- Djordje Pesut (djordje dot pesut at imgtec dot com)
- Frank (1433351828 at qq dot com)
- Frank Barchard (fbarchard at google dot com)
- Henner Zeller (hzeller at google dot com)
- Hui Su (huisu at google dot com)
- H. Vetinari (h dot vetinari at gmx dot com)
- Ilya Kurdyukov (jpegqs at gmail dot com)
- Ingvar Stepanyan (rreverser at google dot com)
- Istvan Stefan (Istvan dot Stefan at arm dot com)
- James Zern (jzern at google dot com)
- Jan Engelhardt (jengelh at medozas dot de)
- Jehan (jehan at girinstud dot io)
- Jeremy Maitin-Shepard (jbms at google dot com)
- Johann Koenig (johann dot koenig at duck dot com)
- Jonathan Grant (jgrantinfotech at gmail dot com)
- Jonliu1993 (13720414433 at 163 dot com)
- Jovan Zelincevic (jovan dot zelincevic at imgtec dot com)
- Jyrki Alakuijala (jyrki at google dot com)
- Konstantin Ivlev (tomskside at gmail dot com)
- Lode Vandevenne (lode at google dot com)
- Lou Quillio (louquillio at google dot com)
- Mans Rullgard (mans at mansr dot com)
- Marcin Kowalczyk (qrczak at google dot com)
- Martin Olsson (mnemo at minimum dot se)
- Maryla Ustarroz-Calonge (maryla at google dot com)
- Masahiro Hanada (hanada at atmark-techno dot com)
- Mikołaj Zalewski (mikolajz at google dot com)
- Mislav Bradac (mislavm at google dot com)
- natewood (natewood at fb dot com)
- Nico Weber (thakis at chromium dot org)
- Noel Chromium (noel at chromium dot org)
- Nozomi Isozaki (nontan at pixiv dot co dot jp)
- Oliver Wolff (oliver dot wolff at qt dot io)
- Owen Rodley (orodley at google dot com)
- Ozkan Sezer (sezeroz at gmail dot com)
- Parag Salasakar (img dot mips1 at gmail dot com)
- Pascal Massimino (pascal dot massimino at gmail dot com)
- Paweł Hajdan, Jr (phajdan dot jr at chromium dot org)
- Pierre Joye (pierre dot php at gmail dot com)
- Roberto Alanis (alanisbaez at google dot com)
- Sam Clegg (sbc at chromium dot org)
- Scott Hancher (seh at google dot com)
- Scott LaVarnway (slavarnway at google dot com)
- Scott Talbot (s at chikachow dot org)
- Slobodan Prijic (slobodan dot prijic at imgtec dot com)
- Somnath Banerjee (somnath dot banerjee at gmail dot com)
- Sriraman Tallam (tmsriram at google dot com)
- Tamar Levy (tamar dot levy at intel dot com)
- Thiago Perrotta (tperrotta at google dot com)
- Timothy Gu (timothygu99 at gmail dot com)
- Urvang Joshi (urvang at google dot com)
- Vikas Arora (vikasa at google dot com)
- Vincent Rabaud (vrabaud at google dot com)
- Vlad Tsyrklevich (vtsyrklevich at chromium dot org)
- Wan-Teh Chang (wtc at google dot com)
- wrv (wrv at utexas dot edu)
- Yang Zhang (yang dot zhang at arm dot com)
- Yannis Guyon (yguyon at google dot com)
- Zhi An Ng (zhin at chromium dot org)
~~~~

## libFLAC

- Upstream: https://github.com/xiph/flac
- Version: 1.5.0.
- Pinned archive: `https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz`,
  sha256 `f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920`
  (recipe `flac-1.5.0-v1`, cmake-source adapter, library only: no programs, no libFLAC++,
  no Ogg, no assembly, no multithreading).
- Licence: BSD-3-Clause (`COPYING.Xiph`). The GPL programs, the LGPL/GPL helpers under
  `src/share/` and the FDL documentation in the archive are not built.
- Used by: `arken_audio_codecs_v1` (FLAC decoding and deterministic encoding).
- Modified: yes, two exact-file replacements, both build-system only:
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/flac-1.5.0/CMakeLists.txt` (sha256 `346cdc37093158e415d074772b99aa843b93755f04b0d0697af55028b70d4cdf`) replaces `CMakeLists.txt`: removes upstream's forced `CMAKE_BUILD_TYPE=Release`
    cache default, removes the `git describe` probe so the vendor string stays the release
    identity `reference libFLAC 1.5.0 20250211`, and probes iconv only when programs or
    tests are built.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/flac-1.5.0/src/CMakeLists.txt` (sha256 `df527e85a593099a824fde3693c026b00a70e27d307618a27e51ed4ebb78ca29`) replaces `src/CMakeLists.txt`: defines the `share/` helper targets (getopt,
    grabbag, utf8, replaygain) only when programs, tests, examples or utilities are built.

`COPYING.Xiph` (sha256 `7866ee98760fc1f0156b4fe6bf530257e02be487ab3fd94e2b63799dd32d6b2c`):

~~~~text
Copyright (C) 2000-2009  Josh Coalson
Copyright (C) 2011-2025  Xiph.Org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.Org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
~~~~

`AUTHORS` (sha256 `89d44b9c876a8a5cbfc886348a151abd74eeb49e4b690b0cfe96d44a9afe8118`):

~~~~text
/* FLAC - Free Lossless Audio Codec
 * Copyright (C) 2001-2009  Josh Coalson
 * Copyright (C) 2011-2025  Xiph.Org Foundation
 *
 * This file is part the FLAC project.  FLAC is comprised of several
 * components distributed under different licenses.  The codec libraries
 * are distributed under Xiph.Org's BSD-like license (see the file
 * COPYING.Xiph in this distribution).  All other programs, libraries, and
 * plugins are distributed under the GPL (see COPYING.GPL).  The documentation
 * is distributed under the Gnu FDL (see COPYING.FDL).  Each file in the
 * FLAC distribution contains at the top the terms under which it may be
 * distributed.
 *
 * Since this particular file is relevant to all components of FLAC,
 * it may be distributed under the Xiph.Org license, which is the least
 * restrictive of those mentioned above.  See the file COPYING.Xiph in this
 * distribution.
 */

This file lists major contributors to the FLAC project. This list is not
exhaustive. For an exhaustive list, run the command `git shortlog -s` on
the git repo or visit https://gitlab.xiph.org/xiph/flac/-/graphs/master

For a complete list of contributions, run the command `git log` on the
git repo, visit https://github.com/xiph/flac/commits or visit
https://gitlab.xiph.org/xiph/flac/commits

Original author: Josh Coalson <jcoalson@users.sourceforge.net>
Maintainer 2012-2020: Erik de Castro Lopo <erikd@mega-nerd.com>
Maintainer from 2022: Martijn van Beurden <mvanb1@gmail.com>

Website : https://www.xiph.org/flac/

Other major contributors and their contributions:

"lvqcl" <lvqcl@users.sourceforge.net>
* Visual Studio build system.
* Optimisations in the encoder and decoder.

"Janne Hyvärinen" <cse@sci.fi>
* Visual Studio build system.
* Unicode handling on Windows.

"Andrey Astafiev" <andrei@tvcell.ru>
* Russian translation of the HTML documentation

"Miroslav Lichvar" <lichvarm@phoenix.inf.upol.cz>
* IA-32 assembly versions of several libFLAC routines

"Brady Patterson" <bpat@users.sourceforge.net>
* AIFF file support, PPC assembly versions of libFLAC routines

"Daisuke Shimamura" <Daisuke_Shimamura@nifty.com>
* i18n support in the XMMS plugin

"X-Fixer" <x-fixer@narod.ru>
* Configuration system, tag editing, and file info in the Winamp2 plugin

"Matt Zimmerman" <mdz@debian.org>
* Libtool/autoconf/automake make system, flac man page
~~~~

## libogg

- Upstream: https://github.com/xiph/ogg
- Version: 1.3.6.
- Pinned archive: `https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.xz`,
  sha256 `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061`
  (recipe `libogg-1.3.6-v1`, selected-sources adapter: `src/bitwise.c`, `src/framing.c`).
- Licence: BSD-3-Clause.
- Used by: `arken_audio_codecs_v1` (Ogg page sync, CRC and packet framing for the Vorbis and
  Opus demuxer and muxers).
- Modified: yes, one exact-file replacement:
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libogg-1.3.6/include/ogg/os_types.h` (sha256 `dfd39d81172b29081e621b3cc233c5d98a2b5db03b23e17c5fd461a4a168a388`) replaces `include/ogg/os_types.h`: the release ships only `config_types.h.in`; the
    replacement defines the `ogg_int*_t` types directly from C99 `<stdint.h>`, exactly as
    configure/CMake would generate them for Linux, macOS and WebAssembly, so no configure
    step or generated header is needed.

`COPYING` (sha256 `d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814`):

~~~~text
Copyright (c) 2002, Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
~~~~

`AUTHORS` (sha256 `6fcb33d0cdd60b5054e7df820916388b590427514288c2bab01c626bb8ae9506`):

~~~~text
Monty <monty@xiph.org>
Greg Maxwell <greg@xiph.org>
Ralph Giles <giles@xiph.org>
Cristian Adam <cristian.adam@gmail.com>
Tim Terriberry <tterribe@xiph.org>

and the rest of the Xiph.Org Foundation.
~~~~

## libvorbis and libvorbisenc

- Upstream: https://github.com/xiph/vorbis
- Version: 1.3.7.
- Pinned archive: `https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.xz`,
  sha256 `b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b`
  (recipe `libvorbis-1.3.7-v1`, selected-sources adapter; libvorbis and libvorbisenc only,
  no vorbisfile, no tools; depends on `libogg-1.3.6-v1`).
- Licence: BSD-3-Clause.
- Used by: `arken_audio_codecs_v1` (Vorbis decoding and VBR encoding).
- Modified: no.

`COPYING` (sha256 `ec1815db59fcd302846df949d7424876cb2e2dc5ed1606c5fb0b36787b1cf43a`):

~~~~text
Copyright (c) 2002-2020 Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
~~~~

`AUTHORS` (sha256 `f5cab9f16035f770f50299f85cf2938c06dc3a9a30425961067788102ec896d3`):

~~~~text
Monty <monty@xiph.org>

and the rest of the Xiph.org Foundation.
~~~~

## Opus (libopus)

- Upstream: https://github.com/xiph/opus (https://opus-codec.org)
- Version: 1.5.2.
- Pinned archive: `https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz`,
  sha256 `65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1`
  (recipe `opus-1.5.2-v1`, selected-sources adapter; float build without intrinsics,
  runtime CPU detection, DNN features or custom modes).
- Licence: BSD-3-Clause; `COPYING` also points to the royalty-free patent licences
  declared to the IETF.
- Used by: `arken_audio_codecs_v1` (Opus decoding and encoding).
- Modified: no.

`COPYING` (sha256 `01e1167d54a096d123cf6dfbbeb19587278845c6481d2d66d545669846079551`):

~~~~text
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo, Mozilla, Amazon

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Opus is subject to the royalty-free patent licenses which are
specified at:

Xiph.Org Foundation:
https://datatracker.ietf.org/ipr/1524/

Microsoft Corporation:
https://datatracker.ietf.org/ipr/1914/

Broadcom Corporation:
https://datatracker.ietf.org/ipr/1526/
~~~~

`AUTHORS` (sha256 `f9fd849e652219ce98e008303370b352d37663d0ffb27b8dc514ad93c9c7438b`):

~~~~text
Jean-Marc Valin (jmvalin@jmvalin.ca)
Koen Vos (koenvos74@gmail.com)
Timothy Terriberry (tterribe@xiph.org)
Karsten Vandborg Sorensen (karsten.vandborg.sorensen@skype.net)
Soren Skak Jensen (ssjensen@gn.com)
Gregory Maxwell (greg@xiph.org)
~~~~

## minimp3

- Upstream: https://github.com/lieff/minimp3
- Version: commit `ea99364f61c14656440e8d77e9c233ccf3124633`.
- Pinned source: vendored in `sdk/arken_sdk_dev/third_party/minimp3` (`minimp3.h`,
  `minimp3_ex.h`, `LICENSE`; recipe `minimp3-ea99364-v1`, tracked source). Source-tree
  identity `3c0017c43be58f1415a2bd7095684223369e00be3e68033364c23f282913d025`, recomputed
  and matching.
- Licence: CC0-1.0.
- Used by: `arken_audio_codecs_v1` (MP3 Layer III decoding; compiled with
  `MINIMP3_ONLY_MP3` and `MINIMP3_NO_SIMD` in the first-party `src/minimp3.c`).
- Modified: no.

`sdk/arken_sdk_dev/third_party/minimp3/LICENSE` (sha256 `6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01`):

~~~~text
CC0 1.0 Universal

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator and
subsequent owner(s) (each and all, an "owner") of an original work of
authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for the
purpose of contributing to a commons of creative, cultural and scientific
works ("Commons") that the public can reliably and without fear of later
claims of infringement build upon, modify, incorporate in other works, reuse
and redistribute as freely as possible in any form whatsoever and for any
purposes, including without limitation commercial purposes. These owners may
contribute to the Commons to promote the ideal of a free culture and the
further production of creative, cultural and scientific works, or to gain
reputation or greater distribution for their Work in part through the use and
efforts of others.

For these and/or other purposes and motivations, and without any expectation
of additional consideration or compensation, the person associating CC0 with a
Work (the "Affirmer"), to the extent that he or she is an owner of Copyright
and Related Rights in the Work, voluntarily elects to apply CC0 to the Work
and publicly distribute the Work under its terms, with knowledge of his or her
Copyright and Related Rights in the Work and the meaning and intended legal
effect of CC0 on those rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not limited
to, the following:

  i. the right to reproduce, adapt, distribute, perform, display, communicate,
  and translate a Work;

  ii. moral rights retained by the original author(s) and/or performer(s);

  iii. publicity and privacy rights pertaining to a person's image or likeness
  depicted in a Work;

  iv. rights protecting against unfair competition in regards to a Work,
  subject to the limitations in paragraph 4(a), below;

  v. rights protecting the extraction, dissemination, use and reuse of data in
  a Work;

  vi. database rights (such as those arising under Directive 96/9/EC of the
  European Parliament and of the Council of 11 March 1996 on the legal
  protection of databases, and under any national implementation thereof,
  including any amended or successor version of such directive); and

  vii. other similar, equivalent or corresponding rights throughout the world
  based on applicable law or treaty, and any national implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention of,
applicable law, Affirmer hereby overtly, fully, permanently, irrevocably and
unconditionally waives, abandons, and surrenders all of Affirmer's Copyright
and Related Rights and associated claims and causes of action, whether now
known or unknown (including existing as well as future claims and causes of
action), in the Work (i) in all territories worldwide, (ii) for the maximum
duration provided by applicable law or treaty (including future time
extensions), (iii) in any current or future medium and for any number of
copies, and (iv) for any purpose whatsoever, including without limitation
commercial, advertising or promotional purposes (the "Waiver"). Affirmer makes
the Waiver for the benefit of each member of the public at large and to the
detriment of Affirmer's heirs and successors, fully intending that such Waiver
shall not be subject to revocation, rescission, cancellation, termination, or
any other legal or equitable action to disrupt the quiet enjoyment of the Work
by the public as contemplated by Affirmer's express Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason be
judged legally invalid or ineffective under applicable law, then the Waiver
shall be preserved to the maximum extent permitted taking into account
Affirmer's express Statement of Purpose. In addition, to the extent the Waiver
is so judged Affirmer hereby grants to each affected person a royalty-free,
non transferable, non sublicensable, non exclusive, irrevocable and
unconditional license to exercise Affirmer's Copyright and Related Rights in
the Work (i) in all territories worldwide, (ii) for the maximum duration
provided by applicable law or treaty (including future time extensions), (iii)
in any current or future medium and for any number of copies, and (iv) for any
purpose whatsoever, including without limitation commercial, advertising or
promotional purposes (the "License"). The License shall be deemed effective as
of the date CC0 was applied by Affirmer to the Work. Should any part of the
License for any reason be judged legally invalid or ineffective under
applicable law, such partial invalidity or ineffectiveness shall not
invalidate the remainder of the License, and in such case Affirmer hereby
affirms that he or she will not (i) exercise any of his or her remaining
Copyright and Related Rights in the Work or (ii) assert any associated claims
and causes of action with respect to the Work, in either case contrary to
Affirmer's express Statement of Purpose.

4. Limitations and Disclaimers.

  a. No trademark or patent rights held by Affirmer are waived, abandoned,
  surrendered, licensed or otherwise affected by this document.

  b. Affirmer offers the Work as-is and makes no representations or warranties
  of any kind concerning the Work, express, implied, statutory or otherwise,
  including without limitation warranties of title, merchantability, fitness
  for a particular purpose, non infringement, or the absence of latent or
  other defects, accuracy, or the present or absence of errors, whether or not
  discoverable, all to the greatest extent permissible under applicable law.

  c. Affirmer disclaims responsibility for clearing rights of other persons
  that may apply to the Work or any use thereof, including without limitation
  any person's Copyright and Related Rights in the Work. Further, Affirmer
  disclaims responsibility for obtaining any necessary consents, permissions
  or other rights required for any use of the Work.

  d. Affirmer understands and acknowledges that Creative Commons is not a
  party to this document and has no duty or obligation with respect to this
  CC0 or use of the Work.

For more information, please see
<http://creativecommons.org/publicdomain/zero/1.0/>

~~~~

## LAME (libmp3lame)

- Upstream: https://lame.sourceforge.io/
- Version: 3.100.
- Pinned archive: `https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz`,
  sha256 `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`
  (recipe `lame-3.100-v1`, selected-sources adapter: the 20 `libmp3lame` C files, no
  frontend, no mpglib decoder, no SIMD or NASM; configuration by the recipe's
  `compileDefinitions` instead of a generated `config.h`).
- Licence: LGPL-2.0-or-later. `COPYING` is the GNU Library General Public License
  version 2; the source headers (e.g. `libmp3lame/lame.c`) grant "version 2 of the License,
  or (at your option) any later version". The licence text is reproduced once in
  [GNU Library General Public License, version 2](#gnu-library-general-public-license-version-2-lame).
  LAME's own `LICENSE` file (an FAQ note, not licence terms) is reproduced there too.
- Used by: `arken_audio_codecs_v1` (MP3 encoding with a LAME gapless tag; tools
  `wav_to_mp3`, `flac_to_mp3` and `convert_audio` with an `mp3` output).
- Linking: statically linked into `arken-convert-commands`. See `LGPL_COMPLIANCE.md`.
- Modified: no source file is changed. The build compiles a selected file list with
  explicit preprocessor definitions (`STDC_HEADERS`, `HAVE_ERRNO_H`, `HAVE_FCNTL_H`,
  `HAVE_INTTYPES_H`, `HAVE_STDINT_H`, `HAVE_LIMITS_H`, `HAVE_STDLIB_H`, `HAVE_STRING_H`,
  `HAVE_STRCHR`, `HAVE_MEMCPY`, `ieee754_float32_t=float`, `LAME_LIBRARY_BUILD`), recorded
  in `sdk/arken_sdk_dev/external_packages/recipes/lame-3.100-v1.json`.
- Licence files: `COPYING` sha256 `bfe4a52dc4645385f356a8e83cc54216a293e3b6f1cb4f79f5fc0277abf937fd`,
  `LICENSE` sha256 `7a40cbd8818c57e5285e83c772a459a996d1662c824a879836b97b473c07cae4`.

## nlohmann/json

- Upstream: https://github.com/nlohmann/json
- Version: 3.12.0 (header version macros), snapshot of upstream commit
  `f8eee1bb7953c6a4bff384d45052d5acc3d69698` as recorded in
  `sdk/arken_sdk_dev/cmake/ArkenPinnedNlohmannJson.cmake`.
- Pinned source: vendored in `sdk/arken_sdk_dev/third_party/nlohmann`; the build refuses
  any other content: `json.hpp` sha256
  `acaa0c0e8cb75bbb2001ef3312549140f0ede093cf6f772683b612db75ecd004`, `json_fwd.hpp`
  sha256 `93d2c8f52015dfc200249a42a94c488df73c3c1a73139915ae66d34e314e656d` (both
  recomputed and matching).
- Licence: MIT.
- Used by: `arken_mcp_protocol_v1` (JSON-RPC over stdio), `arken_schema_core_v1` and the
  `arken_schema_cpp_v1` runtime (tool schemas and argument binding).
- Modified: no.

`sdk/arken_sdk_dev/third_party/nlohmann/LICENSE.MIT` (sha256 `8838e206e44184072a772982558b267ab22c6161375fca7fa21de61cfde570f7`):

~~~~text
MIT License

Copyright (c) 2013-2026 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
~~~~

## libheif

- Upstream: https://github.com/strukturag/libheif
- Version: 1.23.7.
- Pinned archive: `https://github.com/strukturag/libheif/releases/download/v1.23.7/libheif-1.23.7.tar.gz`,
  sha256 `f7483e818503ea6224c8863e4a56ac02c7089021a98472c811c12aa19faf2475`. The server uses
  recipe `libheif-1.23.7-hevc-av1-v1` (target `arken::external_heif_v1`, static, plugin
  loading off, only the libde265 decoder and the libaom decoder and encoder enabled;
  depends on `libde265-1.1.3-v2` and `libaom-3.15.3-v1`).
- Licence: LGPL-3.0-or-later. `COPYING` states that the library is under the GNU LGPL and
  that the sample applications and the Go and C++ wrappers are MIT (none of those are
  compiled; the capsule uses the C API only); the source headers grant "version 3 of the
  License, or (at your option) any later version". `COPYING` (sha256
  `fa81ce652315b013359d6e8e4744335f31a50c7c192907176d3632f78a3b4596`) is reproduced once in
  [GNU Lesser General Public License, version 3](#gnu-lesser-general-public-license-version-3-libheif-and-libde265);
  it carries the LGPL-3.0 text, the GNU GPL-3.0 text it incorporates, and the MIT text. An
  identical copy ships in `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/COPYING`
  and `.../libheif-1.23.7-av1/COPYING`.
- Used by: `arken_image_codecs_v1` with `ARKEN_IMAGE_CODECS_V1_HEIF` ON: memory-only HEIC
  decoding (`src/heif.cpp`) and still-AVIF decoding and encoding (`src/avif.cpp`) through
  `src/heif_common.cpp`. Not admitted by any MCP tool (development qualification only).
- Linking: statically linked into `arken-convert-commands`. See `LGPL_COMPLIANCE.md`.
- Modified: yes, six exact-file replacements. On the native server build only the two
  CMake files and an added `#include <stdexcept>` take effect; the other changes are
  confined to Emscripten builds.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7-av1/CMakeLists.txt` (sha256 `f4852157314d03574ad811e83ce67b29121d5568f5f313ed2c0d21990069afe9`) replaces `CMakeLists.txt`: `find_package(LIBDE265)` and `find_package(AOM)` are
    replaced by the pinned `arken::external_de265_v1` and `arken::external_aom_v1` targets
    (configuration fails if they are not resolved), so no system copy can be picked up.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/libheif/CMakeLists.txt` (sha256 `15877dc9bf6cb17fcec086c87d3016f7a11ac2a439d060a69d3733e128fc028b`) replaces `libheif/CMakeLists.txt`: the public build-interface include directories
    point at `libheif/api` and the libheif binary directory, so consumers inside the same
    CMake graph see the public headers.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/libheif/plugins/decoder_libde265.cc` (sha256 `18e3f081bad0d737de9da25e1e1749fded89b974d292f98ac4350a8d4318b24d`) replaces `libheif/plugins/decoder_libde265.cc`: in Emscripten builds upstream turns
    off HEVC deblocking and SAO for speed; the replacement keeps these normative filters on,
    so browser output matches native output.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/libheif/box.cc` (sha256 `44ea92bef06bb436c7a24f5f67529aaec234265423075cbd68a81900f8bf391f`) replaces `libheif/box.cc`: adds `#include <stdexcept>`; in standalone-Wasm builds
    the temporary-file path is compiled out and requesting it throws.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/libheif/box.h` (sha256 `439c567fd20e6f60c5da13432114f6123f1625587243ea0aac1c471aeee4a91a`) replaces `libheif/box.h`: in standalone-Wasm builds `m_use_tmpfile` is a
    `constexpr false`.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libheif-1.23.7/libheif/file.cc` (sha256 `f611f3179c51149fac62822047f5554230eff702cbff95a2c529bbd5becfc637`) replaces `libheif/file.cc`: in standalone-Wasm builds reading from a file path
    returns `heif_error_Unsupported_feature` (memory-only decoder).

  The unified diffs `patches/libheif-1.23.7-pinned-decoder.patch` and
  `patches/libheif-1.23.7-memory-only.patch` are the authoring references for these
  replacements; no recipe applies them.

## libde265

- Upstream: https://github.com/strukturag/libde265
- Version: 1.1.3.
- Pinned archive: `https://github.com/strukturag/libde265/releases/download/v1.1.3/libde265-1.1.3.tar.gz`,
  sha256 `554228bd17788c99a7e63b37ab5634722190e6e2bf60c1dcb01cef328e133905`. The server uses
  recipe `libde265-1.1.3-v2` (target `arken::external_de265_v1`, static, library only: no
  decoder/encoder programs, no SDL, no SIMD).
- Licence: LGPL-3.0-or-later (source headers: "version 3 of the License, or (at your
  option) any later version"). `COPYING` (sha256
  `02cc1585a20677992e0ba578fa692635dc193735f2691dc81de924b51c4e8020`) differs from
  libheif's only in its first two lines, quoted verbatim here; from line 3 on it is
  identical (checked line by line) to the text reproduced in
  [GNU Lesser General Public License, version 3](#gnu-lesser-general-public-license-version-3-libheif-and-libde265).
  Copies ship in `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libde265-1.1.3/COPYING`
  and `.../libde265-1.1.3-v2/COPYING`.

~~~~text
* The library `libde265` is distributed under the terms of the GNU Lesser General Public License.
* The sample applications are distributed under the terms of the MIT license.
~~~~

- Used by: libheif's HEVC decoder plugin (compiled in, not dynamically loaded) for HEIC.
- Linking: statically linked into `arken-convert-commands`. See `LGPL_COMPLIANCE.md`.
- Modified: yes, two exact-file replacements, build-system only (no compiled source changes):
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libde265-1.1.3-v2/CMakeLists.txt` (sha256 `19adc1c9197c65b03b9d6652baffba5e0929b8cd940830e46ee629b1373fe1e8`) replaces `CMakeLists.txt`: the maintainer `dist` tarball target is defined only in a
    top-level libde265 build, because libaom defines a `dist` target too and both share one
    CMake graph.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libde265-1.1.3/libde265/CMakeLists.txt` (sha256 `2d0d94661f48312cab4029ef5b9da6d4f38a3bdc392aae231c71f2cb12721de8`) replaces `libde265/CMakeLists.txt`: adds public build-interface include directories
    to the `de265` target so libheif finds the headers through the target.

  The unified diff `patches/libde265-1.1.3-target-includes.patch` is the authoring
  reference for the second replacement; no recipe applies it.

## libaom

- Upstream: https://aomedia.googlesource.com/aom
- Version: 3.15.3.
- Pinned archive: `https://storage.googleapis.com/aom-releases/libaom-3.15.3.tar.gz`,
  sha256 `12c01dc731ed7b5a999e28120b45bf58a002ba3435a4ba2cfbc1e084a27c9c04` (recipe
  `libaom-3.15.3-v1`, cmake-source adapter: static, `AOM_TARGET_CPU=generic`, AV1 decoder
  and encoder, 8-bit only, no runtime CPU detection, no apps, tests, tools or docs,
  `-ffp-contract=off`).
- Licence: BSD-2-Clause together with the Alliance for Open Media Patent License 1.0
  (`LICENSE` and `PATENTS`, both reproduced in full below).
- Used by: libheif's AV1 decoder and encoder (compiled in) for still AVIF.
- Linking: statically linked into `arken-convert-commands`.
- Modified: yes, four exact-file replacements, build-system only (no codec source changes):
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libaom-3.15.3/CMakeLists.txt` (sha256 `c85faa7cf364bf57fff14d0e601466c596dbe3f996a3d9103b4533fc35a5b15f`) replaces `CMakeLists.txt`: saves and restores the consumer's
    `CMAKE_<LANG>_FLAGS*` cache entries around libaom's configuration (upstream forces its
    own flags into them, which would leak into every other target), and exposes only the
    public `aom/*.h` headers to consumers through a copied include directory.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libaom-3.15.3/cmake/aom_configure.cmake` (sha256 `040ae115f1df8007b26e9766ddb05087da2a8dd7f77aeca8223b0494cb66f251`) replaces `cmake/aom_configure.cmake`: drops the Git probe and the Perl `rtcd.pl`
    step; the three RTCD headers (`aom_dsp_rtcd.h`, `aom_scale_rtcd.h`, `av1_rtcd.h`) are
    embedded as the exact output of upstream `rtcd.pl --arch=generic` for this
    configuration, with digests, and configuration is refused if the options would change
    that output.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libaom-3.15.3/cmake/aom_optimization.cmake` (sha256 `0158565f876c8d21f2e8f0e79764b115ee136c4fe11279f6c4b1168c26168f38`) replaces `cmake/aom_optimization.cmake`: `add_rtcd_build_step` no longer runs Perl;
    it requires the pinned header written at configure time.
  - `sdk/arken_sdk_dev/external_packages/recipes/patches/generated/libaom-3.15.3/cmake/version.cmake` (sha256 `f9021f564c7e7049bd11bb4cd7f90559b8506f835198c15aa9894f38a4824046`) replaces `cmake/version.cmake`: reads the release version from `CHANGELOG` in CMake
    (no Git, no `version.pl`) and writes a byte-identical `aom_version.h`.

`LICENSE` (sha256 `4764a286d8b2faeaf42f4418e7d7a28d58fc8fd4d00a3d0a7f44b0a4099de7f2`):

~~~~text
Copyright (c) 2016, Alliance for Open Media. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

~~~~

`PATENTS` (sha256 `661fb8e504744e95587b556b94a58343448300606a41bea8c7a9b97125696e61`):

~~~~text
Alliance for Open Media Patent License 1.0

1. License Terms.

1.1. Patent License. Subject to the terms and conditions of this License, each
     Licensor, on behalf of itself and successors in interest and assigns,
     grants Licensee a non-sublicensable, perpetual, worldwide, non-exclusive,
     no-charge, royalty-free, irrevocable (except as expressly stated in this
     License) patent license to its Necessary Claims to make, use, sell, offer
     for sale, import or distribute any Implementation.

1.2. Conditions.

1.2.1. Availability. As a condition to the grant of rights to Licensee to make,
       sell, offer for sale, import or distribute an Implementation under
       Section 1.1, Licensee must make its Necessary Claims available under
       this License, and must reproduce this License with any Implementation
       as follows:

       a. For distribution in source code, by including this License in the
          root directory of the source code with its Implementation.

       b. For distribution in any other form (including binary, object form,
          and/or hardware description code (e.g., HDL, RTL, Gate Level Netlist,
          GDSII, etc.)), by including this License in the documentation, legal
          notices, and/or other written materials provided with the
          Implementation.

1.2.2. Additional Conditions. This license is directly from Licensor to
       Licensee.  Licensee acknowledges as a condition of benefiting from it
       that no rights from Licensor are received from suppliers, distributors,
       or otherwise in connection with this License.

1.3. Defensive Termination. If any Licensee, its Affiliates, or its agents
     initiates patent litigation or files, maintains, or voluntarily
     participates in a lawsuit against another entity or any person asserting
     that any Implementation infringes Necessary Claims, any patent licenses
     granted under this License directly to the Licensee are immediately
     terminated as of the date of the initiation of action unless 1) that suit
     was in response to a corresponding suit regarding an Implementation first
     brought against an initiating entity, or 2) that suit was brought to
     enforce the terms of this License (including intervention in a third-party
     action by a Licensee).

1.4. Disclaimers. The Reference Implementation and Specification are provided
     "AS IS" and without warranty. The entire risk as to implementing or
     otherwise using the Reference Implementation or Specification is assumed
     by the implementer and user. Licensor expressly disclaims any warranties
     (express, implied, or otherwise), including implied warranties of
     merchantability, non-infringement, fitness for a particular purpose, or
     title, related to the material. IN NO EVENT WILL LICENSOR BE LIABLE TO
     ANY OTHER PARTY FOR LOST PROFITS OR ANY FORM OF INDIRECT, SPECIAL,
     INCIDENTAL, OR CONSEQUENTIAL DAMAGES OF ANY CHARACTER FROM ANY CAUSES OF
     ACTION OF ANY KIND WITH RESPECT TO THIS LICENSE, WHETHER BASED ON BREACH
     OF CONTRACT, TORT (INCLUDING NEGLIGENCE), OR OTHERWISE, AND WHETHER OR
     NOT THE OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

2. Definitions.

2.1. Affiliate.  "Affiliate" means an entity that directly or indirectly
     Controls, is Controlled by, or is under common Control of that party.

2.2. Control. "Control" means direct or indirect control of more than 50% of
     the voting power to elect directors of that corporation, or for any other
     entity, the power to direct management of such entity.

2.3. Decoder.  "Decoder" means any decoder that conforms fully with all
     non-optional portions of the Specification.

2.4. Encoder.  "Encoder" means any encoder that produces a bitstream that can
     be decoded by a Decoder only to the extent it produces such a bitstream.

2.5. Final Deliverable.  "Final Deliverable" means the final version of a
     deliverable approved by the Alliance for Open Media as a Final
     Deliverable.

2.6. Implementation.  "Implementation" means any implementation, including the
     Reference Implementation, that is an Encoder and/or a Decoder. An
     Implementation also includes components of an Implementation only to the
     extent they are used as part of an Implementation.

2.7. License. "License" means this license.

2.8. Licensee. "Licensee" means any person or entity who exercises patent
     rights granted under this License.

2.9. Licensor.  "Licensor" means (i) any Licensee that makes, sells, offers
     for sale, imports or distributes any Implementation, or (ii) a person
     or entity that has a licensing obligation to the Implementation as a
     result of its membership and/or participation in the Alliance for Open
     Media working group that developed the Specification.

2.10. Necessary Claims.  "Necessary Claims" means all claims of patents or
      patent applications, (a) that currently or at any time in the future,
      are owned or controlled by the Licensor, and (b) (i) would be an
      Essential Claim as defined by the W3C Policy as of February 5, 2004
      (https://www.w3.org/Consortium/Patent-Policy-20040205/#def-essential)
      as if the Specification was a W3C Recommendation; or (ii) are infringed
      by the Reference Implementation.

2.11. Reference Implementation. "Reference Implementation" means an Encoder
      and/or Decoder released by the Alliance for Open Media as a Final
      Deliverable.

2.12. Specification. "Specification" means the specification designated by
      the Alliance for Open Media as a Final Deliverable for which this
      License was issued.

~~~~

## Shared licence texts

### GNU Lesser General Public License, version 3 (libheif and libde265)

The complete `COPYING` file of libheif 1.23.7, verbatim. It contains the GNU LGPL
version 3, the GNU GPL version 3 that the LGPL incorporates by reference (LGPL-3.0 section 4
requires both to accompany the object code), and the MIT licence of libheif's sample
applications and wrappers (not compiled here). libde265's `COPYING` is the same file except
for its first two lines, quoted in the [libde265](#libde265) section.

`libheif-1.23.7/COPYING` (sha256 `fa81ce652315b013359d6e8e4744335f31a50c7c192907176d3632f78a3b4596`):

~~~~text
* The library `libheif` is distributed under the terms of the GNU Lesser General Public License.
* The sample applications and the Go and C++ wrappers are distributed under the terms of the MIT License.

License texts below and in the `COPYING` files of the corresponding subfolders.

----------------------------------------------------------------------

                   GNU LESSER GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <http://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.


  This version of the GNU Lesser General Public License incorporates
the terms and conditions of version 3 of the GNU General Public
License, supplemented by the additional permissions listed below.

  0. Additional Definitions.

  As used herein, "this License" refers to version 3 of the GNU Lesser
General Public License, and the "GNU GPL" refers to version 3 of the GNU
General Public License.

  "The Library" refers to a covered work governed by this License,
other than an Application or a Combined Work as defined below.

  An "Application" is any work that makes use of an interface provided
by the Library, but which is not otherwise based on the Library.
Defining a subclass of a class defined by the Library is deemed a mode
of using an interface provided by the Library.

  A "Combined Work" is a work produced by combining or linking an
Application with the Library.  The particular version of the Library
with which the Combined Work was made is also called the "Linked
Version".

  The "Minimal Corresponding Source" for a Combined Work means the
Corresponding Source for the Combined Work, excluding any source code
for portions of the Combined Work that, considered in isolation, are
based on the Application, and not on the Linked Version.

  The "Corresponding Application Code" for a Combined Work means the
object code and/or source code for the Application, including any data
and utility programs needed for reproducing the Combined Work from the
Application, but excluding the System Libraries of the Combined Work.

  1. Exception to Section 3 of the GNU GPL.

  You may convey a covered work under sections 3 and 4 of this License
without being bound by section 3 of the GNU GPL.

  2. Conveying Modified Versions.

  If you modify a copy of the Library, and, in your modifications, a
facility refers to a function or data to be supplied by an Application
that uses the facility (other than as an argument passed when the
facility is invoked), then you may convey a copy of the modified
version:

   a) under this License, provided that you make a good faith effort to
   ensure that, in the event an Application does not supply the
   function or data, the facility still operates, and performs
   whatever part of its purpose remains meaningful, or

   b) under the GNU GPL, with none of the additional permissions of
   this License applicable to that copy.

  3. Object Code Incorporating Material from Library Header Files.

  The object code form of an Application may incorporate material from
a header file that is part of the Library.  You may convey such object
code under terms of your choice, provided that, if the incorporated
material is not limited to numerical parameters, data structure
layouts and accessors, or small macros, inline functions and templates
(ten or fewer lines in length), you do both of the following:

   a) Give prominent notice with each copy of the object code that the
   Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the object code with a copy of the GNU GPL and this license
   document.

  4. Combined Works.

  You may convey a Combined Work under terms of your choice that,
taken together, effectively do not restrict modification of the
portions of the Library contained in the Combined Work and reverse
engineering for debugging such modifications, if you also do each of
the following:

   a) Give prominent notice with each copy of the Combined Work that
   the Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the Combined Work with a copy of the GNU GPL and this license
   document.

   c) For a Combined Work that displays copyright notices during
   execution, include the copyright notice for the Library among
   these notices, as well as a reference directing the user to the
   copies of the GNU GPL and this license document.

   d) Do one of the following:

       0) Convey the Minimal Corresponding Source under the terms of this
       License, and the Corresponding Application Code in a form
       suitable for, and under terms that permit, the user to
       recombine or relink the Application with a modified version of
       the Linked Version to produce a modified Combined Work, in the
       manner specified by section 6 of the GNU GPL for conveying
       Corresponding Source.

       1) Use a suitable shared library mechanism for linking with the
       Library.  A suitable mechanism is one that (a) uses at run time
       a copy of the Library already present on the user's computer
       system, and (b) will operate properly with a modified version
       of the Library that is interface-compatible with the Linked
       Version.

   e) Provide Installation Information, but only if you would otherwise
   be required to provide such information under section 6 of the
   GNU GPL, and only to the extent that such information is
   necessary to install and execute a modified version of the
   Combined Work produced by recombining or relinking the
   Application with a modified version of the Linked Version. (If
   you use option 4d0, the Installation Information must accompany
   the Minimal Corresponding Source and Corresponding Application
   Code. If you use option 4d1, you must provide the Installation
   Information in the manner specified by section 6 of the GNU GPL
   for conveying Corresponding Source.)

  5. Combined Libraries.

  You may place library facilities that are a work based on the
Library side by side in a single library together with other library
facilities that are not Applications and are not covered by this
License, and convey such a combined library under terms of your
choice, if you do both of the following:

   a) Accompany the combined library with a copy of the same work based
   on the Library, uncombined with any other library facilities,
   conveyed under the terms of this License.

   b) Give prominent notice with the combined library that part of it
   is a work based on the Library, and explaining where to find the
   accompanying uncombined form of the same work.

  6. Revised Versions of the GNU Lesser General Public License.

  The Free Software Foundation may publish revised and/or new versions
of the GNU Lesser General Public License from time to time. Such new
versions will be similar in spirit to the present version, but may
differ in detail to address new problems or concerns.

  Each version is given a distinguishing version number. If the
Library as you received it specifies that a certain numbered version
of the GNU Lesser General Public License "or any later version"
applies to it, you have the option of following the terms and
conditions either of that published version or of any later version
published by the Free Software Foundation. If the Library as you
received it does not specify a version number of the GNU Lesser
General Public License, you may choose any version of the GNU Lesser
General Public License ever published by the Free Software Foundation.

  If the Library as you received it specifies that a proxy can decide
whether future versions of the GNU Lesser General Public License shall
apply, that proxy's public statement of acceptance of any version is
permanent authorization for you to choose that version for the
Library.

----------------------------------------------------------------------

                    GNU GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <http://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.

                            Preamble

  The GNU General Public License is a free, copyleft license for
software and other kinds of works.

  The licenses for most software and other practical works are designed
to take away your freedom to share and change the works.  By contrast,
the GNU General Public License is intended to guarantee your freedom to
share and change all versions of a program--to make sure it remains free
software for all its users.  We, the Free Software Foundation, use the
GNU General Public License for most of our software; it applies also to
any other work released this way by its authors.  You can apply it to
your programs, too.

  When we speak of free software, we are referring to freedom, not
price.  Our General Public Licenses are designed to make sure that you
have the freedom to distribute copies of free software (and charge for
them if you wish), that you receive source code or can get it if you
want it, that you can change the software or use pieces of it in new
free programs, and that you know you can do these things.

  To protect your rights, we need to prevent others from denying you
these rights or asking you to surrender the rights.  Therefore, you have
certain responsibilities if you distribute copies of the software, or if
you modify it: responsibilities to respect the freedom of others.

  For example, if you distribute copies of such a program, whether
gratis or for a fee, you must pass on to the recipients the same
freedoms that you received.  You must make sure that they, too, receive
or can get the source code.  And you must show them these terms so they
know their rights.

  Developers that use the GNU GPL protect your rights with two steps:
(1) assert copyright on the software, and (2) offer you this License
giving you legal permission to copy, distribute and/or modify it.

  For the developers' and authors' protection, the GPL clearly explains
that there is no warranty for this free software.  For both users' and
authors' sake, the GPL requires that modified versions be marked as
changed, so that their problems will not be attributed erroneously to
authors of previous versions.

  Some devices are designed to deny users access to install or run
modified versions of the software inside them, although the manufacturer
can do so.  This is fundamentally incompatible with the aim of
protecting users' freedom to change the software.  The systematic
pattern of such abuse occurs in the area of products for individuals to
use, which is precisely where it is most unacceptable.  Therefore, we
have designed this version of the GPL to prohibit the practice for those
products.  If such problems arise substantially in other domains, we
stand ready to extend this provision to those domains in future versions
of the GPL, as needed to protect the freedom of users.

  Finally, every program is threatened constantly by software patents.
States should not allow patents to restrict development and use of
software on general-purpose computers, but in those that do, we wish to
avoid the special danger that patents applied to a free program could
make it effectively proprietary.  To prevent this, the GPL assures that
patents cannot be used to render the program non-free.

  The precise terms and conditions for copying, distribution and
modification follow.

                       TERMS AND CONDITIONS

  0. Definitions.

  "This License" refers to version 3 of the GNU General Public License.

  "Copyright" also means copyright-like laws that apply to other kinds of
works, such as semiconductor masks.

  "The Program" refers to any copyrightable work licensed under this
License.  Each licensee is addressed as "you".  "Licensees" and
"recipients" may be individuals or organizations.

  To "modify" a work means to copy from or adapt all or part of the work
in a fashion requiring copyright permission, other than the making of an
exact copy.  The resulting work is called a "modified version" of the
earlier work or a work "based on" the earlier work.

  A "covered work" means either the unmodified Program or a work based
on the Program.

  To "propagate" a work means to do anything with it that, without
permission, would make you directly or secondarily liable for
infringement under applicable copyright law, except executing it on a
computer or modifying a private copy.  Propagation includes copying,
distribution (with or without modification), making available to the
public, and in some countries other activities as well.

  To "convey" a work means any kind of propagation that enables other
parties to make or receive copies.  Mere interaction with a user through
a computer network, with no transfer of a copy, is not conveying.

  An interactive user interface displays "Appropriate Legal Notices"
to the extent that it includes a convenient and prominently visible
feature that (1) displays an appropriate copyright notice, and (2)
tells the user that there is no warranty for the work (except to the
extent that warranties are provided), that licensees may convey the
work under this License, and how to view a copy of this License.  If
the interface presents a list of user commands or options, such as a
menu, a prominent item in the list meets this criterion.

  1. Source Code.

  The "source code" for a work means the preferred form of the work
for making modifications to it.  "Object code" means any non-source
form of a work.

  A "Standard Interface" means an interface that either is an official
standard defined by a recognized standards body, or, in the case of
interfaces specified for a particular programming language, one that
is widely used among developers working in that language.

  The "System Libraries" of an executable work include anything, other
than the work as a whole, that (a) is included in the normal form of
packaging a Major Component, but which is not part of that Major
Component, and (b) serves only to enable use of the work with that
Major Component, or to implement a Standard Interface for which an
implementation is available to the public in source code form.  A
"Major Component", in this context, means a major essential component
(kernel, window system, and so on) of the specific operating system
(if any) on which the executable work runs, or a compiler used to
produce the work, or an object code interpreter used to run it.

  The "Corresponding Source" for a work in object code form means all
the source code needed to generate, install, and (for an executable
work) run the object code and to modify the work, including scripts to
control those activities.  However, it does not include the work's
System Libraries, or general-purpose tools or generally available free
programs which are used unmodified in performing those activities but
which are not part of the work.  For example, Corresponding Source
includes interface definition files associated with source files for
the work, and the source code for shared libraries and dynamically
linked subprograms that the work is specifically designed to require,
such as by intimate data communication or control flow between those
subprograms and other parts of the work.

  The Corresponding Source need not include anything that users
can regenerate automatically from other parts of the Corresponding
Source.

  The Corresponding Source for a work in source code form is that
same work.

  2. Basic Permissions.

  All rights granted under this License are granted for the term of
copyright on the Program, and are irrevocable provided the stated
conditions are met.  This License explicitly affirms your unlimited
permission to run the unmodified Program.  The output from running a
covered work is covered by this License only if the output, given its
content, constitutes a covered work.  This License acknowledges your
rights of fair use or other equivalent, as provided by copyright law.

  You may make, run and propagate covered works that you do not
convey, without conditions so long as your license otherwise remains
in force.  You may convey covered works to others for the sole purpose
of having them make modifications exclusively for you, or provide you
with facilities for running those works, provided that you comply with
the terms of this License in conveying all material for which you do
not control copyright.  Those thus making or running the covered works
for you must do so exclusively on your behalf, under your direction
and control, on terms that prohibit them from making any copies of
your copyrighted material outside their relationship with you.

  Conveying under any other circumstances is permitted solely under
the conditions stated below.  Sublicensing is not allowed; section 10
makes it unnecessary.

  3. Protecting Users' Legal Rights From Anti-Circumvention Law.

  No covered work shall be deemed part of an effective technological
measure under any applicable law fulfilling obligations under article
11 of the WIPO copyright treaty adopted on 20 December 1996, or
similar laws prohibiting or restricting circumvention of such
measures.

  When you convey a covered work, you waive any legal power to forbid
circumvention of technological measures to the extent such circumvention
is effected by exercising rights under this License with respect to
the covered work, and you disclaim any intention to limit operation or
modification of the work as a means of enforcing, against the work's
users, your or third parties' legal rights to forbid circumvention of
technological measures.

  4. Conveying Verbatim Copies.

  You may convey verbatim copies of the Program's source code as you
receive it, in any medium, provided that you conspicuously and
appropriately publish on each copy an appropriate copyright notice;
keep intact all notices stating that this License and any
non-permissive terms added in accord with section 7 apply to the code;
keep intact all notices of the absence of any warranty; and give all
recipients a copy of this License along with the Program.

  You may charge any price or no price for each copy that you convey,
and you may offer support or warranty protection for a fee.

  5. Conveying Modified Source Versions.

  You may convey a work based on the Program, or the modifications to
produce it from the Program, in the form of source code under the
terms of section 4, provided that you also meet all of these conditions:

    a) The work must carry prominent notices stating that you modified
    it, and giving a relevant date.

    b) The work must carry prominent notices stating that it is
    released under this License and any conditions added under section
    7.  This requirement modifies the requirement in section 4 to
    "keep intact all notices".

    c) You must license the entire work, as a whole, under this
    License to anyone who comes into possession of a copy.  This
    License will therefore apply, along with any applicable section 7
    additional terms, to the whole of the work, and all its parts,
    regardless of how they are packaged.  This License gives no
    permission to license the work in any other way, but it does not
    invalidate such permission if you have separately received it.

    d) If the work has interactive user interfaces, each must display
    Appropriate Legal Notices; however, if the Program has interactive
    interfaces that do not display Appropriate Legal Notices, your
    work need not make them do so.

  A compilation of a covered work with other separate and independent
works, which are not by their nature extensions of the covered work,
and which are not combined with it such as to form a larger program,
in or on a volume of a storage or distribution medium, is called an
"aggregate" if the compilation and its resulting copyright are not
used to limit the access or legal rights of the compilation's users
beyond what the individual works permit.  Inclusion of a covered work
in an aggregate does not cause this License to apply to the other
parts of the aggregate.

  6. Conveying Non-Source Forms.

  You may convey a covered work in object code form under the terms
of sections 4 and 5, provided that you also convey the
machine-readable Corresponding Source under the terms of this License,
in one of these ways:

    a) Convey the object code in, or embodied in, a physical product
    (including a physical distribution medium), accompanied by the
    Corresponding Source fixed on a durable physical medium
    customarily used for software interchange.

    b) Convey the object code in, or embodied in, a physical product
    (including a physical distribution medium), accompanied by a
    written offer, valid for at least three years and valid for as
    long as you offer spare parts or customer support for that product
    model, to give anyone who possesses the object code either (1) a
    copy of the Corresponding Source for all the software in the
    product that is covered by this License, on a durable physical
    medium customarily used for software interchange, for a price no
    more than your reasonable cost of physically performing this
    conveying of source, or (2) access to copy the
    Corresponding Source from a network server at no charge.

    c) Convey individual copies of the object code with a copy of the
    written offer to provide the Corresponding Source.  This
    alternative is allowed only occasionally and noncommercially, and
    only if you received the object code with such an offer, in accord
    with subsection 6b.

    d) Convey the object code by offering access from a designated
    place (gratis or for a charge), and offer equivalent access to the
    Corresponding Source in the same way through the same place at no
    further charge.  You need not require recipients to copy the
    Corresponding Source along with the object code.  If the place to
    copy the object code is a network server, the Corresponding Source
    may be on a different server (operated by you or a third party)
    that supports equivalent copying facilities, provided you maintain
    clear directions next to the object code saying where to find the
    Corresponding Source.  Regardless of what server hosts the
    Corresponding Source, you remain obligated to ensure that it is
    available for as long as needed to satisfy these requirements.

    e) Convey the object code using peer-to-peer transmission, provided
    you inform other peers where the object code and Corresponding
    Source of the work are being offered to the general public at no
    charge under subsection 6d.

  A separable portion of the object code, whose source code is excluded
from the Corresponding Source as a System Library, need not be
included in conveying the object code work.

  A "User Product" is either (1) a "consumer product", which means any
tangible personal property which is normally used for personal, family,
or household purposes, or (2) anything designed or sold for incorporation
into a dwelling.  In determining whether a product is a consumer product,
doubtful cases shall be resolved in favor of coverage.  For a particular
product received by a particular user, "normally used" refers to a
typical or common use of that class of product, regardless of the status
of the particular user or of the way in which the particular user
actually uses, or expects or is expected to use, the product.  A product
is a consumer product regardless of whether the product has substantial
commercial, industrial or non-consumer uses, unless such uses represent
the only significant mode of use of the product.

  "Installation Information" for a User Product means any methods,
procedures, authorization keys, or other information required to install
and execute modified versions of a covered work in that User Product from
a modified version of its Corresponding Source.  The information must
suffice to ensure that the continued functioning of the modified object
code is in no case prevented or interfered with solely because
modification has been made.

  If you convey an object code work under this section in, or with, or
specifically for use in, a User Product, and the conveying occurs as
part of a transaction in which the right of possession and use of the
User Product is transferred to the recipient in perpetuity or for a
fixed term (regardless of how the transaction is characterized), the
Corresponding Source conveyed under this section must be accompanied
by the Installation Information.  But this requirement does not apply
if neither you nor any third party retains the ability to install
modified object code on the User Product (for example, the work has
been installed in ROM).

  The requirement to provide Installation Information does not include a
requirement to continue to provide support service, warranty, or updates
for a work that has been modified or installed by the recipient, or for
the User Product in which it has been modified or installed.  Access to a
network may be denied when the modification itself materially and
adversely affects the operation of the network or violates the rules and
protocols for communication across the network.

  Corresponding Source conveyed, and Installation Information provided,
in accord with this section must be in a format that is publicly
documented (and with an implementation available to the public in
source code form), and must require no special password or key for
unpacking, reading or copying.

  7. Additional Terms.

  "Additional permissions" are terms that supplement the terms of this
License by making exceptions from one or more of its conditions.
Additional permissions that are applicable to the entire Program shall
be treated as though they were included in this License, to the extent
that they are valid under applicable law.  If additional permissions
apply only to part of the Program, that part may be used separately
under those permissions, but the entire Program remains governed by
this License without regard to the additional permissions.

  When you convey a copy of a covered work, you may at your option
remove any additional permissions from that copy, or from any part of
it.  (Additional permissions may be written to require their own
removal in certain cases when you modify the work.)  You may place
additional permissions on material, added by you to a covered work,
for which you have or can give appropriate copyright permission.

  Notwithstanding any other provision of this License, for material you
add to a covered work, you may (if authorized by the copyright holders of
that material) supplement the terms of this License with terms:

    a) Disclaiming warranty or limiting liability differently from the
    terms of sections 15 and 16 of this License; or

    b) Requiring preservation of specified reasonable legal notices or
    author attributions in that material or in the Appropriate Legal
    Notices displayed by works containing it; or

    c) Prohibiting misrepresentation of the origin of that material, or
    requiring that modified versions of such material be marked in
    reasonable ways as different from the original version; or

    d) Limiting the use for publicity purposes of names of licensors or
    authors of the material; or

    e) Declining to grant rights under trademark law for use of some
    trade names, trademarks, or service marks; or

    f) Requiring indemnification of licensors and authors of that
    material by anyone who conveys the material (or modified versions of
    it) with contractual assumptions of liability to the recipient, for
    any liability that these contractual assumptions directly impose on
    those licensors and authors.

  All other non-permissive additional terms are considered "further
restrictions" within the meaning of section 10.  If the Program as you
received it, or any part of it, contains a notice stating that it is
governed by this License along with a term that is a further
restriction, you may remove that term.  If a license document contains
a further restriction but permits relicensing or conveying under this
License, you may add to a covered work material governed by the terms
of that license document, provided that the further restriction does
not survive such relicensing or conveying.

  If you add terms to a covered work in accord with this section, you
must place, in the relevant source files, a statement of the
additional terms that apply to those files, or a notice indicating
where to find the applicable terms.

  Additional terms, permissive or non-permissive, may be stated in the
form of a separately written license, or stated as exceptions;
the above requirements apply either way.

  8. Termination.

  You may not propagate or modify a covered work except as expressly
provided under this License.  Any attempt otherwise to propagate or
modify it is void, and will automatically terminate your rights under
this License (including any patent licenses granted under the third
paragraph of section 11).

  However, if you cease all violation of this License, then your
license from a particular copyright holder is reinstated (a)
provisionally, unless and until the copyright holder explicitly and
finally terminates your license, and (b) permanently, if the copyright
holder fails to notify you of the violation by some reasonable means
prior to 60 days after the cessation.

  Moreover, your license from a particular copyright holder is
reinstated permanently if the copyright holder notifies you of the
violation by some reasonable means, this is the first time you have
received notice of violation of this License (for any work) from that
copyright holder, and you cure the violation prior to 30 days after
your receipt of the notice.

  Termination of your rights under this section does not terminate the
licenses of parties who have received copies or rights from you under
this License.  If your rights have been terminated and not permanently
reinstated, you do not qualify to receive new licenses for the same
material under section 10.

  9. Acceptance Not Required for Having Copies.

  You are not required to accept this License in order to receive or
run a copy of the Program.  Ancillary propagation of a covered work
occurring solely as a consequence of using peer-to-peer transmission
to receive a copy likewise does not require acceptance.  However,
nothing other than this License grants you permission to propagate or
modify any covered work.  These actions infringe copyright if you do
not accept this License.  Therefore, by modifying or propagating a
covered work, you indicate your acceptance of this License to do so.

  10. Automatic Licensing of Downstream Recipients.

  Each time you convey a covered work, the recipient automatically
receives a license from the original licensors, to run, modify and
propagate that work, subject to this License.  You are not responsible
for enforcing compliance by third parties with this License.

  An "entity transaction" is a transaction transferring control of an
organization, or substantially all assets of one, or subdividing an
organization, or merging organizations.  If propagation of a covered
work results from an entity transaction, each party to that
transaction who receives a copy of the work also receives whatever
licenses to the work the party's predecessor in interest had or could
give under the previous paragraph, plus a right to possession of the
Corresponding Source of the work from the predecessor in interest, if
the predecessor has it or can get it with reasonable efforts.

  You may not impose any further restrictions on the exercise of the
rights granted or affirmed under this License.  For example, you may
not impose a license fee, royalty, or other charge for exercise of
rights granted under this License, and you may not initiate litigation
(including a cross-claim or counterclaim in a lawsuit) alleging that
any patent claim is infringed by making, using, selling, offering for
sale, or importing the Program or any portion of it.

  11. Patents.

  A "contributor" is a copyright holder who authorizes use under this
License of the Program or a work on which the Program is based.  The
work thus licensed is called the contributor's "contributor version".

  A contributor's "essential patent claims" are all patent claims
owned or controlled by the contributor, whether already acquired or
hereafter acquired, that would be infringed by some manner, permitted
by this License, of making, using, or selling its contributor version,
but do not include claims that would be infringed only as a
consequence of further modification of the contributor version.  For
purposes of this definition, "control" includes the right to grant
patent sublicenses in a manner consistent with the requirements of
this License.

  Each contributor grants you a non-exclusive, worldwide, royalty-free
patent license under the contributor's essential patent claims, to
make, use, sell, offer for sale, import and otherwise run, modify and
propagate the contents of its contributor version.

  In the following three paragraphs, a "patent license" is any express
agreement or commitment, however denominated, not to enforce a patent
(such as an express permission to practice a patent or covenant not to
sue for patent infringement).  To "grant" such a patent license to a
party means to make such an agreement or commitment not to enforce a
patent against the party.

  If you convey a covered work, knowingly relying on a patent license,
and the Corresponding Source of the work is not available for anyone
to copy, free of charge and under the terms of this License, through a
publicly available network server or other readily accessible means,
then you must either (1) cause the Corresponding Source to be so
available, or (2) arrange to deprive yourself of the benefit of the
patent license for this particular work, or (3) arrange, in a manner
consistent with the requirements of this License, to extend the patent
license to downstream recipients.  "Knowingly relying" means you have
actual knowledge that, but for the patent license, your conveying the
covered work in a country, or your recipient's use of the covered work
in a country, would infringe one or more identifiable patents in that
country that you have reason to believe are valid.

  If, pursuant to or in connection with a single transaction or
arrangement, you convey, or propagate by procuring conveyance of, a
covered work, and grant a patent license to some of the parties
receiving the covered work authorizing them to use, propagate, modify
or convey a specific copy of the covered work, then the patent license
you grant is automatically extended to all recipients of the covered
work and works based on it.

  A patent license is "discriminatory" if it does not include within
the scope of its coverage, prohibits the exercise of, or is
conditioned on the non-exercise of one or more of the rights that are
specifically granted under this License.  You may not convey a covered
work if you are a party to an arrangement with a third party that is
in the business of distributing software, under which you make payment
to the third party based on the extent of your activity of conveying
the work, and under which the third party grants, to any of the
parties who would receive the covered work from you, a discriminatory
patent license (a) in connection with copies of the covered work
conveyed by you (or copies made from those copies), or (b) primarily
for and in connection with specific products or compilations that
contain the covered work, unless you entered into that arrangement,
or that patent license was granted, prior to 28 March 2007.

  Nothing in this License shall be construed as excluding or limiting
any implied license or other defenses to infringement that may
otherwise be available to you under applicable patent law.

  12. No Surrender of Others' Freedom.

  If conditions are imposed on you (whether by court order, agreement or
otherwise) that contradict the conditions of this License, they do not
excuse you from the conditions of this License.  If you cannot convey a
covered work so as to satisfy simultaneously your obligations under this
License and any other pertinent obligations, then as a consequence you may
not convey it at all.  For example, if you agree to terms that obligate you
to collect a royalty for further conveying from those to whom you convey
the Program, the only way you could satisfy both those terms and this
License would be to refrain entirely from conveying the Program.

  13. Use with the GNU Affero General Public License.

  Notwithstanding any other provision of this License, you have
permission to link or combine any covered work with a work licensed
under version 3 of the GNU Affero General Public License into a single
combined work, and to convey the resulting work.  The terms of this
License will continue to apply to the part which is the covered work,
but the special requirements of the GNU Affero General Public License,
section 13, concerning interaction through a network will apply to the
combination as such.

  14. Revised Versions of this License.

  The Free Software Foundation may publish revised and/or new versions of
the GNU General Public License from time to time.  Such new versions will
be similar in spirit to the present version, but may differ in detail to
address new problems or concerns.

  Each version is given a distinguishing version number.  If the
Program specifies that a certain numbered version of the GNU General
Public License "or any later version" applies to it, you have the
option of following the terms and conditions either of that numbered
version or of any later version published by the Free Software
Foundation.  If the Program does not specify a version number of the
GNU General Public License, you may choose any version ever published
by the Free Software Foundation.

  If the Program specifies that a proxy can decide which future
versions of the GNU General Public License can be used, that proxy's
public statement of acceptance of a version permanently authorizes you
to choose that version for the Program.

  Later license versions may give you additional or different
permissions.  However, no additional obligations are imposed on any
author or copyright holder as a result of your choosing to follow a
later version.

  15. Disclaimer of Warranty.

  THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY
APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT
HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS" WITHOUT WARRANTY
OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM
IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF
ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

  16. Limitation of Liability.

  IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MODIFIES AND/OR CONVEYS
THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY
GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE
USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO LOSS OF
DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD
PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER PROGRAMS),
EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF
SUCH DAMAGES.

  17. Interpretation of Sections 15 and 16.

  If the disclaimer of warranty and limitation of liability provided
above cannot be given local legal effect according to their terms,
reviewing courts shall apply local law that most closely approximates
an absolute waiver of all civil liability in connection with the
Program, unless a warranty or assumption of liability accompanies a
copy of the Program in return for a fee.

                     END OF TERMS AND CONDITIONS

            How to Apply These Terms to Your New Programs

  If you develop a new program, and you want it to be of the greatest
possible use to the public, the best way to achieve this is to make it
free software which everyone can redistribute and change under these terms.

  To do so, attach the following notices to the program.  It is safest
to attach them to the start of each source file to most effectively
state the exclusion of warranty; and each file should have at least
the "copyright" line and a pointer to where the full notice is found.

    <one line to give the program's name and a brief idea of what it does.>
    Copyright (C) <year>  <name of author>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

Also add information on how to contact you by electronic and paper mail.

  If the program does terminal interaction, make it output a short
notice like this when it starts in an interactive mode:

    <program>  Copyright (C) <year>  <name of author>
    This program comes with ABSOLUTELY NO WARRANTY; for details type `show w'.
    This is free software, and you are welcome to redistribute it
    under certain conditions; type `show c' for details.

The hypothetical commands `show w' and `show c' should show the appropriate
parts of the General Public License.  Of course, your program's commands
might be different; for a GUI interface, you would use an "about box".

  You should also get your employer (if you work as a programmer) or school,
if any, to sign a "copyright disclaimer" for the program, if necessary.
For more information on this, and how to apply and follow the GNU GPL, see
<http://www.gnu.org/licenses/>.

  The GNU General Public License does not permit incorporating your program
into proprietary programs.  If your program is a subroutine library, you
may consider it more useful to permit linking proprietary applications with
the library.  If this is what you want to do, use the GNU Lesser General
Public License instead of this License.  But first, please read
<http://www.gnu.org/philosophy/why-not-lgpl.html>.

----------------------------------------------------------------------

                             MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
~~~~

### GNU Library General Public License, version 2 (LAME)

The complete `COPYING` file of LAME 3.100, verbatim, followed by LAME's `LICENSE` note.

`lame-3.100/COPYING` (sha256 `bfe4a52dc4645385f356a8e83cc54216a293e3b6f1cb4f79f5fc0277abf937fd`):

~~~~text
		  GNU LIBRARY GENERAL PUBLIC LICENSE
		       Version 2, June 1991

 Copyright (C) 1991 Free Software Foundation, Inc.
    		    59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.

[This is the first released version of the library GPL.  It is
 numbered 2 because it goes with version 2 of the ordinary GPL.]

			    Preamble

  The licenses for most software are designed to take away your
freedom to share and change it.  By contrast, the GNU General Public
Licenses are intended to guarantee your freedom to share and change
free software--to make sure the software is free for all its users.

  This license, the Library General Public License, applies to some
specially designated Free Software Foundation software, and to any
other libraries whose authors decide to use it.  You can use it for
your libraries, too.

  When we speak of free software, we are referring to freedom, not
price.  Our General Public Licenses are designed to make sure that you
have the freedom to distribute copies of free software (and charge for
this service if you wish), that you receive source code or can get it
if you want it, that you can change the software or use pieces of it
in new free programs; and that you know you can do these things.

  To protect your rights, we need to make restrictions that forbid
anyone to deny you these rights or to ask you to surrender the rights.
These restrictions translate to certain responsibilities for you if
you distribute copies of the library, or if you modify it.

  For example, if you distribute copies of the library, whether gratis
or for a fee, you must give the recipients all the rights that we gave
you.  You must make sure that they, too, receive or can get the source
code.  If you link a program with the library, you must provide
complete object files to the recipients so that they can relink them
with the library, after making changes to the library and recompiling
it.  And you must show them these terms so they know their rights.

  Our method of protecting your rights has two steps: (1) copyright
the library, and (2) offer you this license which gives you legal
permission to copy, distribute and/or modify the library.

  Also, for each distributor's protection, we want to make certain
that everyone understands that there is no warranty for this free
library.  If the library is modified by someone else and passed on, we
want its recipients to know that what they have is not the original
version, so that any problems introduced by others will not reflect on
the original authors' reputations.

  Finally, any free program is threatened constantly by software
patents.  We wish to avoid the danger that companies distributing free
software will individually obtain patent licenses, thus in effect
transforming the program into proprietary software.  To prevent this,
we have made it clear that any patent must be licensed for everyone's
free use or not licensed at all.

  Most GNU software, including some libraries, is covered by the ordinary
GNU General Public License, which was designed for utility programs.  This
license, the GNU Library General Public License, applies to certain
designated libraries.  This license is quite different from the ordinary
one; be sure to read it in full, and don't assume that anything in it is
the same as in the ordinary license.

  The reason we have a separate public license for some libraries is that
they blur the distinction we usually make between modifying or adding to a
program and simply using it.  Linking a program with a library, without
changing the library, is in some sense simply using the library, and is
analogous to running a utility program or application program.  However, in
a textual and legal sense, the linked executable is a combined work, a
derivative of the original library, and the ordinary General Public License
treats it as such.

  Because of this blurred distinction, using the ordinary General
Public License for libraries did not effectively promote software
sharing, because most developers did not use the libraries.  We
concluded that weaker conditions might promote sharing better.

  However, unrestricted linking of non-free programs would deprive the
users of those programs of all benefit from the free status of the
libraries themselves.  This Library General Public License is intended to
permit developers of non-free programs to use free libraries, while
preserving your freedom as a user of such programs to change the free
libraries that are incorporated in them.  (We have not seen how to achieve
this as regards changes in header files, but we have achieved it as regards
changes in the actual functions of the Library.)  The hope is that this
will lead to faster development of free libraries.

  The precise terms and conditions for copying, distribution and
modification follow.  Pay close attention to the difference between a
"work based on the library" and a "work that uses the library".  The
former contains code derived from the library, while the latter only
works together with the library.

  Note that it is possible for a library to be covered by the ordinary
General Public License rather than by this special one.

		  GNU LIBRARY GENERAL PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. This License Agreement applies to any software library which
contains a notice placed by the copyright holder or other authorized
party saying it may be distributed under the terms of this Library
General Public License (also called "this License").  Each licensee is
addressed as "you".

  A "library" means a collection of software functions and/or data
prepared so as to be conveniently linked with application programs
(which use some of those functions and data) to form executables.

  The "Library", below, refers to any such software library or work
which has been distributed under these terms.  A "work based on the
Library" means either the Library or any derivative work under
copyright law: that is to say, a work containing the Library or a
portion of it, either verbatim or with modifications and/or translated
straightforwardly into another language.  (Hereinafter, translation is
included without limitation in the term "modification".)

  "Source code" for a work means the preferred form of the work for
making modifications to it.  For a library, complete source code means
all the source code for all modules it contains, plus any associated
interface definition files, plus the scripts used to control compilation
and installation of the library.

  Activities other than copying, distribution and modification are not
covered by this License; they are outside its scope.  The act of
running a program using the Library is not restricted, and output from
such a program is covered only if its contents constitute a work based
on the Library (independent of the use of the Library in a tool for
writing it).  Whether that is true depends on what the Library does
and what the program that uses the Library does.
  
  1. You may copy and distribute verbatim copies of the Library's
complete source code as you receive it, in any medium, provided that
you conspicuously and appropriately publish on each copy an
appropriate copyright notice and disclaimer of warranty; keep intact
all the notices that refer to this License and to the absence of any
warranty; and distribute a copy of this License along with the
Library.

  You may charge a fee for the physical act of transferring a copy,
and you may at your option offer warranty protection in exchange for a
fee.

  2. You may modify your copy or copies of the Library or any portion
of it, thus forming a work based on the Library, and copy and
distribute such modifications or work under the terms of Section 1
above, provided that you also meet all of these conditions:

    a) The modified work must itself be a software library.

    b) You must cause the files modified to carry prominent notices
    stating that you changed the files and the date of any change.

    c) You must cause the whole of the work to be licensed at no
    charge to all third parties under the terms of this License.

    d) If a facility in the modified Library refers to a function or a
    table of data to be supplied by an application program that uses
    the facility, other than as an argument passed when the facility
    is invoked, then you must make a good faith effort to ensure that,
    in the event an application does not supply such function or
    table, the facility still operates, and performs whatever part of
    its purpose remains meaningful.

    (For example, a function in a library to compute square roots has
    a purpose that is entirely well-defined independent of the
    application.  Therefore, Subsection 2d requires that any
    application-supplied function or table used by this function must
    be optional: if the application does not supply it, the square
    root function must still compute square roots.)

These requirements apply to the modified work as a whole.  If
identifiable sections of that work are not derived from the Library,
and can be reasonably considered independent and separate works in
themselves, then this License, and its terms, do not apply to those
sections when you distribute them as separate works.  But when you
distribute the same sections as part of a whole which is a work based
on the Library, the distribution of the whole must be on the terms of
this License, whose permissions for other licensees extend to the
entire whole, and thus to each and every part regardless of who wrote
it.

Thus, it is not the intent of this section to claim rights or contest
your rights to work written entirely by you; rather, the intent is to
exercise the right to control the distribution of derivative or
collective works based on the Library.

In addition, mere aggregation of another work not based on the Library
with the Library (or with a work based on the Library) on a volume of
a storage or distribution medium does not bring the other work under
the scope of this License.

  3. You may opt to apply the terms of the ordinary GNU General Public
License instead of this License to a given copy of the Library.  To do
this, you must alter all the notices that refer to this License, so
that they refer to the ordinary GNU General Public License, version 2,
instead of to this License.  (If a newer version than version 2 of the
ordinary GNU General Public License has appeared, then you can specify
that version instead if you wish.)  Do not make any other change in
these notices.

  Once this change is made in a given copy, it is irreversible for
that copy, so the ordinary GNU General Public License applies to all
subsequent copies and derivative works made from that copy.

  This option is useful when you wish to copy part of the code of
the Library into a program that is not a library.

  4. You may copy and distribute the Library (or a portion or
derivative of it, under Section 2) in object code or executable form
under the terms of Sections 1 and 2 above provided that you accompany
it with the complete corresponding machine-readable source code, which
must be distributed under the terms of Sections 1 and 2 above on a
medium customarily used for software interchange.

  If distribution of object code is made by offering access to copy
from a designated place, then offering equivalent access to copy the
source code from the same place satisfies the requirement to
distribute the source code, even though third parties are not
compelled to copy the source along with the object code.

  5. A program that contains no derivative of any portion of the
Library, but is designed to work with the Library by being compiled or
linked with it, is called a "work that uses the Library".  Such a
work, in isolation, is not a derivative work of the Library, and
therefore falls outside the scope of this License.

  However, linking a "work that uses the Library" with the Library
creates an executable that is a derivative of the Library (because it
contains portions of the Library), rather than a "work that uses the
library".  The executable is therefore covered by this License.
Section 6 states terms for distribution of such executables.

  When a "work that uses the Library" uses material from a header file
that is part of the Library, the object code for the work may be a
derivative work of the Library even though the source code is not.
Whether this is true is especially significant if the work can be
linked without the Library, or if the work is itself a library.  The
threshold for this to be true is not precisely defined by law.

  If such an object file uses only numerical parameters, data
structure layouts and accessors, and small macros and small inline
functions (ten lines or less in length), then the use of the object
file is unrestricted, regardless of whether it is legally a derivative
work.  (Executables containing this object code plus portions of the
Library will still fall under Section 6.)

  Otherwise, if the work is a derivative of the Library, you may
distribute the object code for the work under the terms of Section 6.
Any executables containing that work also fall under Section 6,
whether or not they are linked directly with the Library itself.

  6. As an exception to the Sections above, you may also compile or
link a "work that uses the Library" with the Library to produce a
work containing portions of the Library, and distribute that work
under terms of your choice, provided that the terms permit
modification of the work for the customer's own use and reverse
engineering for debugging such modifications.

  You must give prominent notice with each copy of the work that the
Library is used in it and that the Library and its use are covered by
this License.  You must supply a copy of this License.  If the work
during execution displays copyright notices, you must include the
copyright notice for the Library among them, as well as a reference
directing the user to the copy of this License.  Also, you must do one
of these things:

    a) Accompany the work with the complete corresponding
    machine-readable source code for the Library including whatever
    changes were used in the work (which must be distributed under
    Sections 1 and 2 above); and, if the work is an executable linked
    with the Library, with the complete machine-readable "work that
    uses the Library", as object code and/or source code, so that the
    user can modify the Library and then relink to produce a modified
    executable containing the modified Library.  (It is understood
    that the user who changes the contents of definitions files in the
    Library will not necessarily be able to recompile the application
    to use the modified definitions.)

    b) Accompany the work with a written offer, valid for at
    least three years, to give the same user the materials
    specified in Subsection 6a, above, for a charge no more
    than the cost of performing this distribution.

    c) If distribution of the work is made by offering access to copy
    from a designated place, offer equivalent access to copy the above
    specified materials from the same place.

    d) Verify that the user has already received a copy of these
    materials or that you have already sent this user a copy.

  For an executable, the required form of the "work that uses the
Library" must include any data and utility programs needed for
reproducing the executable from it.  However, as a special exception,
the source code distributed need not include anything that is normally
distributed (in either source or binary form) with the major
components (compiler, kernel, and so on) of the operating system on
which the executable runs, unless that component itself accompanies
the executable.

  It may happen that this requirement contradicts the license
restrictions of other proprietary libraries that do not normally
accompany the operating system.  Such a contradiction means you cannot
use both them and the Library together in an executable that you
distribute.

  7. You may place library facilities that are a work based on the
Library side-by-side in a single library together with other library
facilities not covered by this License, and distribute such a combined
library, provided that the separate distribution of the work based on
the Library and of the other library facilities is otherwise
permitted, and provided that you do these two things:

    a) Accompany the combined library with a copy of the same work
    based on the Library, uncombined with any other library
    facilities.  This must be distributed under the terms of the
    Sections above.

    b) Give prominent notice with the combined library of the fact
    that part of it is a work based on the Library, and explaining
    where to find the accompanying uncombined form of the same work.

  8. You may not copy, modify, sublicense, link with, or distribute
the Library except as expressly provided under this License.  Any
attempt otherwise to copy, modify, sublicense, link with, or
distribute the Library is void, and will automatically terminate your
rights under this License.  However, parties who have received copies,
or rights, from you under this License will not have their licenses
terminated so long as such parties remain in full compliance.

  9. You are not required to accept this License, since you have not
signed it.  However, nothing else grants you permission to modify or
distribute the Library or its derivative works.  These actions are
prohibited by law if you do not accept this License.  Therefore, by
modifying or distributing the Library (or any work based on the
Library), you indicate your acceptance of this License to do so, and
all its terms and conditions for copying, distributing or modifying
the Library or works based on it.

  10. Each time you redistribute the Library (or any work based on the
Library), the recipient automatically receives a license from the
original licensor to copy, distribute, link with or modify the Library
subject to these terms and conditions.  You may not impose any further
restrictions on the recipients' exercise of the rights granted herein.
You are not responsible for enforcing compliance by third parties to
this License.

  11. If, as a consequence of a court judgment or allegation of patent
infringement or for any other reason (not limited to patent issues),
conditions are imposed on you (whether by court order, agreement or
otherwise) that contradict the conditions of this License, they do not
excuse you from the conditions of this License.  If you cannot
distribute so as to satisfy simultaneously your obligations under this
License and any other pertinent obligations, then as a consequence you
may not distribute the Library at all.  For example, if a patent
license would not permit royalty-free redistribution of the Library by
all those who receive copies directly or indirectly through you, then
the only way you could satisfy both it and this License would be to
refrain entirely from distribution of the Library.

If any portion of this section is held invalid or unenforceable under any
particular circumstance, the balance of the section is intended to apply,
and the section as a whole is intended to apply in other circumstances.

It is not the purpose of this section to induce you to infringe any
patents or other property right claims or to contest validity of any
such claims; this section has the sole purpose of protecting the
integrity of the free software distribution system which is
implemented by public license practices.  Many people have made
generous contributions to the wide range of software distributed
through that system in reliance on consistent application of that
system; it is up to the author/donor to decide if he or she is willing
to distribute software through any other system and a licensee cannot
impose that choice.

This section is intended to make thoroughly clear what is believed to
be a consequence of the rest of this License.

  12. If the distribution and/or use of the Library is restricted in
certain countries either by patents or by copyrighted interfaces, the
original copyright holder who places the Library under this License may add
an explicit geographical distribution limitation excluding those countries,
so that distribution is permitted only in or among countries not thus
excluded.  In such case, this License incorporates the limitation as if
written in the body of this License.

  13. The Free Software Foundation may publish revised and/or new
versions of the Library General Public License from time to time.
Such new versions will be similar in spirit to the present version,
but may differ in detail to address new problems or concerns.

Each version is given a distinguishing version number.  If the Library
specifies a version number of this License which applies to it and
"any later version", you have the option of following the terms and
conditions either of that version or of any later version published by
the Free Software Foundation.  If the Library does not specify a
license version number, you may choose any version ever published by
the Free Software Foundation.

  14. If you wish to incorporate parts of the Library into other free
programs whose distribution conditions are incompatible with these,
write to the author to ask for permission.  For software which is
copyrighted by the Free Software Foundation, write to the Free
Software Foundation; we sometimes make exceptions for this.  Our
decision will be guided by the two goals of preserving the free status
of all derivatives of our free software and of promoting the sharing
and reuse of software generally.

			    NO WARRANTY

  15. BECAUSE THE LIBRARY IS LICENSED FREE OF CHARGE, THERE IS NO
WARRANTY FOR THE LIBRARY, TO THE EXTENT PERMITTED BY APPLICABLE LAW.
EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR
OTHER PARTIES PROVIDE THE LIBRARY "AS IS" WITHOUT WARRANTY OF ANY
KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
LIBRARY IS WITH YOU.  SHOULD THE LIBRARY PROVE DEFECTIVE, YOU ASSUME
THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

  16. IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN
WRITING WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY
AND/OR REDISTRIBUTE THE LIBRARY AS PERMITTED ABOVE, BE LIABLE TO YOU
FOR DAMAGES, INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR
CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR INABILITY TO USE THE
LIBRARY (INCLUDING BUT NOT LIMITED TO LOSS OF DATA OR DATA BEING
RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD PARTIES OR A
FAILURE OF THE LIBRARY TO OPERATE WITH ANY OTHER SOFTWARE), EVEN IF
SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.

		     END OF TERMS AND CONDITIONS

           How to Apply These Terms to Your New Libraries

  If you develop a new library, and you want it to be of the greatest
possible use to the public, we recommend making it free software that
everyone can redistribute and change.  You can do so by permitting
redistribution under these terms (or, alternatively, under the terms of the
ordinary General Public License).

  To apply these terms, attach the following notices to the library.  It is
safest to attach them to the start of each source file to most effectively
convey the exclusion of warranty; and each file should have at least the
"copyright" line and a pointer to where the full notice is found.

    <one line to give the library's name and a brief idea of what it does.>
    Copyright (C) <year>  <name of author>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Also add information on how to contact you by electronic and paper mail.

You should also get your employer (if you work as a programmer) or your
school, if any, to sign a "copyright disclaimer" for the library, if
necessary.  Here is a sample; alter the names:

  Yoyodyne, Inc., hereby disclaims all copyright interest in the
  library `Frob' (a library for tweaking knobs) written by James Random Hacker.

  <signature of Ty Coon>, 1 April 1990
  Ty Coon, President of Vice

That's all there is to it!
~~~~

`lame-3.100/LICENSE` (sha256 `7a40cbd8818c57e5285e83c772a459a996d1662c824a879836b97b473c07cae4`):

~~~~text
Can I use LAME in my commercial program?  

Yes, you can, under the restrictions of the LGPL (see COPYING
in this folder). The easiest way to do this is to:

1. Link to LAME as separate library (libmp3lame.a on unix or 
   lame_enc.dll or libmp3lame.dll on windows)

2. Fully acknowledge that you are using LAME, and give a link
   to our web site, www.mp3dev.org

3. If you make modifications to LAME, you *must* release these
   modifications back to the LAME project, under the LGPL.
~~~~
