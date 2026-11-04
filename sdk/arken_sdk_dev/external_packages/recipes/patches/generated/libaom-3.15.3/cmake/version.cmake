#
# Copyright (c) 2017, Alliance for Open Media. All rights reserved.
#
# This source code is subject to the terms of the BSD 2 Clause License and the
# Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at www.aomedia.org/license/software. If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at www.aomedia.org/license/patent.
#
# Arken pinned build: the upstream script asks Git for a description and then
# runs cmake/version.pl. A pinned release archive has no repository, so this
# CMake-only replacement reads the newest CHANGELOG release exactly as
# version.pl does and writes byte-identical aom_version.h (VERSION_EXTRA "").
cmake_minimum_required(VERSION 3.16)

foreach(arg AOM_ROOT AOM_CONFIG_DIR)
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

file(STRINGS "${AOM_ROOT}/CHANGELOG" changelog_lines)
set(aom_version "")
foreach(line IN LISTS changelog_lines)
  if(line MATCHES "^[ \t]*[^ \t]+[ \t]+(v[^ \t]*)")
    set(aom_version "${CMAKE_MATCH_1}")
    break()
  endif()
endforeach()
if(NOT aom_version MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "Unable to read a release version from ${AOM_ROOT}/CHANGELOG.")
endif()
set(version_major "${CMAKE_MATCH_1}")
set(version_minor "${CMAKE_MATCH_2}")
set(version_patch "${CMAKE_MATCH_3}")
string(SUBSTRING "${aom_version}" 1 -1 version_string)

set(version_file "${AOM_CONFIG_DIR}/config/aom_version.h")
string(CONCAT version_text
  "/*\n"
  " * Copyright (c) 2016, Alliance for Open Media. All rights reserved.\n"
  " *\n"
  " * This source code is subject to the terms of the BSD 2 Clause License and\n"
  " * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License\n"
  " * was not distributed with this source code in the LICENSE file, you can\n"
  " * obtain it at www.aomedia.org/license/software. If the Alliance for Open\n"
  " * Media Patent License 1.0 was not distributed with this source code in the\n"
  " * PATENTS file, you can obtain it at www.aomedia.org/license/patent.\n"
  " */\n"
  "\n"
  "#ifndef AOM_VERSION_H_\n"
  "#define AOM_VERSION_H_\n"
  "#define VERSION_MAJOR ${version_major}\n"
  "#define VERSION_MINOR ${version_minor}\n"
  "#define VERSION_PATCH ${version_patch}\n"
  "#define VERSION_EXTRA \"\"\n"
  "#define VERSION_PACKED \\\n"
  "  ((VERSION_MAJOR << 16) | (VERSION_MINOR << 8) | (VERSION_PATCH))\n"
  "#define VERSION_STRING_NOSP \"v${version_string}\"\n"
  "#define VERSION_STRING \" v${version_string}\"\n"
  "#endif  // AOM_VERSION_H_\n")
file(WRITE "${version_file}.arken-candidate" "${version_text}")
file(COPY_FILE "${version_file}.arken-candidate" "${version_file}" ONLY_IF_DIFFERENT)
file(REMOVE "${version_file}.arken-candidate")
