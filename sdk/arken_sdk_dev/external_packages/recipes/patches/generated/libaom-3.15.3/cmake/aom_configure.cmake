#
# Copyright (c) 2016, Alliance for Open Media. All rights reserved.
#
# This source code is subject to the terms of the BSD 2 Clause License and the
# Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at www.aomedia.org/license/software. If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at www.aomedia.org/license/patent.
#
if(AOM_BUILD_CMAKE_AOM_CONFIGURE_CMAKE_)
  return()
endif() # AOM_BUILD_CMAKE_AOM_CONFIGURE_CMAKE_
set(AOM_BUILD_CMAKE_AOM_CONFIGURE_CMAKE_ 1)

include(FindThreads)
include(CheckSymbolExists)

include("${AOM_ROOT}/cmake/aom_config_defaults.cmake")
include("${AOM_ROOT}/cmake/aom_experiment_deps.cmake")
include("${AOM_ROOT}/cmake/aom_optimization.cmake")
include("${AOM_ROOT}/cmake/compiler_flags.cmake")
include("${AOM_ROOT}/cmake/compiler_tests.cmake")
include("${AOM_ROOT}/cmake/util.cmake")

if(DEFINED CONFIG_LOWBITDEPTH)
  message(WARNING "CONFIG_LOWBITDEPTH has been removed. \
    Use -DFORCE_HIGHBITDEPTH_DECODING=1 instead of -DCONFIG_LOWBITDEPTH=0 \
    and -DFORCE_HIGHBITDEPTH_DECODING=0 instead of -DCONFIG_LOWBITDEPTH=1.")
  if(NOT CONFIG_LOWBITDEPTH)
    set(FORCE_HIGHBITDEPTH_DECODING
        1
        CACHE STRING "${cmake_cmdline_helpstring}" FORCE)
  endif()
endif()

if(FORCE_HIGHBITDEPTH_DECODING AND NOT CONFIG_AV1_HIGHBITDEPTH)
  change_config_and_warn(CONFIG_AV1_HIGHBITDEPTH 1
                         "FORCE_HIGHBITDEPTH_DECODING")
endif()

if(CONFIG_THREE_PASS AND NOT CONFIG_AV1_DECODER)
  change_config_and_warn(CONFIG_THREE_PASS 0 "CONFIG_AV1_DECODER=0")
endif()

# Generate the user config settings.
list(APPEND aom_build_vars ${AOM_CONFIG_VARS} ${AOM_OPTION_VARS})
foreach(cache_var ${aom_build_vars})
  get_property(cache_var_helpstring CACHE ${cache_var} PROPERTY HELPSTRING)
  if(cache_var_helpstring STREQUAL cmake_cmdline_helpstring)
    set(AOM_CMAKE_CONFIG "${AOM_CMAKE_CONFIG} -D${cache_var}=${${cache_var}}")
  endif()
endforeach()
string(STRIP "${AOM_CMAKE_CONFIG}" AOM_CMAKE_CONFIG)

# Detect target CPU.
if(NOT AOM_TARGET_CPU)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cpu_lowercase)
  if(cpu_lowercase STREQUAL "amd64" OR cpu_lowercase STREQUAL "x86_64")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      set(AOM_TARGET_CPU "x86")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(AOM_TARGET_CPU "x86_64")
    else()
      message(
        FATAL_ERROR "--- Unexpected pointer size (${CMAKE_SIZEOF_VOID_P}) for\n"
                    "      CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}\n"
                    "      CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}\n"
                    "      CMAKE_GENERATOR=${CMAKE_GENERATOR}\n")
    endif()
  elseif(cpu_lowercase STREQUAL "i386" OR cpu_lowercase STREQUAL "x86")
    set(AOM_TARGET_CPU "x86")
  elseif(cpu_lowercase MATCHES "^arm")
    set(AOM_TARGET_CPU "${cpu_lowercase}")
  elseif(cpu_lowercase MATCHES "aarch64")
    set(AOM_TARGET_CPU "arm64")
  elseif(cpu_lowercase MATCHES "^ppc")
    set(AOM_TARGET_CPU "ppc")
  elseif(cpu_lowercase MATCHES "^riscv")
    set(AOM_TARGET_CPU "riscv")
  else()
    message(WARNING "The architecture ${CMAKE_SYSTEM_PROCESSOR} is not "
                    "supported, falling back to the generic target")
    set(AOM_TARGET_CPU "generic")
  endif()
endif()

if(CMAKE_TOOLCHAIN_FILE) # Add toolchain file to config string.
  if(IS_ABSOLUTE "${CMAKE_TOOLCHAIN_FILE}")
    file(RELATIVE_PATH toolchain_path "${AOM_CONFIG_DIR}"
         "${CMAKE_TOOLCHAIN_FILE}")
  else()
    set(toolchain_path "${CMAKE_TOOLCHAIN_FILE}")
  endif()
  set(toolchain_string "-DCMAKE_TOOLCHAIN_FILE=\\\"${toolchain_path}\\\"")
  set(AOM_CMAKE_CONFIG "${toolchain_string} ${AOM_CMAKE_CONFIG}")
else()

  # Add detected CPU to the config string.
  set(AOM_CMAKE_CONFIG "-DAOM_TARGET_CPU=${AOM_TARGET_CPU} ${AOM_CMAKE_CONFIG}")
endif()
set(AOM_CMAKE_CONFIG "-G \\\"${CMAKE_GENERATOR}\\\" ${AOM_CMAKE_CONFIG}")
file(RELATIVE_PATH source_path "${AOM_CONFIG_DIR}" "${AOM_ROOT}")
set(AOM_CMAKE_CONFIG "cmake ${source_path} ${AOM_CMAKE_CONFIG}")
string(STRIP "${AOM_CMAKE_CONFIG}" AOM_CMAKE_CONFIG)

message("--- aom_configure: Detected CPU: ${AOM_TARGET_CPU}")
set(AOM_TARGET_SYSTEM ${CMAKE_SYSTEM_NAME})

string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type_lowercase)
if(build_type_lowercase STREQUAL "debug")
  set(CONFIG_DEBUG 1)
endif()

if(BUILD_SHARED_LIBS)
  set(CONFIG_PIC 1)
  set(CONFIG_SHARED 1)
elseif(NOT CONFIG_PIC)
  # Update the variable only when it does not carry the CMake assigned help
  # string for variables specified via the command line. This allows the user to
  # force CONFIG_PIC=0.
  unset(cache_helpstring)
  get_property(cache_helpstring CACHE CONFIG_PIC PROPERTY HELPSTRING)
  if(NOT "${cache_helpstring}" STREQUAL "${cmake_cmdline_helpstring}")
    aom_check_c_compiles("pie_check" "
                          #if !(__pie__ || __PIE__)
                          #error Neither __pie__ or __PIE__ are set
                          #endif
                          extern void unused(void);
                          void unused(void) {}" HAVE_PIE)

    if(HAVE_PIE)
      # If -fpie or -fPIE are used ensure the assembly code has PIC enabled to
      # avoid DT_TEXTRELs: /usr/bin/ld: warning: creating DT_TEXTREL in a PIE
      set(CONFIG_PIC 1)
      message(
        "CONFIG_PIC enabled for position independent executable (PIE) build")
    endif()
  endif()
  unset(cache_helpstring)
endif()

if(NOT MSVC)
  if(CONFIG_PIC)

    # TODO(tomfinegan): clang needs -pie in CMAKE_EXE_LINKER_FLAGS for this to
    # work.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(AOM_TARGET_SYSTEM STREQUAL "Linux"
       AND AOM_TARGET_CPU MATCHES "^armv[78]")
      set(AOM_AS_FLAGS ${AOM_AS_FLAGS} --defsym PIC=1)
    else()
      set(AOM_AS_FLAGS ${AOM_AS_FLAGS} -DPIC)
    endif()
  endif()
endif()

if(AOM_TARGET_CPU STREQUAL "x86" OR AOM_TARGET_CPU STREQUAL "x86_64")
  find_program(CMAKE_ASM_NASM_COMPILER yasm $ENV{YASM_PATH})
  if(NOT CMAKE_ASM_NASM_COMPILER OR ENABLE_NASM)
    unset(CMAKE_ASM_NASM_COMPILER CACHE)
    find_program(CMAKE_ASM_NASM_COMPILER nasm $ENV{NASM_PATH})
  endif()

  include(CheckLanguage)
  check_language(ASM_NASM)
  if(CMAKE_ASM_NASM_COMPILER)
    get_asm_obj_format("objformat")
    unset(CMAKE_ASM_NASM_OBJECT_FORMAT)
    set(CMAKE_ASM_NASM_OBJECT_FORMAT ${objformat})
    enable_language(ASM_NASM)
    if(CMAKE_ASM_NASM_COMPILER_ID STREQUAL "NASM")
      test_nasm()
    endif()
    # Xcode requires building the objects manually, so pass the object format
    # flag.
    if(XCODE)
      set(AOM_AS_FLAGS -f ${objformat} ${AOM_AS_FLAGS})
    endif()
  else()
    message(
      FATAL_ERROR
        "Unable to find assembler. Install 'yasm' or 'nasm.' "
        "To build without optimizations, add -DAOM_TARGET_CPU=generic to "
        "your cmake command line.")
  endif()
  string(STRIP "${AOM_AS_FLAGS}" AOM_AS_FLAGS)
elseif(AOM_TARGET_CPU MATCHES "arm")
  if(AOM_TARGET_SYSTEM STREQUAL "Darwin")
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
    endif()
    set(AOM_AS_FLAGS -arch ${AOM_TARGET_CPU} -isysroot ${CMAKE_OSX_SYSROOT})
  elseif(AOM_TARGET_SYSTEM STREQUAL "Windows")
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER} "-c -mimplicit-it=always")
    endif()
  else()
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER as)
    endif()
  endif()
  include(CheckLanguage)
  check_language(ASM)
  if(NOT CMAKE_ASM_COMPILER)
    message(
      FATAL_ERROR
        "Unable to find assembler and optimizations are enabled."
        "Searched for ${CMAKE_ASM_COMPILER}. Install it, add it to your path,"
        "or set the assembler directly by adding "
        "-DCMAKE_ASM_COMPILER=<assembler path> to your CMake command line."
        "To build without optimizations, add -DAOM_TARGET_CPU=generic to your "
        "cmake command line.")
  endif()
  enable_language(ASM)
  string(STRIP "${AOM_AS_FLAGS}" AOM_AS_FLAGS)
endif()

if(CONFIG_ANALYZER)
  find_package(wxWidgets REQUIRED adv base core)
  include(${wxWidgets_USE_FILE})
endif()

if(NOT MSVC AND CMAKE_C_COMPILER_ID MATCHES "GNU\|Clang")
  set(CONFIG_GCC 1)
endif()

if(CONFIG_GCOV)
  message("--- Testing for CONFIG_GCOV support.")
  require_linker_flag("-fprofile-arcs -ftest-coverage")
  require_compiler_flag("-fprofile-arcs -ftest-coverage" YES)
endif()

if(CONFIG_GPROF)
  message("--- Testing for CONFIG_GPROF support.")
  require_compiler_flag("-pg" YES)
endif()

if(AOM_TARGET_SYSTEM MATCHES "Darwin\|Linux\|Windows\|Android")
  set(CONFIG_OS_SUPPORT 1)
endif()

# Define macros that affect Windows headers.
if(AOM_TARGET_SYSTEM STREQUAL "Windows")
  # The default _WIN32_WINNT value in MinGW is 0x0502 (Windows XP with SP2). Set
  # it to 0x0601 (Windows 7).
  add_compiler_flag_if_supported("-D_WIN32_WINNT=0x0601")
  # Quiet warnings related to fopen, printf, etc.
  add_compiler_flag_if_supported("-D_CRT_SECURE_NO_WARNINGS")
endif()

#
# Fix CONFIG_* dependencies. This must be done before including cpu.cmake to
# ensure RTCD_CONFIG_* are properly set.
fix_experiment_configs()

# Don't just check for pthread.h, but use the result of the full pthreads
# including a linking check in FindThreads above.
set(HAVE_PTHREAD_H ${CMAKE_USE_PTHREADS_INIT})
aom_check_source_compiles("unistd_check" "#include <unistd.h>" HAVE_UNISTD_H)

if(HAVE_PTHREAD_H)
  # Check for pthread setname_np API
  aom_push_var(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
  check_symbol_exists(pthread_setname_np pthread.h _HAS_PTHREAD_SETNAME_NP)
  aom_pop_var(CMAKE_REQUIRED_DEFINITIONS)

  # Since https://gitlab.kitware.com/cmake/cmake/-/commit/cebcc07a80 (included
  # in 3.30.0), check_symbol_exists() will modify CMAKE_<LANG>_FLAGS{,_<CONFIG>}
  # and then restore the values afterward. This will create a normal variable
  # which will override the cached variable libaom uses in checking for flags.
  # Due to the level of scope involved, it's easier to clear the normal variable
  # rather than try to propagate the value up through the intermediate scopes.
  if(CMAKE_TRY_COMPILE_CONFIGURATION)
    string(TOUPPER "${CMAKE_TRY_COMPILE_CONFIGURATION}" tc_config)
  else()
    set(tc_config "DEBUG")
  endif()
  set(tc_config "CMAKE_C_FLAGS_${tc_config}")
  foreach(config ${AOM_C_CONFIGS})
    if(${config} STREQUAL ${tc_config})
      unset(${config})
      break()
    endif()
  endforeach()
  unset(tc_config)

  set(HAVE_PTHREAD_SETNAME_NP ${_HAS_PTHREAD_SETNAME_NP})
endif()

if(NOT WIN32)
  aom_push_var(CMAKE_REQUIRED_LIBRARIES "m")
  aom_check_c_compiles("fenv_check" "#define _GNU_SOURCE
                        #include <fenv.h>
                        void unused(void) {
                          (void)unused;
                          (void)feenableexcept(FE_DIVBYZERO | FE_INVALID);
                        }" HAVE_FEXCEPT)
  aom_pop_var(CMAKE_REQUIRED_LIBRARIES)
endif()

include("${AOM_ROOT}/cmake/cpu.cmake")

if(ENABLE_CCACHE)
  set_compiler_launcher(ENABLE_CCACHE ccache)
endif()

if(ENABLE_DISTCC)
  set_compiler_launcher(ENABLE_DISTCC distcc)
endif()

if(ENABLE_GOMA)
  set_compiler_launcher(ENABLE_GOMA gomacc)
endif()

if(NOT CONFIG_AV1_DECODER AND NOT CONFIG_AV1_ENCODER)
  message(FATAL_ERROR "Decoder and encoder disabled, nothing to build.")
endif()

if(DECODE_HEIGHT_LIMIT OR DECODE_WIDTH_LIMIT)
  change_config_and_warn(CONFIG_SIZE_LIMIT 1
                         "DECODE_HEIGHT_LIMIT and DECODE_WIDTH_LIMIT")
endif()

if(CONFIG_SIZE_LIMIT)
  if(NOT DECODE_HEIGHT_LIMIT OR NOT DECODE_WIDTH_LIMIT)
    message(FATAL_ERROR "When setting CONFIG_SIZE_LIMIT, DECODE_HEIGHT_LIMIT "
                        "and DECODE_WIDTH_LIMIT must be set.")
  endif()
endif()

# Test compiler flags.
if(MSVC)
  # C11 conformance is available starting in Visual Studio 2019 version 16.8.
  require_c_flag("/std:c11" YES)
  add_cxx_flag_if_supported("/std:c++17")
  add_compiler_flag_if_supported("/W3")

  # Disable MSVC warnings that suggest making code non-portable.
  add_compiler_flag_if_supported("/wd4996")
  if(ENABLE_WERROR)
    add_compiler_flag_if_supported("/WX")
  endif()

  # Compile source files in parallel
  add_compiler_flag_if_supported("/MP")
else()
  require_c_flag("-std=c11" YES)
  if(CYGWIN AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # The GNU C++ compiler in Cygwin needs the -std=gnu++* flag to make the
    # POSIX function declarations visible in the Standard C Library headers.
    require_cxx_flag_nomsvc("-std=gnu++17" YES)
  else()
    require_cxx_flag_nomsvc("-std=c++17" YES)
  endif()
  add_compiler_flag_if_supported("-Wall")
  add_compiler_flag_if_supported("-Wdisabled-optimization")
  add_compiler_flag_if_supported("-Wextra")
  # Prior to version 3.19.0 cmake would fail to parse the warning emitted by gcc
  # with this flag. Note the order of this check and -Wextra-semi-stmt is
  # important due to is_flag_present() matching substrings with string(FIND
  # ...).
  if(CMAKE_VERSION VERSION_LESS "3.19"
     AND CMAKE_C_COMPILER_ID STREQUAL "GNU"
     AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 10)
    add_cxx_flag_if_supported("-Wextra-semi")
  else()
    add_compiler_flag_if_supported("-Wextra-semi")
  endif()
  add_compiler_flag_if_supported("-Wextra-semi-stmt")
  add_compiler_flag_if_supported("-Wfloat-conversion")
  add_compiler_flag_if_supported("-Wformat=2")
  add_c_flag_if_supported("-Wimplicit-function-declaration")
  add_compiler_flag_if_supported("-Wlogical-op")
  add_compiler_flag_if_supported("-Wmissing-declarations")
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compiler_flag_if_supported("-Wmissing-prototypes")
  else()
    add_c_flag_if_supported("-Wmissing-prototypes")
  endif()
  add_compiler_flag_if_supported("-Wpointer-arith")
  add_compiler_flag_if_supported("-Wshadow")
  add_compiler_flag_if_supported("-Wshorten-64-to-32")
  add_compiler_flag_if_supported("-Wsign-compare")
  add_compiler_flag_if_supported("-Wstring-conversion")
  add_compiler_flag_if_supported("-Wtype-limits")
  add_compiler_flag_if_supported("-Wundef")
  add_compiler_flag_if_supported("-Wuninitialized")
  add_compiler_flag_if_supported("-Wunreachable-code-aggressive")
  add_compiler_flag_if_supported("-Wunused")
  add_compiler_flag_if_supported("-Wvla")
  add_c_flag_if_supported("-Wc23-extensions")
  add_cxx_flag_if_supported("-Wc++20-extensions")
  add_cxx_flag_if_supported("-Wc++23-extensions")

  if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND SANITIZE MATCHES "address|undefined")

    # This combination has more stack overhead, so we account for it by
    # providing higher stack limit than usual.
    add_c_flag_if_supported("-Wstack-usage=285000")
    add_cxx_flag_if_supported("-Wstack-usage=270000")
  elseif(CONFIG_RD_DEBUG) # Another case where higher stack usage is expected.
    add_c_flag_if_supported("-Wstack-usage=135000")
    add_cxx_flag_if_supported("-Wstack-usage=240000")
  else()
    add_c_flag_if_supported("-Wstack-usage=100000")
    add_cxx_flag_if_supported("-Wstack-usage=240000")
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND SANITIZE MATCHES "address")
    # Disable no optimization warning when compiling with sanitizers
    add_compiler_flag_if_supported("-Wno-disabled-optimization")
  endif()

  # Quiet gcc 6 vs 7 abi warnings:
  # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77728
  if(AOM_TARGET_CPU MATCHES "arm")
    add_cxx_flag_if_supported("-Wno-psabi")
  endif()

  if(ENABLE_WERROR)
    add_compiler_flag_if_supported("-Werror")
  endif()

  add_compiler_flag_if_supported("-D_LARGEFILE_SOURCE")
  add_compiler_flag_if_supported("-D_FILE_OFFSET_BITS=64")

  # Do not allow implicit vector type conversions on Clang builds (this is
  # already the default on GCC builds).
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Clang 8.0.1 (in Cygwin) doesn't support -flax-vector-conversions=none.
    add_compiler_flag_if_supported("-flax-vector-conversions=none")
  endif()
endif()

# Prior to r23, or with ANDROID_USE_LEGACY_TOOLCHAIN_FILE set,
# android.toolchain.cmake would set normal (non-cache) versions of variables
# like CMAKE_C_FLAGS_RELEASE which would mask the ones added to the cache
# variable in add_compiler_flag_if_supported(), etc. As a workaround we add
# everything accumulated in AOM_C/CXX_FLAGS to the normal versions. This could
# also be addressed by reworking the flag tests and adding the results directly
# to target_compile_options() as in e.g., libgav1, but that's a larger task.
# https://github.com/android/ndk/wiki/Changelog-r23#changes
if(ANDROID
   AND ("${ANDROID_NDK_MAJOR}" LESS 23 OR ANDROID_USE_LEGACY_TOOLCHAIN_FILE))
  foreach(lang C;CXX)
    string(STRIP "${AOM_${lang}_FLAGS}" AOM_${lang}_FLAGS)
    if(AOM_${lang}_FLAGS)
      foreach(config ${AOM_${lang}_CONFIGS})
        set(${config} "${${config}} ${AOM_${lang}_FLAGS}")
      endforeach()
    endif()
  endforeach()
endif()

set(AOM_LIB_LINK_TYPE PUBLIC)
if(EMSCRIPTEN)

  # Avoid CMake generation time errors resulting from collisions with the form
  # of target_link_libraries() used by Emscripten.cmake.
  unset(AOM_LIB_LINK_TYPE)
endif()

# Sanitize boolean variables to ensure they are 0 or 1.
foreach(aom_config_var ${AOM_CONFIG_VARS})
  if(
    NOT aom_config_var MATCHES
    "AOM_RTCD_FLAGS|CONFIG_MAX_DECODE_PROFILE|DECODE_HEIGHT_LIMIT|DECODE_WIDTH_LIMIT"
    )
    if(${aom_config_var})
      set(${aom_config_var} 1)
    else()
      set(${aom_config_var} 0)
    endif()
  endif()
endforeach()

# Generate aom_config templates.
set(aom_config_asm_template "${AOM_CONFIG_DIR}/config/aom_config.asm.cmake")
set(aom_config_h_template "${AOM_CONFIG_DIR}/config/aom_config.h.cmake")
execute_process(COMMAND ${CMAKE_COMMAND}
                        -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                        -DAOM_ROOT=${AOM_ROOT} -P
                        "${AOM_ROOT}/cmake/generate_aom_config_templates.cmake")

# Generate aom_config.{asm,h}.
configure_file("${aom_config_asm_template}"
               "${AOM_CONFIG_DIR}/config/aom_config.asm")
configure_file("${aom_config_h_template}"
               "${AOM_CONFIG_DIR}/config/aom_config.h")

# Arken pinned build: upstream runs host Perl (cmake/rtcd.pl and
# cmake/version.pl) and probes Git here. Neither is an admitted build tool.
# The three RTCD headers below are the exact, unmodified output of upstream
# cmake/rtcd.pl --arch=generic for the reviewed recipe configuration listed in
# _arken_aom_rtcd_inputs. A configuration that would change rtcd.pl's output is
# refused instead of compiling against mismatched dispatch declarations.
set(year "2016")
configure_file("${AOM_ROOT}/cmake/aom_config.c.template"
               "${AOM_CONFIG_DIR}/config/aom_config.c")

if(NOT "${AOM_TARGET_CPU}" STREQUAL "generic" OR NOT "${AOM_RTCD_FLAGS}" STREQUAL "")
  message(FATAL_ERROR
    "Arken libaom recipe: pinned RTCD headers require AOM_TARGET_CPU=generic and empty AOM_RTCD_FLAGS.")
endif()
set(_arken_aom_rtcd_inputs
    "CONFIG_AV1_DECODER 1" "CONFIG_AV1_ENCODER 1" "CONFIG_AV1_HIGHBITDEPTH 0"
    "CONFIG_AV1_TEMPORAL_DENOISING 0" "CONFIG_EXCLUDE_SIMD_MISMATCH 0"
    "CONFIG_HIGHWAY 0" "CONFIG_INTERNAL_STATS 0" "CONFIG_REALTIME_ONLY 0"
    "CONFIG_RUNTIME_CPU_DETECT 0" "HAVE_AVX2 0" "HAVE_SSSE3 0")
foreach(_arken_aom_rtcd_input IN LISTS _arken_aom_rtcd_inputs)
  string(REPLACE " " ";" _arken_aom_rtcd_pair "${_arken_aom_rtcd_input}")
  list(GET _arken_aom_rtcd_pair 0 _arken_aom_rtcd_name)
  file(STRINGS "${AOM_CONFIG_DIR}/config/aom_config.h" _arken_aom_rtcd_line
       REGEX "^#define ${_arken_aom_rtcd_name} ")
  if(NOT "${_arken_aom_rtcd_line}" STREQUAL "#define ${_arken_aom_rtcd_input}")
    message(FATAL_ERROR
      "Arken libaom recipe: pinned RTCD headers were generated for '${_arken_aom_rtcd_input}', configured '${_arken_aom_rtcd_line}'.")
  endif()
endforeach()

function(_arken_aom_write_pinned name expected_sha256 content)
  set(_destination "${AOM_CONFIG_DIR}/config/${name}")
  set(_candidate "${_destination}.arken-candidate")
  file(WRITE "${_candidate}" "${content}")
  file(SHA256 "${_candidate}" _observed)
  if(NOT _observed STREQUAL expected_sha256)
    file(REMOVE "${_candidate}")
    message(FATAL_ERROR "Arken libaom recipe: ${name} expected ${expected_sha256}, wrote ${_observed}.")
  endif()
  file(COPY_FILE "${_candidate}" "${_destination}" ONLY_IF_DIFFERENT)
  file(REMOVE "${_candidate}")
endfunction()

_arken_aom_write_pinned("aom_dsp_rtcd.h" "1252de0093542d9bb59c8bb65bc418e0350a21f42de6027110eeeb7b6c79d5e1" [==[
/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

// This file is generated. Do not edit.
#ifndef AOM_DSP_RTCD_H_
#define AOM_DSP_RTCD_H_

#ifdef RTCD_C
#define RTCD_EXTERN
#else
#define RTCD_EXTERN extern
#endif

/*
 * DSP
 */

#include "aom/aom_integer.h"
#include "aom_dsp/aom_dsp_common.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"


#ifdef __cplusplus
extern "C" {
#endif

unsigned int aom_avg_4x4_c(const uint8_t *, int p);
#define aom_avg_4x4 aom_avg_4x4_c

unsigned int aom_avg_8x8_c(const uint8_t *, int p);
#define aom_avg_8x8 aom_avg_8x8_c

void aom_avg_8x8_quad_c(const uint8_t *s, int p, int x16_idx, int y16_idx, int *avg);
#define aom_avg_8x8_quad aom_avg_8x8_quad_c

void aom_blend_a64_hmask_c(uint8_t *dst, uint32_t dst_stride, const uint8_t *src0, uint32_t src0_stride, const uint8_t *src1, uint32_t src1_stride, const uint8_t *mask, int w, int h);
#define aom_blend_a64_hmask aom_blend_a64_hmask_c

void aom_blend_a64_mask_c(uint8_t *dst, uint32_t dst_stride, const uint8_t *src0, uint32_t src0_stride, const uint8_t *src1, uint32_t src1_stride, const uint8_t *mask, uint32_t mask_stride, int w, int h, int subw, int subh);
#define aom_blend_a64_mask aom_blend_a64_mask_c

void aom_blend_a64_vmask_c(uint8_t *dst, uint32_t dst_stride, const uint8_t *src0, uint32_t src0_stride, const uint8_t *src1, uint32_t src1_stride, const uint8_t *mask, int w, int h);
#define aom_blend_a64_vmask aom_blend_a64_vmask_c

int64_t aom_calc_variance_stat_c(const uint8_t *src, int stride, int bw, int bh);
#define aom_calc_variance_stat aom_calc_variance_stat_c

void aom_comp_avg_pred_c(uint8_t *comp_pred, const uint8_t *pred, int width, int height, const uint8_t *ref, int ref_stride);
#define aom_comp_avg_pred aom_comp_avg_pred_c

void aom_comp_mask_pred_c(uint8_t *comp_pred, const uint8_t *pred, int width, int height, const uint8_t *ref, int ref_stride, const uint8_t *mask, int mask_stride, int invert_mask);
#define aom_comp_mask_pred aom_comp_mask_pred_c

double aom_compute_correlation_c(const unsigned char *frame1, int stride1, int x1, int y1, double mean1, double one_over_stddev1, const unsigned char *frame2, int stride2, int x2, int y2, double mean2, double one_over_stddev2);
#define aom_compute_correlation aom_compute_correlation_c

void aom_compute_flow_at_point_c(const uint8_t *src, const uint8_t *ref, int x, int y, int width, int height, int stride, double *u, double *v);
#define aom_compute_flow_at_point aom_compute_flow_at_point_c

bool aom_compute_mean_stddev_c(const unsigned char *frame, int stride, int x, int y, double *mean, double *one_over_stddev);
#define aom_compute_mean_stddev aom_compute_mean_stddev_c

void aom_convolve8_horiz_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
#define aom_convolve8_horiz aom_convolve8_horiz_c

void aom_convolve8_vert_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
#define aom_convolve8_vert aom_convolve8_vert_c

void aom_convolve_copy_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, int w, int h);
#define aom_convolve_copy aom_convolve_copy_c

void aom_dc_128_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_16x16 aom_dc_128_predictor_16x16_c

void aom_dc_128_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_16x32 aom_dc_128_predictor_16x32_c

void aom_dc_128_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_16x4 aom_dc_128_predictor_16x4_c

void aom_dc_128_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_16x64 aom_dc_128_predictor_16x64_c

void aom_dc_128_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_16x8 aom_dc_128_predictor_16x8_c

void aom_dc_128_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_32x16 aom_dc_128_predictor_32x16_c

void aom_dc_128_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_32x32 aom_dc_128_predictor_32x32_c

void aom_dc_128_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_32x64 aom_dc_128_predictor_32x64_c

void aom_dc_128_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_32x8 aom_dc_128_predictor_32x8_c

void aom_dc_128_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_4x16 aom_dc_128_predictor_4x16_c

void aom_dc_128_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_4x4 aom_dc_128_predictor_4x4_c

void aom_dc_128_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_4x8 aom_dc_128_predictor_4x8_c

void aom_dc_128_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_64x16 aom_dc_128_predictor_64x16_c

void aom_dc_128_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_64x32 aom_dc_128_predictor_64x32_c

void aom_dc_128_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_64x64 aom_dc_128_predictor_64x64_c

void aom_dc_128_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_8x16 aom_dc_128_predictor_8x16_c

void aom_dc_128_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_8x32 aom_dc_128_predictor_8x32_c

void aom_dc_128_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_8x4 aom_dc_128_predictor_8x4_c

void aom_dc_128_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_128_predictor_8x8 aom_dc_128_predictor_8x8_c

void aom_dc_left_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_16x16 aom_dc_left_predictor_16x16_c

void aom_dc_left_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_16x32 aom_dc_left_predictor_16x32_c

void aom_dc_left_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_16x4 aom_dc_left_predictor_16x4_c

void aom_dc_left_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_16x64 aom_dc_left_predictor_16x64_c

void aom_dc_left_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_16x8 aom_dc_left_predictor_16x8_c

void aom_dc_left_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_32x16 aom_dc_left_predictor_32x16_c

void aom_dc_left_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_32x32 aom_dc_left_predictor_32x32_c

void aom_dc_left_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_32x64 aom_dc_left_predictor_32x64_c

void aom_dc_left_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_32x8 aom_dc_left_predictor_32x8_c

void aom_dc_left_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_4x16 aom_dc_left_predictor_4x16_c

void aom_dc_left_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_4x4 aom_dc_left_predictor_4x4_c

void aom_dc_left_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_4x8 aom_dc_left_predictor_4x8_c

void aom_dc_left_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_64x16 aom_dc_left_predictor_64x16_c

void aom_dc_left_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_64x32 aom_dc_left_predictor_64x32_c

void aom_dc_left_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_64x64 aom_dc_left_predictor_64x64_c

void aom_dc_left_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_8x16 aom_dc_left_predictor_8x16_c

void aom_dc_left_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_8x32 aom_dc_left_predictor_8x32_c

void aom_dc_left_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_8x4 aom_dc_left_predictor_8x4_c

void aom_dc_left_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_left_predictor_8x8 aom_dc_left_predictor_8x8_c

void aom_dc_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_16x16 aom_dc_predictor_16x16_c

void aom_dc_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_16x32 aom_dc_predictor_16x32_c

void aom_dc_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_16x4 aom_dc_predictor_16x4_c

void aom_dc_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_16x64 aom_dc_predictor_16x64_c

void aom_dc_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_16x8 aom_dc_predictor_16x8_c

void aom_dc_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_32x16 aom_dc_predictor_32x16_c

void aom_dc_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_32x32 aom_dc_predictor_32x32_c

void aom_dc_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_32x64 aom_dc_predictor_32x64_c

void aom_dc_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_32x8 aom_dc_predictor_32x8_c

void aom_dc_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_4x16 aom_dc_predictor_4x16_c

void aom_dc_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_4x4 aom_dc_predictor_4x4_c

void aom_dc_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_4x8 aom_dc_predictor_4x8_c

void aom_dc_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_64x16 aom_dc_predictor_64x16_c

void aom_dc_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_64x32 aom_dc_predictor_64x32_c

void aom_dc_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_64x64 aom_dc_predictor_64x64_c

void aom_dc_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_8x16 aom_dc_predictor_8x16_c

void aom_dc_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_8x32 aom_dc_predictor_8x32_c

void aom_dc_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_8x4 aom_dc_predictor_8x4_c

void aom_dc_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_predictor_8x8 aom_dc_predictor_8x8_c

void aom_dc_top_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_16x16 aom_dc_top_predictor_16x16_c

void aom_dc_top_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_16x32 aom_dc_top_predictor_16x32_c

void aom_dc_top_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_16x4 aom_dc_top_predictor_16x4_c

void aom_dc_top_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_16x64 aom_dc_top_predictor_16x64_c

void aom_dc_top_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_16x8 aom_dc_top_predictor_16x8_c

void aom_dc_top_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_32x16 aom_dc_top_predictor_32x16_c

void aom_dc_top_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_32x32 aom_dc_top_predictor_32x32_c

void aom_dc_top_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_32x64 aom_dc_top_predictor_32x64_c

void aom_dc_top_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_32x8 aom_dc_top_predictor_32x8_c

void aom_dc_top_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_4x16 aom_dc_top_predictor_4x16_c

void aom_dc_top_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_4x4 aom_dc_top_predictor_4x4_c

void aom_dc_top_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_4x8 aom_dc_top_predictor_4x8_c

void aom_dc_top_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_64x16 aom_dc_top_predictor_64x16_c

void aom_dc_top_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_64x32 aom_dc_top_predictor_64x32_c

void aom_dc_top_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_64x64 aom_dc_top_predictor_64x64_c

void aom_dc_top_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_8x16 aom_dc_top_predictor_8x16_c

void aom_dc_top_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_8x32 aom_dc_top_predictor_8x32_c

void aom_dc_top_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_8x4 aom_dc_top_predictor_8x4_c

void aom_dc_top_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_dc_top_predictor_8x8 aom_dc_top_predictor_8x8_c

void aom_fdct4x4_c(const int16_t *input, tran_low_t *output, int stride);
#define aom_fdct4x4 aom_fdct4x4_c

void aom_fdct4x4_lp_c(const int16_t *input, int16_t *output, int stride);
#define aom_fdct4x4_lp aom_fdct4x4_lp_c

void aom_fft16x16_float_c(const float *input, float *temp, float *output);
#define aom_fft16x16_float aom_fft16x16_float_c

void aom_fft2x2_float_c(const float *input, float *temp, float *output);
#define aom_fft2x2_float aom_fft2x2_float_c

void aom_fft32x32_float_c(const float *input, float *temp, float *output);
#define aom_fft32x32_float aom_fft32x32_float_c

void aom_fft4x4_float_c(const float *input, float *temp, float *output);
#define aom_fft4x4_float aom_fft4x4_float_c

void aom_fft8x8_float_c(const float *input, float *temp, float *output);
#define aom_fft8x8_float aom_fft8x8_float_c

void aom_get_blk_sse_sum_c(const int16_t *data, int stride, int bw, int bh, int *x_sum, int64_t *x2_sum);
#define aom_get_blk_sse_sum aom_get_blk_sse_sum_c

unsigned int aom_get_mb_ss_c(const int16_t *);
#define aom_get_mb_ss aom_get_mb_ss_c

void aom_get_var_sse_sum_16x16_dual_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse16x16, unsigned int *tot_sse, int *tot_sum, uint32_t *var16x16);
#define aom_get_var_sse_sum_16x16_dual aom_get_var_sse_sum_16x16_dual_c

void aom_get_var_sse_sum_8x8_quad_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse8x8, int *sum8x8, unsigned int *tot_sse, int *tot_sum, uint32_t *var8x8);
#define aom_get_var_sse_sum_8x8_quad aom_get_var_sse_sum_8x8_quad_c

void aom_h_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_16x16 aom_h_predictor_16x16_c

void aom_h_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_16x32 aom_h_predictor_16x32_c

void aom_h_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_16x4 aom_h_predictor_16x4_c

void aom_h_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_16x64 aom_h_predictor_16x64_c

void aom_h_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_16x8 aom_h_predictor_16x8_c

void aom_h_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_32x16 aom_h_predictor_32x16_c

void aom_h_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_32x32 aom_h_predictor_32x32_c

void aom_h_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_32x64 aom_h_predictor_32x64_c

void aom_h_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_32x8 aom_h_predictor_32x8_c

void aom_h_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_4x16 aom_h_predictor_4x16_c

void aom_h_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_4x4 aom_h_predictor_4x4_c

void aom_h_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_4x8 aom_h_predictor_4x8_c

void aom_h_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_64x16 aom_h_predictor_64x16_c

void aom_h_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_64x32 aom_h_predictor_64x32_c

void aom_h_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_64x64 aom_h_predictor_64x64_c

void aom_h_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_8x16 aom_h_predictor_8x16_c

void aom_h_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_8x32 aom_h_predictor_8x32_c

void aom_h_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_8x4 aom_h_predictor_8x4_c

void aom_h_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_h_predictor_8x8 aom_h_predictor_8x8_c

void aom_hadamard_16x16_c(const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff);
#define aom_hadamard_16x16 aom_hadamard_16x16_c

void aom_hadamard_32x32_c(const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff);
#define aom_hadamard_32x32 aom_hadamard_32x32_c

void aom_hadamard_4x4_c(const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff);
#define aom_hadamard_4x4 aom_hadamard_4x4_c

void aom_hadamard_8x8_c(const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff);
#define aom_hadamard_8x8 aom_hadamard_8x8_c

void aom_hadamard_lp_16x16_c(const int16_t *src_diff, ptrdiff_t src_stride, int16_t *coeff);
#define aom_hadamard_lp_16x16 aom_hadamard_lp_16x16_c

void aom_hadamard_lp_8x8_c(const int16_t *src_diff, ptrdiff_t src_stride, int16_t *coeff);
#define aom_hadamard_lp_8x8 aom_hadamard_lp_8x8_c

void aom_hadamard_lp_8x8_dual_c(const int16_t *src_diff, ptrdiff_t src_stride, int16_t *coeff);
#define aom_hadamard_lp_8x8_dual aom_hadamard_lp_8x8_dual_c

void aom_ifft16x16_float_c(const float *input, float *temp, float *output);
#define aom_ifft16x16_float aom_ifft16x16_float_c

void aom_ifft2x2_float_c(const float *input, float *temp, float *output);
#define aom_ifft2x2_float aom_ifft2x2_float_c

void aom_ifft32x32_float_c(const float *input, float *temp, float *output);
#define aom_ifft32x32_float aom_ifft32x32_float_c

void aom_ifft4x4_float_c(const float *input, float *temp, float *output);
#define aom_ifft4x4_float aom_ifft4x4_float_c

void aom_ifft8x8_float_c(const float *input, float *temp, float *output);
#define aom_ifft8x8_float aom_ifft8x8_float_c

void aom_int_pro_col_c(int16_t *vbuf, const uint8_t *ref, const int ref_stride, const int width, const int height, int norm_factor);
#define aom_int_pro_col aom_int_pro_col_c

void aom_int_pro_row_c(int16_t *hbuf, const uint8_t *ref, const int ref_stride, const int width, const int height, int norm_factor);
#define aom_int_pro_row aom_int_pro_row_c

void aom_lowbd_blend_a64_d16_mask_c(uint8_t *dst, uint32_t dst_stride, const CONV_BUF_TYPE *src0, uint32_t src0_stride, const CONV_BUF_TYPE *src1, uint32_t src1_stride, const uint8_t *mask, uint32_t mask_stride, int w, int h, int subw, int subh, ConvolveParams *conv_params);
#define aom_lowbd_blend_a64_d16_mask aom_lowbd_blend_a64_d16_mask_c

void aom_lpf_horizontal_14_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_horizontal_14 aom_lpf_horizontal_14_c

void aom_lpf_horizontal_14_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_horizontal_14_dual aom_lpf_horizontal_14_dual_c

void aom_lpf_horizontal_14_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_horizontal_14_quad aom_lpf_horizontal_14_quad_c

void aom_lpf_horizontal_4_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_horizontal_4 aom_lpf_horizontal_4_c

void aom_lpf_horizontal_4_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_horizontal_4_dual aom_lpf_horizontal_4_dual_c

void aom_lpf_horizontal_4_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_horizontal_4_quad aom_lpf_horizontal_4_quad_c

void aom_lpf_horizontal_6_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_horizontal_6 aom_lpf_horizontal_6_c

void aom_lpf_horizontal_6_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_horizontal_6_dual aom_lpf_horizontal_6_dual_c

void aom_lpf_horizontal_6_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_horizontal_6_quad aom_lpf_horizontal_6_quad_c

void aom_lpf_horizontal_8_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_horizontal_8 aom_lpf_horizontal_8_c

void aom_lpf_horizontal_8_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_horizontal_8_dual aom_lpf_horizontal_8_dual_c

void aom_lpf_horizontal_8_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_horizontal_8_quad aom_lpf_horizontal_8_quad_c

void aom_lpf_vertical_14_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_vertical_14 aom_lpf_vertical_14_c

void aom_lpf_vertical_14_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_vertical_14_dual aom_lpf_vertical_14_dual_c

void aom_lpf_vertical_14_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_vertical_14_quad aom_lpf_vertical_14_quad_c

void aom_lpf_vertical_4_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_vertical_4 aom_lpf_vertical_4_c

void aom_lpf_vertical_4_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_vertical_4_dual aom_lpf_vertical_4_dual_c

void aom_lpf_vertical_4_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_vertical_4_quad aom_lpf_vertical_4_quad_c

void aom_lpf_vertical_6_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_vertical_6 aom_lpf_vertical_6_c

void aom_lpf_vertical_6_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_vertical_6_dual aom_lpf_vertical_6_dual_c

void aom_lpf_vertical_6_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_vertical_6_quad aom_lpf_vertical_6_quad_c

void aom_lpf_vertical_8_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
#define aom_lpf_vertical_8 aom_lpf_vertical_8_c

void aom_lpf_vertical_8_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
#define aom_lpf_vertical_8_dual aom_lpf_vertical_8_dual_c

void aom_lpf_vertical_8_quad_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0);
#define aom_lpf_vertical_8_quad aom_lpf_vertical_8_quad_c

unsigned int aom_masked_sad128x128_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad128x128 aom_masked_sad128x128_c

unsigned int aom_masked_sad128x64_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad128x64 aom_masked_sad128x64_c

unsigned int aom_masked_sad16x16_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad16x16 aom_masked_sad16x16_c

unsigned int aom_masked_sad16x32_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad16x32 aom_masked_sad16x32_c

unsigned int aom_masked_sad16x4_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad16x4 aom_masked_sad16x4_c

unsigned int aom_masked_sad16x64_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad16x64 aom_masked_sad16x64_c

unsigned int aom_masked_sad16x8_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad16x8 aom_masked_sad16x8_c

unsigned int aom_masked_sad32x16_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad32x16 aom_masked_sad32x16_c

unsigned int aom_masked_sad32x32_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad32x32 aom_masked_sad32x32_c

unsigned int aom_masked_sad32x64_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad32x64 aom_masked_sad32x64_c

unsigned int aom_masked_sad32x8_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad32x8 aom_masked_sad32x8_c

unsigned int aom_masked_sad4x16_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad4x16 aom_masked_sad4x16_c

unsigned int aom_masked_sad4x4_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad4x4 aom_masked_sad4x4_c

unsigned int aom_masked_sad4x8_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad4x8 aom_masked_sad4x8_c

unsigned int aom_masked_sad64x128_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad64x128 aom_masked_sad64x128_c

unsigned int aom_masked_sad64x16_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad64x16 aom_masked_sad64x16_c

unsigned int aom_masked_sad64x32_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad64x32 aom_masked_sad64x32_c

unsigned int aom_masked_sad64x64_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad64x64 aom_masked_sad64x64_c

unsigned int aom_masked_sad8x16_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad8x16 aom_masked_sad8x16_c

unsigned int aom_masked_sad8x32_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad8x32 aom_masked_sad8x32_c

unsigned int aom_masked_sad8x4_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad8x4 aom_masked_sad8x4_c

unsigned int aom_masked_sad8x8_c(const uint8_t *src, int src_stride, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask);
#define aom_masked_sad8x8 aom_masked_sad8x8_c

unsigned int aom_masked_sub_pixel_variance128x128_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance128x128 aom_masked_sub_pixel_variance128x128_c

unsigned int aom_masked_sub_pixel_variance128x64_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance128x64 aom_masked_sub_pixel_variance128x64_c

unsigned int aom_masked_sub_pixel_variance16x16_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance16x16 aom_masked_sub_pixel_variance16x16_c

unsigned int aom_masked_sub_pixel_variance16x32_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance16x32 aom_masked_sub_pixel_variance16x32_c

unsigned int aom_masked_sub_pixel_variance16x4_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance16x4 aom_masked_sub_pixel_variance16x4_c

unsigned int aom_masked_sub_pixel_variance16x64_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance16x64 aom_masked_sub_pixel_variance16x64_c

unsigned int aom_masked_sub_pixel_variance16x8_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance16x8 aom_masked_sub_pixel_variance16x8_c

unsigned int aom_masked_sub_pixel_variance32x16_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance32x16 aom_masked_sub_pixel_variance32x16_c

unsigned int aom_masked_sub_pixel_variance32x32_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance32x32 aom_masked_sub_pixel_variance32x32_c

unsigned int aom_masked_sub_pixel_variance32x64_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance32x64 aom_masked_sub_pixel_variance32x64_c

unsigned int aom_masked_sub_pixel_variance32x8_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance32x8 aom_masked_sub_pixel_variance32x8_c

unsigned int aom_masked_sub_pixel_variance4x16_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance4x16 aom_masked_sub_pixel_variance4x16_c

unsigned int aom_masked_sub_pixel_variance4x4_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance4x4 aom_masked_sub_pixel_variance4x4_c

unsigned int aom_masked_sub_pixel_variance4x8_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance4x8 aom_masked_sub_pixel_variance4x8_c

unsigned int aom_masked_sub_pixel_variance64x128_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance64x128 aom_masked_sub_pixel_variance64x128_c

unsigned int aom_masked_sub_pixel_variance64x16_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance64x16 aom_masked_sub_pixel_variance64x16_c

unsigned int aom_masked_sub_pixel_variance64x32_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance64x32 aom_masked_sub_pixel_variance64x32_c

unsigned int aom_masked_sub_pixel_variance64x64_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance64x64 aom_masked_sub_pixel_variance64x64_c

unsigned int aom_masked_sub_pixel_variance8x16_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance8x16 aom_masked_sub_pixel_variance8x16_c

unsigned int aom_masked_sub_pixel_variance8x32_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance8x32 aom_masked_sub_pixel_variance8x32_c

unsigned int aom_masked_sub_pixel_variance8x4_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance8x4 aom_masked_sub_pixel_variance8x4_c

unsigned int aom_masked_sub_pixel_variance8x8_c(const uint8_t *src, int src_stride, int xoffset, int yoffset, const uint8_t *ref, int ref_stride, const uint8_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);
#define aom_masked_sub_pixel_variance8x8 aom_masked_sub_pixel_variance8x8_c

void aom_minmax_8x8_c(const uint8_t *s, int p, const uint8_t *d, int dp, int *min, int *max);
#define aom_minmax_8x8 aom_minmax_8x8_c

unsigned int aom_mse16x16_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define aom_mse16x16 aom_mse16x16_c

unsigned int aom_mse16x8_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define aom_mse16x8 aom_mse16x8_c

unsigned int aom_mse8x16_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define aom_mse8x16 aom_mse8x16_c

unsigned int aom_mse8x8_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define aom_mse8x8 aom_mse8x8_c

uint64_t aom_mse_16xh_16bit_c(uint8_t *dst, int dstride,uint16_t *src, int w, int h);
#define aom_mse_16xh_16bit aom_mse_16xh_16bit_c

uint64_t aom_mse_wxh_16bit_c(uint8_t *dst, int dstride,uint16_t *src, int sstride, int w, int h);
#define aom_mse_wxh_16bit aom_mse_wxh_16bit_c

unsigned int aom_obmc_sad128x128_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad128x128 aom_obmc_sad128x128_c

unsigned int aom_obmc_sad128x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad128x64 aom_obmc_sad128x64_c

unsigned int aom_obmc_sad16x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad16x16 aom_obmc_sad16x16_c

unsigned int aom_obmc_sad16x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad16x32 aom_obmc_sad16x32_c

unsigned int aom_obmc_sad16x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad16x4 aom_obmc_sad16x4_c

unsigned int aom_obmc_sad16x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad16x64 aom_obmc_sad16x64_c

unsigned int aom_obmc_sad16x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad16x8 aom_obmc_sad16x8_c

unsigned int aom_obmc_sad32x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad32x16 aom_obmc_sad32x16_c

unsigned int aom_obmc_sad32x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad32x32 aom_obmc_sad32x32_c

unsigned int aom_obmc_sad32x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad32x64 aom_obmc_sad32x64_c

unsigned int aom_obmc_sad32x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad32x8 aom_obmc_sad32x8_c

unsigned int aom_obmc_sad4x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad4x16 aom_obmc_sad4x16_c

unsigned int aom_obmc_sad4x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad4x4 aom_obmc_sad4x4_c

unsigned int aom_obmc_sad4x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad4x8 aom_obmc_sad4x8_c

unsigned int aom_obmc_sad64x128_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad64x128 aom_obmc_sad64x128_c

unsigned int aom_obmc_sad64x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad64x16 aom_obmc_sad64x16_c

unsigned int aom_obmc_sad64x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad64x32 aom_obmc_sad64x32_c

unsigned int aom_obmc_sad64x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad64x64 aom_obmc_sad64x64_c

unsigned int aom_obmc_sad8x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad8x16 aom_obmc_sad8x16_c

unsigned int aom_obmc_sad8x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad8x32 aom_obmc_sad8x32_c

unsigned int aom_obmc_sad8x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad8x4 aom_obmc_sad8x4_c

unsigned int aom_obmc_sad8x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask);
#define aom_obmc_sad8x8 aom_obmc_sad8x8_c

unsigned int aom_obmc_sub_pixel_variance128x128_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance128x128 aom_obmc_sub_pixel_variance128x128_c

unsigned int aom_obmc_sub_pixel_variance128x64_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance128x64 aom_obmc_sub_pixel_variance128x64_c

unsigned int aom_obmc_sub_pixel_variance16x16_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance16x16 aom_obmc_sub_pixel_variance16x16_c

unsigned int aom_obmc_sub_pixel_variance16x32_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance16x32 aom_obmc_sub_pixel_variance16x32_c

unsigned int aom_obmc_sub_pixel_variance16x4_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance16x4 aom_obmc_sub_pixel_variance16x4_c

unsigned int aom_obmc_sub_pixel_variance16x64_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance16x64 aom_obmc_sub_pixel_variance16x64_c

unsigned int aom_obmc_sub_pixel_variance16x8_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance16x8 aom_obmc_sub_pixel_variance16x8_c

unsigned int aom_obmc_sub_pixel_variance32x16_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance32x16 aom_obmc_sub_pixel_variance32x16_c

unsigned int aom_obmc_sub_pixel_variance32x32_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance32x32 aom_obmc_sub_pixel_variance32x32_c

unsigned int aom_obmc_sub_pixel_variance32x64_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance32x64 aom_obmc_sub_pixel_variance32x64_c

unsigned int aom_obmc_sub_pixel_variance32x8_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance32x8 aom_obmc_sub_pixel_variance32x8_c

unsigned int aom_obmc_sub_pixel_variance4x16_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance4x16 aom_obmc_sub_pixel_variance4x16_c

unsigned int aom_obmc_sub_pixel_variance4x4_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance4x4 aom_obmc_sub_pixel_variance4x4_c

unsigned int aom_obmc_sub_pixel_variance4x8_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance4x8 aom_obmc_sub_pixel_variance4x8_c

unsigned int aom_obmc_sub_pixel_variance64x128_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance64x128 aom_obmc_sub_pixel_variance64x128_c

unsigned int aom_obmc_sub_pixel_variance64x16_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance64x16 aom_obmc_sub_pixel_variance64x16_c

unsigned int aom_obmc_sub_pixel_variance64x32_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance64x32 aom_obmc_sub_pixel_variance64x32_c

unsigned int aom_obmc_sub_pixel_variance64x64_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance64x64 aom_obmc_sub_pixel_variance64x64_c

unsigned int aom_obmc_sub_pixel_variance8x16_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance8x16 aom_obmc_sub_pixel_variance8x16_c

unsigned int aom_obmc_sub_pixel_variance8x32_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance8x32 aom_obmc_sub_pixel_variance8x32_c

unsigned int aom_obmc_sub_pixel_variance8x4_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance8x4 aom_obmc_sub_pixel_variance8x4_c

unsigned int aom_obmc_sub_pixel_variance8x8_c(const uint8_t *pre, int pre_stride, int xoffset, int yoffset, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_sub_pixel_variance8x8 aom_obmc_sub_pixel_variance8x8_c

unsigned int aom_obmc_variance128x128_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance128x128 aom_obmc_variance128x128_c

unsigned int aom_obmc_variance128x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance128x64 aom_obmc_variance128x64_c

unsigned int aom_obmc_variance16x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance16x16 aom_obmc_variance16x16_c

unsigned int aom_obmc_variance16x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance16x32 aom_obmc_variance16x32_c

unsigned int aom_obmc_variance16x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance16x4 aom_obmc_variance16x4_c

unsigned int aom_obmc_variance16x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance16x64 aom_obmc_variance16x64_c

unsigned int aom_obmc_variance16x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance16x8 aom_obmc_variance16x8_c

unsigned int aom_obmc_variance32x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance32x16 aom_obmc_variance32x16_c

unsigned int aom_obmc_variance32x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance32x32 aom_obmc_variance32x32_c

unsigned int aom_obmc_variance32x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance32x64 aom_obmc_variance32x64_c

unsigned int aom_obmc_variance32x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance32x8 aom_obmc_variance32x8_c

unsigned int aom_obmc_variance4x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance4x16 aom_obmc_variance4x16_c

unsigned int aom_obmc_variance4x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance4x4 aom_obmc_variance4x4_c

unsigned int aom_obmc_variance4x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance4x8 aom_obmc_variance4x8_c

unsigned int aom_obmc_variance64x128_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance64x128 aom_obmc_variance64x128_c

unsigned int aom_obmc_variance64x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance64x16 aom_obmc_variance64x16_c

unsigned int aom_obmc_variance64x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance64x32 aom_obmc_variance64x32_c

unsigned int aom_obmc_variance64x64_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance64x64 aom_obmc_variance64x64_c

unsigned int aom_obmc_variance8x16_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance8x16 aom_obmc_variance8x16_c

unsigned int aom_obmc_variance8x32_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance8x32 aom_obmc_variance8x32_c

unsigned int aom_obmc_variance8x4_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance8x4 aom_obmc_variance8x4_c

unsigned int aom_obmc_variance8x8_c(const uint8_t *pre, int pre_stride, const int32_t *wsrc, const int32_t *mask, unsigned int *sse);
#define aom_obmc_variance8x8 aom_obmc_variance8x8_c

void aom_paeth_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_16x16 aom_paeth_predictor_16x16_c

void aom_paeth_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_16x32 aom_paeth_predictor_16x32_c

void aom_paeth_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_16x4 aom_paeth_predictor_16x4_c

void aom_paeth_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_16x64 aom_paeth_predictor_16x64_c

void aom_paeth_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_16x8 aom_paeth_predictor_16x8_c

void aom_paeth_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_32x16 aom_paeth_predictor_32x16_c

void aom_paeth_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_32x32 aom_paeth_predictor_32x32_c

void aom_paeth_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_32x64 aom_paeth_predictor_32x64_c

void aom_paeth_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_32x8 aom_paeth_predictor_32x8_c

void aom_paeth_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_4x16 aom_paeth_predictor_4x16_c

void aom_paeth_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_4x4 aom_paeth_predictor_4x4_c

void aom_paeth_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_4x8 aom_paeth_predictor_4x8_c

void aom_paeth_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_64x16 aom_paeth_predictor_64x16_c

void aom_paeth_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_64x32 aom_paeth_predictor_64x32_c

void aom_paeth_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_64x64 aom_paeth_predictor_64x64_c

void aom_paeth_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_8x16 aom_paeth_predictor_8x16_c

void aom_paeth_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_8x32 aom_paeth_predictor_8x32_c

void aom_paeth_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_8x4 aom_paeth_predictor_8x4_c

void aom_paeth_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_paeth_predictor_8x8 aom_paeth_predictor_8x8_c

void aom_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b aom_quantize_b_c

void aom_quantize_b_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b_32x32 aom_quantize_b_32x32_c

void aom_quantize_b_32x32_adaptive_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b_32x32_adaptive aom_quantize_b_32x32_adaptive_c

void aom_quantize_b_64x64_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b_64x64 aom_quantize_b_64x64_c

void aom_quantize_b_64x64_adaptive_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b_64x64_adaptive aom_quantize_b_64x64_adaptive_c

void aom_quantize_b_adaptive_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define aom_quantize_b_adaptive aom_quantize_b_adaptive_c

unsigned int aom_sad128x128_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad128x128 aom_sad128x128_c

unsigned int aom_sad128x128_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad128x128_avg aom_sad128x128_avg_c

void aom_sad128x128x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad128x128x3d aom_sad128x128x3d_c

void aom_sad128x128x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad128x128x4d aom_sad128x128x4d_c

unsigned int aom_sad128x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad128x64 aom_sad128x64_c

unsigned int aom_sad128x64_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad128x64_avg aom_sad128x64_avg_c

void aom_sad128x64x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad128x64x3d aom_sad128x64x3d_c

void aom_sad128x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad128x64x4d aom_sad128x64x4d_c

unsigned int aom_sad16x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad16x16 aom_sad16x16_c

unsigned int aom_sad16x16_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad16x16_avg aom_sad16x16_avg_c

void aom_sad16x16x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x16x3d aom_sad16x16x3d_c

void aom_sad16x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x16x4d aom_sad16x16x4d_c

unsigned int aom_sad16x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad16x32 aom_sad16x32_c

unsigned int aom_sad16x32_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad16x32_avg aom_sad16x32_avg_c

void aom_sad16x32x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x32x3d aom_sad16x32x3d_c

void aom_sad16x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x32x4d aom_sad16x32x4d_c

unsigned int aom_sad16x4_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad16x4 aom_sad16x4_c

void aom_sad16x4x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x4x3d aom_sad16x4x3d_c

void aom_sad16x4x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x4x4d aom_sad16x4x4d_c

unsigned int aom_sad16x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad16x64 aom_sad16x64_c

unsigned int aom_sad16x64_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad16x64_avg aom_sad16x64_avg_c

void aom_sad16x64x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x64x3d aom_sad16x64x3d_c

void aom_sad16x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x64x4d aom_sad16x64x4d_c

unsigned int aom_sad16x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad16x8 aom_sad16x8_c

unsigned int aom_sad16x8_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad16x8_avg aom_sad16x8_avg_c

void aom_sad16x8x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x8x3d aom_sad16x8x3d_c

void aom_sad16x8x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad16x8x4d aom_sad16x8x4d_c

unsigned int aom_sad32x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad32x16 aom_sad32x16_c

unsigned int aom_sad32x16_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad32x16_avg aom_sad32x16_avg_c

void aom_sad32x16x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x16x3d aom_sad32x16x3d_c

void aom_sad32x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x16x4d aom_sad32x16x4d_c

unsigned int aom_sad32x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad32x32 aom_sad32x32_c

unsigned int aom_sad32x32_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad32x32_avg aom_sad32x32_avg_c

void aom_sad32x32x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x32x3d aom_sad32x32x3d_c

void aom_sad32x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x32x4d aom_sad32x32x4d_c

unsigned int aom_sad32x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad32x64 aom_sad32x64_c

unsigned int aom_sad32x64_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad32x64_avg aom_sad32x64_avg_c

void aom_sad32x64x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x64x3d aom_sad32x64x3d_c

void aom_sad32x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x64x4d aom_sad32x64x4d_c

unsigned int aom_sad32x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad32x8 aom_sad32x8_c

unsigned int aom_sad32x8_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad32x8_avg aom_sad32x8_avg_c

void aom_sad32x8x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x8x3d aom_sad32x8x3d_c

void aom_sad32x8x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad32x8x4d aom_sad32x8x4d_c

unsigned int aom_sad4x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad4x16 aom_sad4x16_c

void aom_sad4x16x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x16x3d aom_sad4x16x3d_c

void aom_sad4x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x16x4d aom_sad4x16x4d_c

unsigned int aom_sad4x4_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad4x4 aom_sad4x4_c

void aom_sad4x4x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x4x3d aom_sad4x4x3d_c

void aom_sad4x4x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x4x4d aom_sad4x4x4d_c

unsigned int aom_sad4x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad4x8 aom_sad4x8_c

void aom_sad4x8x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x8x3d aom_sad4x8x3d_c

void aom_sad4x8x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad4x8x4d aom_sad4x8x4d_c

unsigned int aom_sad64x128_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad64x128 aom_sad64x128_c

unsigned int aom_sad64x128_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad64x128_avg aom_sad64x128_avg_c

void aom_sad64x128x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x128x3d aom_sad64x128x3d_c

void aom_sad64x128x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x128x4d aom_sad64x128x4d_c

unsigned int aom_sad64x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad64x16 aom_sad64x16_c

unsigned int aom_sad64x16_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad64x16_avg aom_sad64x16_avg_c

void aom_sad64x16x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x16x3d aom_sad64x16x3d_c

void aom_sad64x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x16x4d aom_sad64x16x4d_c

unsigned int aom_sad64x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad64x32 aom_sad64x32_c

unsigned int aom_sad64x32_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad64x32_avg aom_sad64x32_avg_c

void aom_sad64x32x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x32x3d aom_sad64x32x3d_c

void aom_sad64x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x32x4d aom_sad64x32x4d_c

unsigned int aom_sad64x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad64x64 aom_sad64x64_c

unsigned int aom_sad64x64_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad64x64_avg aom_sad64x64_avg_c

void aom_sad64x64x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x64x3d aom_sad64x64x3d_c

void aom_sad64x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad64x64x4d aom_sad64x64x4d_c

unsigned int aom_sad8x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad8x16 aom_sad8x16_c

unsigned int aom_sad8x16_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad8x16_avg aom_sad8x16_avg_c

void aom_sad8x16x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x16x3d aom_sad8x16x3d_c

void aom_sad8x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x16x4d aom_sad8x16x4d_c

unsigned int aom_sad8x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad8x32 aom_sad8x32_c

unsigned int aom_sad8x32_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad8x32_avg aom_sad8x32_avg_c

void aom_sad8x32x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x32x3d aom_sad8x32x3d_c

void aom_sad8x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x32x4d aom_sad8x32x4d_c

unsigned int aom_sad8x4_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad8x4 aom_sad8x4_c

void aom_sad8x4x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x4x3d aom_sad8x4x3d_c

void aom_sad8x4x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x4x4d aom_sad8x4x4d_c

unsigned int aom_sad8x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad8x8 aom_sad8x8_c

unsigned int aom_sad8x8_avg_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define aom_sad8x8_avg aom_sad8x8_avg_c

void aom_sad8x8x3d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x8x3d aom_sad8x8x3d_c

void aom_sad8x8x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad8x8x4d aom_sad8x8x4d_c

unsigned int aom_sad_skip_128x128_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_128x128 aom_sad_skip_128x128_c

void aom_sad_skip_128x128x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_128x128x4d aom_sad_skip_128x128x4d_c

unsigned int aom_sad_skip_128x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_128x64 aom_sad_skip_128x64_c

void aom_sad_skip_128x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_128x64x4d aom_sad_skip_128x64x4d_c

unsigned int aom_sad_skip_16x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_16x16 aom_sad_skip_16x16_c

void aom_sad_skip_16x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_16x16x4d aom_sad_skip_16x16x4d_c

unsigned int aom_sad_skip_16x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_16x32 aom_sad_skip_16x32_c

void aom_sad_skip_16x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_16x32x4d aom_sad_skip_16x32x4d_c

unsigned int aom_sad_skip_16x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_16x64 aom_sad_skip_16x64_c

void aom_sad_skip_16x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_16x64x4d aom_sad_skip_16x64x4d_c

unsigned int aom_sad_skip_32x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_32x16 aom_sad_skip_32x16_c

void aom_sad_skip_32x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_32x16x4d aom_sad_skip_32x16x4d_c

unsigned int aom_sad_skip_32x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_32x32 aom_sad_skip_32x32_c

void aom_sad_skip_32x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_32x32x4d aom_sad_skip_32x32x4d_c

unsigned int aom_sad_skip_32x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_32x64 aom_sad_skip_32x64_c

void aom_sad_skip_32x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_32x64x4d aom_sad_skip_32x64x4d_c

unsigned int aom_sad_skip_4x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_4x16 aom_sad_skip_4x16_c

void aom_sad_skip_4x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_4x16x4d aom_sad_skip_4x16x4d_c

unsigned int aom_sad_skip_64x128_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_64x128 aom_sad_skip_64x128_c

void aom_sad_skip_64x128x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_64x128x4d aom_sad_skip_64x128x4d_c

unsigned int aom_sad_skip_64x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_64x16 aom_sad_skip_64x16_c

void aom_sad_skip_64x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_64x16x4d aom_sad_skip_64x16x4d_c

unsigned int aom_sad_skip_64x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_64x32 aom_sad_skip_64x32_c

void aom_sad_skip_64x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_64x32x4d aom_sad_skip_64x32x4d_c

unsigned int aom_sad_skip_64x64_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_64x64 aom_sad_skip_64x64_c

void aom_sad_skip_64x64x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_64x64x4d aom_sad_skip_64x64x4d_c

unsigned int aom_sad_skip_8x16_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_8x16 aom_sad_skip_8x16_c

void aom_sad_skip_8x16x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_8x16x4d aom_sad_skip_8x16x4d_c

unsigned int aom_sad_skip_8x32_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride);
#define aom_sad_skip_8x32 aom_sad_skip_8x32_c

void aom_sad_skip_8x32x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[4], int ref_stride, uint32_t sad_array[4]);
#define aom_sad_skip_8x32x4d aom_sad_skip_8x32x4d_c

int aom_satd_c(const tran_low_t *coeff, int length);
#define aom_satd aom_satd_c

int aom_satd_lp_c(const int16_t *coeff, int length);
#define aom_satd_lp aom_satd_lp_c

void aom_scaled_2d_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const InterpKernel *filter, int x0_q4, int x_step_q4, int y0_q4, int y_step_q4, int w, int h);
#define aom_scaled_2d aom_scaled_2d_c

void aom_smooth_h_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_16x16 aom_smooth_h_predictor_16x16_c

void aom_smooth_h_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_16x32 aom_smooth_h_predictor_16x32_c

void aom_smooth_h_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_16x4 aom_smooth_h_predictor_16x4_c

void aom_smooth_h_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_16x64 aom_smooth_h_predictor_16x64_c

void aom_smooth_h_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_16x8 aom_smooth_h_predictor_16x8_c

void aom_smooth_h_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_32x16 aom_smooth_h_predictor_32x16_c

void aom_smooth_h_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_32x32 aom_smooth_h_predictor_32x32_c

void aom_smooth_h_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_32x64 aom_smooth_h_predictor_32x64_c

void aom_smooth_h_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_32x8 aom_smooth_h_predictor_32x8_c

void aom_smooth_h_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_4x16 aom_smooth_h_predictor_4x16_c

void aom_smooth_h_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_4x4 aom_smooth_h_predictor_4x4_c

void aom_smooth_h_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_4x8 aom_smooth_h_predictor_4x8_c

void aom_smooth_h_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_64x16 aom_smooth_h_predictor_64x16_c

void aom_smooth_h_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_64x32 aom_smooth_h_predictor_64x32_c

void aom_smooth_h_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_64x64 aom_smooth_h_predictor_64x64_c

void aom_smooth_h_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_8x16 aom_smooth_h_predictor_8x16_c

void aom_smooth_h_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_8x32 aom_smooth_h_predictor_8x32_c

void aom_smooth_h_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_8x4 aom_smooth_h_predictor_8x4_c

void aom_smooth_h_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_h_predictor_8x8 aom_smooth_h_predictor_8x8_c

void aom_smooth_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_16x16 aom_smooth_predictor_16x16_c

void aom_smooth_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_16x32 aom_smooth_predictor_16x32_c

void aom_smooth_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_16x4 aom_smooth_predictor_16x4_c

void aom_smooth_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_16x64 aom_smooth_predictor_16x64_c

void aom_smooth_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_16x8 aom_smooth_predictor_16x8_c

void aom_smooth_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_32x16 aom_smooth_predictor_32x16_c

void aom_smooth_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_32x32 aom_smooth_predictor_32x32_c

void aom_smooth_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_32x64 aom_smooth_predictor_32x64_c

void aom_smooth_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_32x8 aom_smooth_predictor_32x8_c

void aom_smooth_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_4x16 aom_smooth_predictor_4x16_c

void aom_smooth_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_4x4 aom_smooth_predictor_4x4_c

void aom_smooth_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_4x8 aom_smooth_predictor_4x8_c

void aom_smooth_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_64x16 aom_smooth_predictor_64x16_c

void aom_smooth_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_64x32 aom_smooth_predictor_64x32_c

void aom_smooth_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_64x64 aom_smooth_predictor_64x64_c

void aom_smooth_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_8x16 aom_smooth_predictor_8x16_c

void aom_smooth_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_8x32 aom_smooth_predictor_8x32_c

void aom_smooth_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_8x4 aom_smooth_predictor_8x4_c

void aom_smooth_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_predictor_8x8 aom_smooth_predictor_8x8_c

void aom_smooth_v_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_16x16 aom_smooth_v_predictor_16x16_c

void aom_smooth_v_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_16x32 aom_smooth_v_predictor_16x32_c

void aom_smooth_v_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_16x4 aom_smooth_v_predictor_16x4_c

void aom_smooth_v_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_16x64 aom_smooth_v_predictor_16x64_c

void aom_smooth_v_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_16x8 aom_smooth_v_predictor_16x8_c

void aom_smooth_v_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_32x16 aom_smooth_v_predictor_32x16_c

void aom_smooth_v_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_32x32 aom_smooth_v_predictor_32x32_c

void aom_smooth_v_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_32x64 aom_smooth_v_predictor_32x64_c

void aom_smooth_v_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_32x8 aom_smooth_v_predictor_32x8_c

void aom_smooth_v_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_4x16 aom_smooth_v_predictor_4x16_c

void aom_smooth_v_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_4x4 aom_smooth_v_predictor_4x4_c

void aom_smooth_v_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_4x8 aom_smooth_v_predictor_4x8_c

void aom_smooth_v_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_64x16 aom_smooth_v_predictor_64x16_c

void aom_smooth_v_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_64x32 aom_smooth_v_predictor_64x32_c

void aom_smooth_v_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_64x64 aom_smooth_v_predictor_64x64_c

void aom_smooth_v_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_8x16 aom_smooth_v_predictor_8x16_c

void aom_smooth_v_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_8x32 aom_smooth_v_predictor_8x32_c

void aom_smooth_v_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_8x4 aom_smooth_v_predictor_8x4_c

void aom_smooth_v_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_smooth_v_predictor_8x8 aom_smooth_v_predictor_8x8_c

int64_t aom_sse_c(const uint8_t *a, int a_stride, const uint8_t *b,int b_stride, int width, int height);
#define aom_sse aom_sse_c

void aom_ssim_parms_8x8_c(const uint8_t *s, int sp, const uint8_t *r, int rp, uint32_t *sum_s, uint32_t *sum_r, uint32_t *sum_sq_s, uint32_t *sum_sq_r, uint32_t *sum_sxr);
#define aom_ssim_parms_8x8 aom_ssim_parms_8x8_c

uint32_t aom_sub_pixel_avg_variance128x128_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance128x128 aom_sub_pixel_avg_variance128x128_c

uint32_t aom_sub_pixel_avg_variance128x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance128x64 aom_sub_pixel_avg_variance128x64_c

uint32_t aom_sub_pixel_avg_variance16x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance16x16 aom_sub_pixel_avg_variance16x16_c

uint32_t aom_sub_pixel_avg_variance16x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance16x32 aom_sub_pixel_avg_variance16x32_c

uint32_t aom_sub_pixel_avg_variance16x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance16x4 aom_sub_pixel_avg_variance16x4_c

uint32_t aom_sub_pixel_avg_variance16x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance16x64 aom_sub_pixel_avg_variance16x64_c

uint32_t aom_sub_pixel_avg_variance16x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance16x8 aom_sub_pixel_avg_variance16x8_c

uint32_t aom_sub_pixel_avg_variance32x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance32x16 aom_sub_pixel_avg_variance32x16_c

uint32_t aom_sub_pixel_avg_variance32x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance32x32 aom_sub_pixel_avg_variance32x32_c

uint32_t aom_sub_pixel_avg_variance32x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance32x64 aom_sub_pixel_avg_variance32x64_c

uint32_t aom_sub_pixel_avg_variance32x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance32x8 aom_sub_pixel_avg_variance32x8_c

uint32_t aom_sub_pixel_avg_variance4x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance4x16 aom_sub_pixel_avg_variance4x16_c

uint32_t aom_sub_pixel_avg_variance4x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance4x4 aom_sub_pixel_avg_variance4x4_c

uint32_t aom_sub_pixel_avg_variance4x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance4x8 aom_sub_pixel_avg_variance4x8_c

uint32_t aom_sub_pixel_avg_variance64x128_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance64x128 aom_sub_pixel_avg_variance64x128_c

uint32_t aom_sub_pixel_avg_variance64x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance64x16 aom_sub_pixel_avg_variance64x16_c

uint32_t aom_sub_pixel_avg_variance64x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance64x32 aom_sub_pixel_avg_variance64x32_c

uint32_t aom_sub_pixel_avg_variance64x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance64x64 aom_sub_pixel_avg_variance64x64_c

uint32_t aom_sub_pixel_avg_variance8x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance8x16 aom_sub_pixel_avg_variance8x16_c

uint32_t aom_sub_pixel_avg_variance8x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance8x32 aom_sub_pixel_avg_variance8x32_c

uint32_t aom_sub_pixel_avg_variance8x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance8x4 aom_sub_pixel_avg_variance8x4_c

uint32_t aom_sub_pixel_avg_variance8x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse, const uint8_t *second_pred);
#define aom_sub_pixel_avg_variance8x8 aom_sub_pixel_avg_variance8x8_c

uint32_t aom_sub_pixel_variance128x128_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance128x128 aom_sub_pixel_variance128x128_c

uint32_t aom_sub_pixel_variance128x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance128x64 aom_sub_pixel_variance128x64_c

uint32_t aom_sub_pixel_variance16x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance16x16 aom_sub_pixel_variance16x16_c

uint32_t aom_sub_pixel_variance16x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance16x32 aom_sub_pixel_variance16x32_c

uint32_t aom_sub_pixel_variance16x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance16x4 aom_sub_pixel_variance16x4_c

uint32_t aom_sub_pixel_variance16x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance16x64 aom_sub_pixel_variance16x64_c

uint32_t aom_sub_pixel_variance16x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance16x8 aom_sub_pixel_variance16x8_c

uint32_t aom_sub_pixel_variance32x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance32x16 aom_sub_pixel_variance32x16_c

uint32_t aom_sub_pixel_variance32x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance32x32 aom_sub_pixel_variance32x32_c

uint32_t aom_sub_pixel_variance32x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance32x64 aom_sub_pixel_variance32x64_c

uint32_t aom_sub_pixel_variance32x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance32x8 aom_sub_pixel_variance32x8_c

uint32_t aom_sub_pixel_variance4x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance4x16 aom_sub_pixel_variance4x16_c

uint32_t aom_sub_pixel_variance4x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance4x4 aom_sub_pixel_variance4x4_c

uint32_t aom_sub_pixel_variance4x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance4x8 aom_sub_pixel_variance4x8_c

uint32_t aom_sub_pixel_variance64x128_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance64x128 aom_sub_pixel_variance64x128_c

uint32_t aom_sub_pixel_variance64x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance64x16 aom_sub_pixel_variance64x16_c

uint32_t aom_sub_pixel_variance64x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance64x32 aom_sub_pixel_variance64x32_c

uint32_t aom_sub_pixel_variance64x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance64x64 aom_sub_pixel_variance64x64_c

uint32_t aom_sub_pixel_variance8x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance8x16 aom_sub_pixel_variance8x16_c

uint32_t aom_sub_pixel_variance8x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance8x32 aom_sub_pixel_variance8x32_c

uint32_t aom_sub_pixel_variance8x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance8x4 aom_sub_pixel_variance8x4_c

uint32_t aom_sub_pixel_variance8x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, uint32_t *sse);
#define aom_sub_pixel_variance8x8 aom_sub_pixel_variance8x8_c

void aom_subtract_block_c(int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint8_t *src_ptr, ptrdiff_t src_stride, const uint8_t *pred_ptr, ptrdiff_t pred_stride);
#define aom_subtract_block aom_subtract_block_c

uint64_t aom_sum_squares_2d_i16_c(const int16_t *src, int stride, int width, int height);
#define aom_sum_squares_2d_i16 aom_sum_squares_2d_i16_c

uint64_t aom_sum_squares_i16_c(const int16_t *src, uint32_t N);
#define aom_sum_squares_i16 aom_sum_squares_i16_c

uint64_t aom_sum_sse_2d_i16_c(const int16_t *src, int src_stride, int width, int height, int *sum);
#define aom_sum_sse_2d_i16 aom_sum_sse_2d_i16_c

void aom_v_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_16x16 aom_v_predictor_16x16_c

void aom_v_predictor_16x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_16x32 aom_v_predictor_16x32_c

void aom_v_predictor_16x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_16x4 aom_v_predictor_16x4_c

void aom_v_predictor_16x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_16x64 aom_v_predictor_16x64_c

void aom_v_predictor_16x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_16x8 aom_v_predictor_16x8_c

void aom_v_predictor_32x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_32x16 aom_v_predictor_32x16_c

void aom_v_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_32x32 aom_v_predictor_32x32_c

void aom_v_predictor_32x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_32x64 aom_v_predictor_32x64_c

void aom_v_predictor_32x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_32x8 aom_v_predictor_32x8_c

void aom_v_predictor_4x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_4x16 aom_v_predictor_4x16_c

void aom_v_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_4x4 aom_v_predictor_4x4_c

void aom_v_predictor_4x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_4x8 aom_v_predictor_4x8_c

void aom_v_predictor_64x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_64x16 aom_v_predictor_64x16_c

void aom_v_predictor_64x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_64x32 aom_v_predictor_64x32_c

void aom_v_predictor_64x64_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_64x64 aom_v_predictor_64x64_c

void aom_v_predictor_8x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_8x16 aom_v_predictor_8x16_c

void aom_v_predictor_8x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_8x32 aom_v_predictor_8x32_c

void aom_v_predictor_8x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_8x4 aom_v_predictor_8x4_c

void aom_v_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define aom_v_predictor_8x8 aom_v_predictor_8x8_c

uint64_t aom_var_2d_u8_c(uint8_t *src, int src_stride, int width, int height);
#define aom_var_2d_u8 aom_var_2d_u8_c

unsigned int aom_variance128x128_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance128x128 aom_variance128x128_c

unsigned int aom_variance128x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance128x64 aom_variance128x64_c

unsigned int aom_variance16x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance16x16 aom_variance16x16_c

unsigned int aom_variance16x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance16x32 aom_variance16x32_c

unsigned int aom_variance16x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance16x4 aom_variance16x4_c

unsigned int aom_variance16x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance16x64 aom_variance16x64_c

unsigned int aom_variance16x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance16x8 aom_variance16x8_c

unsigned int aom_variance32x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance32x16 aom_variance32x16_c

unsigned int aom_variance32x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance32x32 aom_variance32x32_c

unsigned int aom_variance32x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance32x64 aom_variance32x64_c

unsigned int aom_variance32x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance32x8 aom_variance32x8_c

unsigned int aom_variance4x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance4x16 aom_variance4x16_c

unsigned int aom_variance4x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance4x4 aom_variance4x4_c

unsigned int aom_variance4x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance4x8 aom_variance4x8_c

unsigned int aom_variance64x128_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance64x128 aom_variance64x128_c

unsigned int aom_variance64x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance64x16 aom_variance64x16_c

unsigned int aom_variance64x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance64x32 aom_variance64x32_c

unsigned int aom_variance64x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance64x64 aom_variance64x64_c

unsigned int aom_variance8x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance8x16 aom_variance8x16_c

unsigned int aom_variance8x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance8x32 aom_variance8x32_c

unsigned int aom_variance8x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance8x4 aom_variance8x4_c

unsigned int aom_variance8x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define aom_variance8x8 aom_variance8x8_c

int aom_vector_var_c(const int16_t *ref, const int16_t *src, int bwl);
#define aom_vector_var aom_vector_var_c

void aom_dsp_rtcd(void);

#include "config/aom_config.h"

#ifdef RTCD_C
static void setup_rtcd_internal(void)
{
}
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_DSP_RTCD_H_
]==])

_arken_aom_write_pinned("aom_scale_rtcd.h" "16c294bebc7c8e685323f7c8ad06ab20ad079e003cb0a6b18c34d1bd907e4f64" [==[
/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

// This file is generated. Do not edit.
#ifndef AOM_SCALE_RTCD_H_
#define AOM_SCALE_RTCD_H_

#ifdef RTCD_C
#define RTCD_EXTERN
#else
#define RTCD_EXTERN extern
#endif

#include <stdbool.h>

struct yv12_buffer_config;

#ifdef __cplusplus
extern "C" {
#endif

void aom_extend_frame_borders_c(struct yv12_buffer_config *ybf, int num_planes);
#define aom_extend_frame_borders aom_extend_frame_borders_c

void aom_extend_frame_borders_plane_row_c(const struct yv12_buffer_config *ybf, int plane, int v_start, int v_end);
#define aom_extend_frame_borders_plane_row aom_extend_frame_borders_plane_row_c

void aom_yv12_copy_frame_c(const struct yv12_buffer_config *src_bc, struct yv12_buffer_config *dst_bc, const int num_planes);
#define aom_yv12_copy_frame aom_yv12_copy_frame_c

void aom_yv12_copy_u_c(const struct yv12_buffer_config *src_bc, struct yv12_buffer_config *dst_bc, int use_crop);
#define aom_yv12_copy_u aom_yv12_copy_u_c

void aom_yv12_copy_v_c(const struct yv12_buffer_config *src_bc, struct yv12_buffer_config *dst_bc, int use_crop);
#define aom_yv12_copy_v aom_yv12_copy_v_c

void aom_yv12_copy_y_c(const struct yv12_buffer_config *src_ybc, struct yv12_buffer_config *dst_ybc, int use_crop);
#define aom_yv12_copy_y aom_yv12_copy_y_c

void aom_yv12_partial_coloc_copy_u_c(const struct yv12_buffer_config *src_bc, struct yv12_buffer_config *dst_bc, int hstart, int hend, int vstart, int vend);
#define aom_yv12_partial_coloc_copy_u aom_yv12_partial_coloc_copy_u_c

void aom_yv12_partial_coloc_copy_v_c(const struct yv12_buffer_config *src_bc, struct yv12_buffer_config *dst_bc, int hstart, int hend, int vstart, int vend);
#define aom_yv12_partial_coloc_copy_v aom_yv12_partial_coloc_copy_v_c

void aom_yv12_partial_coloc_copy_y_c(const struct yv12_buffer_config *src_ybc, struct yv12_buffer_config *dst_ybc, int hstart, int hend, int vstart, int vend);
#define aom_yv12_partial_coloc_copy_y aom_yv12_partial_coloc_copy_y_c

void aom_yv12_partial_copy_u_c(const struct yv12_buffer_config *src_bc, int hstart1, int hend1, int vstart1, int vend1, struct yv12_buffer_config *dst_bc, int hstart2, int vstart2);
#define aom_yv12_partial_copy_u aom_yv12_partial_copy_u_c

void aom_yv12_partial_copy_v_c(const struct yv12_buffer_config *src_bc, int hstart1, int hend1, int vstart1, int vend1, struct yv12_buffer_config *dst_bc, int hstart2, int vstart2);
#define aom_yv12_partial_copy_v aom_yv12_partial_copy_v_c

void aom_yv12_partial_copy_y_c(const struct yv12_buffer_config *src_ybc, int hstart1, int hend1, int vstart1, int vend1, struct yv12_buffer_config *dst_ybc, int hstart2, int vstart2);
#define aom_yv12_partial_copy_y aom_yv12_partial_copy_y_c

int aom_yv12_realloc_with_new_border_c(struct yv12_buffer_config *ybf, int new_border, int byte_alignment, bool alloc_pyramid, int num_planes);
#define aom_yv12_realloc_with_new_border aom_yv12_realloc_with_new_border_c

void aom_scale_rtcd(void);

#include "config/aom_config.h"

#ifdef RTCD_C
static void setup_rtcd_internal(void)
{
}
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_SCALE_RTCD_H_
]==])

_arken_aom_write_pinned("av1_rtcd.h" "6dc08b0bafb5bbe125c7262c9e2984707e5f34da6c48683d6161734660eb2c03" [==[
/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

// This file is generated. Do not edit.
#ifndef AV1_RTCD_H_
#define AV1_RTCD_H_

#ifdef RTCD_C
#define RTCD_EXTERN
#else
#define RTCD_EXTERN extern
#endif

/*
 * AV1
 */

#include "aom/aom_integer.h"
#include "aom_dsp/odintrin.h"
#include "aom_dsp/txfm_common.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/common.h"
#include "av1/common/convolve.h"
#include "av1/common/enums.h"
#include "av1/common/filter.h"
#include "av1/common/quant_common.h"
#include "av1/common/restoration.h"

struct macroblockd;

/* Encoder forward decls */
struct macroblock;
struct txfm_param;
struct aom_variance_vtable;
struct search_site_config;
struct yv12_buffer_config;
struct NN_CONFIG;
typedef struct NN_CONFIG NN_CONFIG;

enum { NONE, RELU, SOFTSIGN, SIGMOID } UENUM1BYTE(ACTIVATION);
#if CONFIG_NN_V2
enum { SOFTMAX_CROSS_ENTROPY } UENUM1BYTE(LOSS);
struct NN_CONFIG_V2;
typedef struct NN_CONFIG_V2 NN_CONFIG_V2;
struct FC_LAYER;
typedef struct FC_LAYER FC_LAYER;
#endif  // CONFIG_NN_V2

struct CNN_CONFIG;
typedef struct CNN_CONFIG CNN_CONFIG;
struct CNN_LAYER_CONFIG;
typedef struct CNN_LAYER_CONFIG CNN_LAYER_CONFIG;
struct CNN_THREAD_DATA;
typedef struct CNN_THREAD_DATA CNN_THREAD_DATA;
struct CNN_BRANCH_CONFIG;
typedef struct CNN_BRANCH_CONFIG CNN_BRANCH_CONFIG;
struct CNN_MULTI_OUT;
typedef struct CNN_MULTI_OUT CNN_MULTI_OUT;

/* Function pointers return by CfL functions */
typedef void (*cfl_subsample_lbd_fn)(const uint8_t *input, int input_stride,
                                     uint16_t *output_q3);

#if CONFIG_AV1_HIGHBITDEPTH
typedef void (*cfl_subsample_hbd_fn)(const uint16_t *input, int input_stride,
                                     uint16_t *output_q3);

typedef void (*cfl_predict_hbd_fn)(const int16_t *src, uint16_t *dst,
                                   int dst_stride, int alpha_q3, int bd);
#endif

typedef void (*cfl_subtract_average_fn)(const uint16_t *src, int16_t *dst);

typedef void (*cfl_predict_lbd_fn)(const int16_t *src, uint8_t *dst,
                                   int dst_stride, int alpha_q3);


#ifdef __cplusplus
extern "C" {
#endif

void aom_comp_avg_upsampled_pred_c(MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                                   const MV *const mv, uint8_t *comp_pred, const uint8_t *pred, int width,
                                                   int height, int subpel_x_q3, int subpel_y_q3, const uint8_t *ref,
                                                   int ref_stride, int subpel_search);
#define aom_comp_avg_upsampled_pred aom_comp_avg_upsampled_pred_c

void aom_quantize_b_helper_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr, const int log_scale);
#define aom_quantize_b_helper aom_quantize_b_helper_c

void aom_upsampled_pred_c(MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                          const MV *const mv, uint8_t *comp_pred, int width, int height, int subpel_x_q3,
                                          int subpel_y_q3, const uint8_t *ref, int ref_stride, int subpel_search);
#define aom_upsampled_pred aom_upsampled_pred_c

int av1_apply_selfguided_restoration_c(const uint8_t *dat, int width, int height, int stride, int eps, const int *xqd, uint8_t *dst, int dst_stride, int32_t *tmpbuf, int bit_depth, int highbd);
#define av1_apply_selfguided_restoration av1_apply_selfguided_restoration_c

void av1_apply_temporal_filter_c(const struct yv12_buffer_config *frame_to_filter, const struct macroblockd *mbd, const BLOCK_SIZE block_size, const int mb_row, const int mb_col, const int num_planes, const double *noise_levels, const MV *subblock_mvs, const int *subblock_mses, const int q_factor, const int filter_strength, int tf_wgt_calc_lvl, const uint8_t *pred, uint32_t *accum, uint16_t *count);
#define av1_apply_temporal_filter av1_apply_temporal_filter_c

int64_t av1_block_error_c(const tran_low_t *coeff, const tran_low_t *dqcoeff, intptr_t block_size, int64_t *ssz);
#define av1_block_error av1_block_error_c

int64_t av1_block_error_lp_c(const int16_t *coeff, const int16_t *dqcoeff, intptr_t block_size);
#define av1_block_error_lp av1_block_error_lp_c

void av1_build_compound_diffwtd_mask_c(uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const uint8_t *src0, int src0_stride, const uint8_t *src1, int src1_stride, int h, int w);
#define av1_build_compound_diffwtd_mask av1_build_compound_diffwtd_mask_c

void av1_build_compound_diffwtd_mask_d16_c(uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0, int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w, ConvolveParams *conv_params, int bd);
#define av1_build_compound_diffwtd_mask_d16 av1_build_compound_diffwtd_mask_d16_c

void av1_calc_indices_dim1_c(const int16_t *data, const int16_t *centroids, uint8_t *indices, int64_t *total_dist, int n, int k);
#define av1_calc_indices_dim1 av1_calc_indices_dim1_c

void av1_calc_indices_dim2_c(const int16_t *data, const int16_t *centroids, uint8_t *indices, int64_t *total_dist, int n, int k);
#define av1_calc_indices_dim2 av1_calc_indices_dim2_c

void av1_calc_proj_params_c(const uint8_t *src8, int width, int height, int src_stride, const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride, int32_t *flt1, int flt1_stride, int64_t H[2][2], int64_t C[2], const sgr_params_type *params);
#define av1_calc_proj_params av1_calc_proj_params_c

void av1_cnn_activate_c(float **input, int channels, int width, int height, int stride, ACTIVATION layer_activation);
#define av1_cnn_activate av1_cnn_activate_c

void av1_cnn_add_c(float **input, int channels, int width, int height, int stride, const float **add);
#define av1_cnn_add av1_cnn_add_c

void av1_cnn_batchnorm_c(float **image, int channels, int width, int height, int stride, const float *gamma, const float *beta, const float *mean, const float *std);
#define av1_cnn_batchnorm av1_cnn_batchnorm_c

void av1_cnn_convolve_no_maxpool_padding_valid_c(const float **input, int in_width, int in_height, int in_stride, const CNN_LAYER_CONFIG *layer_config, float **output, int out_stride, int start_idx, int cstep, int channel_step);
#define av1_cnn_convolve_no_maxpool_padding_valid av1_cnn_convolve_no_maxpool_padding_valid_c

void av1_cnn_deconvolve_c(const float **input, int in_width, int in_height, int in_stride, const CNN_LAYER_CONFIG *layer_config, float **output, int out_stride);
#define av1_cnn_deconvolve av1_cnn_deconvolve_c

bool av1_cnn_predict_c(const float **input, int in_width, int in_height, int in_stride, const CNN_CONFIG *cnn_config, const CNN_THREAD_DATA *thread_data, CNN_MULTI_OUT *output_struct);
#define av1_cnn_predict av1_cnn_predict_c

void av1_compute_stats_c(int wiener_win, const uint8_t *dgd8, const uint8_t *src8, int16_t *dgd_avg, int16_t *src_avg, int h_start, int h_end, int v_start, int v_end, int dgd_stride, int src_stride, int64_t *M, int64_t *H, int use_downsampled_wiener_stats);
#define av1_compute_stats av1_compute_stats_c

void av1_convolve_2d_scale_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int x_step_qn, const int subpel_y_qn, const int y_step_qn, ConvolveParams *conv_params);
#define av1_convolve_2d_scale av1_convolve_2d_scale_c

void av1_convolve_2d_sr_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params);
#define av1_convolve_2d_sr av1_convolve_2d_sr_c

void av1_convolve_2d_sr_intrabc_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params);
#define av1_convolve_2d_sr_intrabc av1_convolve_2d_sr_intrabc_c

void av1_convolve_horiz_rs_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const int16_t *x_filters, int x0_qn, int x_step_qn);
#define av1_convolve_horiz_rs av1_convolve_horiz_rs_c

void av1_convolve_x_sr_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn, ConvolveParams *conv_params);
#define av1_convolve_x_sr av1_convolve_x_sr_c

void av1_convolve_x_sr_intrabc_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn, ConvolveParams *conv_params);
#define av1_convolve_x_sr_intrabc av1_convolve_x_sr_intrabc_c

void av1_convolve_y_sr_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn);
#define av1_convolve_y_sr av1_convolve_y_sr_c

void av1_convolve_y_sr_intrabc_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn);
#define av1_convolve_y_sr_intrabc av1_convolve_y_sr_intrabc_c

void av1_dist_wtd_convolve_2d_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params);
#define av1_dist_wtd_convolve_2d av1_dist_wtd_convolve_2d_c

void av1_dist_wtd_convolve_2d_copy_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, ConvolveParams *conv_params);
#define av1_dist_wtd_convolve_2d_copy av1_dist_wtd_convolve_2d_copy_c

void av1_dist_wtd_convolve_x_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn, ConvolveParams *conv_params);
#define av1_dist_wtd_convolve_x av1_dist_wtd_convolve_x_c

void av1_dist_wtd_convolve_y_c(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn, ConvolveParams *conv_params);
#define av1_dist_wtd_convolve_y av1_dist_wtd_convolve_y_c

void av1_dr_prediction_z1_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh, const uint8_t *above, const uint8_t *left, int upsample_above, int dx, int dy);
#define av1_dr_prediction_z1 av1_dr_prediction_z1_c

void av1_dr_prediction_z2_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh, const uint8_t *above, const uint8_t *left, int upsample_above, int upsample_left, int dx, int dy);
#define av1_dr_prediction_z2 av1_dr_prediction_z2_c

void av1_dr_prediction_z3_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh, const uint8_t *above, const uint8_t *left, int upsample_left, int dx, int dy);
#define av1_dr_prediction_z3 av1_dr_prediction_z3_c

double av1_estimate_noise_from_single_plane_c(const uint8_t *src, int height, int width, int stride, int edge_thresh);
#define av1_estimate_noise_from_single_plane av1_estimate_noise_from_single_plane_c

void av1_fdwt8x8_uint8_input_c(const uint8_t *input, tran_low_t *output, int stride, int hbd);
#define av1_fdwt8x8_uint8_input av1_fdwt8x8_uint8_input_c

void av1_filter_intra_edge_c(uint8_t *p, int sz, int strength);
#define av1_filter_intra_edge av1_filter_intra_edge_c

void av1_filter_intra_predictor_c(uint8_t *dst, ptrdiff_t stride, TX_SIZE tx_size, const uint8_t *above, const uint8_t *left, int mode);
#define av1_filter_intra_predictor av1_filter_intra_predictor_c

void av1_fwd_txfm2d_16x16_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_16x16 av1_fwd_txfm2d_16x16_c

void av1_fwd_txfm2d_16x32_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_16x32 av1_fwd_txfm2d_16x32_c

void av1_fwd_txfm2d_16x4_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_16x4 av1_fwd_txfm2d_16x4_c

void av1_fwd_txfm2d_16x64_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_16x64 av1_fwd_txfm2d_16x64_c

void av1_fwd_txfm2d_16x8_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_16x8 av1_fwd_txfm2d_16x8_c

void av1_fwd_txfm2d_32x16_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_32x16 av1_fwd_txfm2d_32x16_c

void av1_fwd_txfm2d_32x32_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_32x32 av1_fwd_txfm2d_32x32_c

void av1_fwd_txfm2d_32x64_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_32x64 av1_fwd_txfm2d_32x64_c

void av1_fwd_txfm2d_32x8_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_32x8 av1_fwd_txfm2d_32x8_c

void av1_fwd_txfm2d_4x16_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_4x16 av1_fwd_txfm2d_4x16_c

void av1_fwd_txfm2d_4x4_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_4x4 av1_fwd_txfm2d_4x4_c

void av1_fwd_txfm2d_4x8_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_4x8 av1_fwd_txfm2d_4x8_c

void av1_fwd_txfm2d_64x16_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_64x16 av1_fwd_txfm2d_64x16_c

void av1_fwd_txfm2d_64x32_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_64x32 av1_fwd_txfm2d_64x32_c

void av1_fwd_txfm2d_64x64_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_64x64 av1_fwd_txfm2d_64x64_c

void av1_fwd_txfm2d_8x16_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_8x16 av1_fwd_txfm2d_8x16_c

void av1_fwd_txfm2d_8x32_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_8x32 av1_fwd_txfm2d_8x32_c

void av1_fwd_txfm2d_8x4_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_8x4 av1_fwd_txfm2d_8x4_c

void av1_fwd_txfm2d_8x8_c(const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_fwd_txfm2d_8x8 av1_fwd_txfm2d_8x8_c

void av1_fwht4x4_c(const int16_t *input, tran_low_t *output, int stride);
#define av1_fwht4x4 av1_fwht4x4_c

uint32_t av1_get_crc32c_value_c(void *crc_calculator, const uint8_t *p, size_t length);
#define av1_get_crc32c_value av1_get_crc32c_value_c

void av1_get_horver_correlation_full_c(const int16_t *diff, int stride, int w, int h, float *hcorr, float *vcorr);
#define av1_get_horver_correlation_full av1_get_horver_correlation_full_c

void av1_get_nz_map_contexts_c(const uint8_t *const levels, const int16_t *const scan, const int eob, const TX_SIZE tx_size, const TX_CLASS tx_class, int8_t *const coeff_contexts);
#define av1_get_nz_map_contexts av1_get_nz_map_contexts_c

void av1_highbd_inv_txfm_add_c(const tran_low_t *input, uint8_t *dest, int stride, const TxfmParam *txfm_param);
#define av1_highbd_inv_txfm_add av1_highbd_inv_txfm_add_c

void av1_highbd_iwht4x4_16_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride, int bd);
#define av1_highbd_iwht4x4_16_add av1_highbd_iwht4x4_16_add_c

void av1_highbd_iwht4x4_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride, int bd);
#define av1_highbd_iwht4x4_1_add av1_highbd_iwht4x4_1_add_c

void av1_interp_cubic_rate_dist_c(const double *p1, const double *p2, double x, double rate_dist_f[2]);
#define av1_interp_cubic_rate_dist av1_interp_cubic_rate_dist_c

void av1_inv_txfm2d_add_16x16_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_16x16 av1_inv_txfm2d_add_16x16_c

void av1_inv_txfm2d_add_16x32_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_16x32 av1_inv_txfm2d_add_16x32_c

void av1_inv_txfm2d_add_16x4_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_16x4 av1_inv_txfm2d_add_16x4_c

void av1_inv_txfm2d_add_16x64_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_16x64 av1_inv_txfm2d_add_16x64_c

void av1_inv_txfm2d_add_16x8_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_16x8 av1_inv_txfm2d_add_16x8_c

void av1_inv_txfm2d_add_32x16_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_32x16 av1_inv_txfm2d_add_32x16_c

void av1_inv_txfm2d_add_32x32_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_32x32 av1_inv_txfm2d_add_32x32_c

void av1_inv_txfm2d_add_32x64_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_32x64 av1_inv_txfm2d_add_32x64_c

void av1_inv_txfm2d_add_32x8_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_32x8 av1_inv_txfm2d_add_32x8_c

void av1_inv_txfm2d_add_4x16_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_4x16 av1_inv_txfm2d_add_4x16_c

void av1_inv_txfm2d_add_4x4_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_4x4 av1_inv_txfm2d_add_4x4_c

void av1_inv_txfm2d_add_4x8_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_4x8 av1_inv_txfm2d_add_4x8_c

void av1_inv_txfm2d_add_64x16_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_64x16 av1_inv_txfm2d_add_64x16_c

void av1_inv_txfm2d_add_64x32_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_64x32 av1_inv_txfm2d_add_64x32_c

void av1_inv_txfm2d_add_64x64_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_64x64 av1_inv_txfm2d_add_64x64_c

void av1_inv_txfm2d_add_8x16_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_8x16 av1_inv_txfm2d_add_8x16_c

void av1_inv_txfm2d_add_8x32_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_8x32 av1_inv_txfm2d_add_8x32_c

void av1_inv_txfm2d_add_8x4_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_8x4 av1_inv_txfm2d_add_8x4_c

void av1_inv_txfm2d_add_8x8_c(const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd);
#define av1_inv_txfm2d_add_8x8 av1_inv_txfm2d_add_8x8_c

void av1_inv_txfm_add_c(const tran_low_t *dqcoeff, uint8_t *dst, int stride, const TxfmParam *txfm_param);
#define av1_inv_txfm_add av1_inv_txfm_add_c

void av1_lowbd_fwd_txfm_c(const int16_t *src_diff, tran_low_t *coeff, int diff_stride, TxfmParam *txfm_param);
#define av1_lowbd_fwd_txfm av1_lowbd_fwd_txfm_c

int64_t av1_lowbd_pixel_proj_error_c(const uint8_t *src8, int width, int height, int src_stride, const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride, int32_t *flt1, int flt1_stride, int xq[2], const sgr_params_type *params);
#define av1_lowbd_pixel_proj_error av1_lowbd_pixel_proj_error_c

void av1_nn_fast_softmax_16_c(const float *input_nodes, float *output);
#define av1_nn_fast_softmax_16 av1_nn_fast_softmax_16_c

void av1_nn_predict_c(const float *input_nodes, const NN_CONFIG *const nn_config, int reduce_prec, float *const output);
#define av1_nn_predict av1_nn_predict_c

void av1_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, const qm_val_t * qm_ptr, const qm_val_t * iqm_ptr, int log_scale);
#define av1_quantize_b av1_quantize_b_c

void av1_quantize_fp_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define av1_quantize_fp av1_quantize_fp_c

void av1_quantize_fp_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define av1_quantize_fp_32x32 av1_quantize_fp_32x32_c

void av1_quantize_fp_64x64_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define av1_quantize_fp_64x64 av1_quantize_fp_64x64_c

void av1_quantize_lp_c(const int16_t *coeff_ptr, intptr_t n_coeffs, const int16_t *round_ptr, const int16_t *quant_ptr, int16_t *qcoeff_ptr, int16_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define av1_quantize_lp av1_quantize_lp_c

void av1_resize_and_extend_frame_c(const YV12_BUFFER_CONFIG *src, YV12_BUFFER_CONFIG *dst, const InterpFilter filter, const int phase, const int num_planes);
#define av1_resize_and_extend_frame av1_resize_and_extend_frame_c

void av1_resize_horz_dir_c(const uint8_t *const input, int in_stride, uint8_t *intbuf, int height, int filtered_length, int width2);
#define av1_resize_horz_dir av1_resize_horz_dir_c

bool av1_resize_vert_dir_c(uint8_t *intbuf, uint8_t *output, int out_stride, int height, int height2, int width2, int start_col);
#define av1_resize_vert_dir av1_resize_vert_dir_c

void av1_round_shift_array_c(int32_t *arr, int size, int bit);
#define av1_round_shift_array av1_round_shift_array_c

int av1_selfguided_restoration_c(const uint8_t *dgd8, int width, int height,
                                  int dgd_stride, int32_t *flt0, int32_t *flt1, int flt_stride,
                                  int sgr_params_idx, int bit_depth, int highbd);
#define av1_selfguided_restoration av1_selfguided_restoration_c

void av1_txb_init_levels_c(const tran_low_t *const coeff, const int width, const int height, uint8_t *const levels);
#define av1_txb_init_levels av1_txb_init_levels_c

void av1_upsample_intra_edge_c(uint8_t *p, int sz);
#define av1_upsample_intra_edge av1_upsample_intra_edge_c

void av1_warp_affine_c(const int32_t *mat, const uint8_t *ref, int width, int height, int stride, uint8_t *pred, int p_col, int p_row, int p_width, int p_height, int p_stride, int subsampling_x, int subsampling_y, ConvolveParams *conv_params, int16_t alpha, int16_t beta, int16_t gamma, int16_t delta);
#define av1_warp_affine av1_warp_affine_c

void av1_wedge_compute_delta_squares_c(int16_t *d, const int16_t *a, const int16_t *b, int N);
#define av1_wedge_compute_delta_squares av1_wedge_compute_delta_squares_c

int8_t av1_wedge_sign_from_residuals_c(const int16_t *ds, const uint8_t *m, int N, int64_t limit);
#define av1_wedge_sign_from_residuals av1_wedge_sign_from_residuals_c

uint64_t av1_wedge_sse_from_residuals_c(const int16_t *r1, const int16_t *d, const uint8_t *m, int N);
#define av1_wedge_sse_from_residuals av1_wedge_sse_from_residuals_c

void av1_wiener_convolve_add_src_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h, const WienerConvolveParams *conv_params);
#define av1_wiener_convolve_add_src av1_wiener_convolve_add_src_c

void cdef_copy_rect8_8bit_to_16bit_c(uint16_t *dst, int dstride, const uint8_t *src, int sstride, int width, int height);
#define cdef_copy_rect8_8bit_to_16bit cdef_copy_rect8_8bit_to_16bit_c

void cdef_filter_16_0_c(void *dst16, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_16_0 cdef_filter_16_0_c

void cdef_filter_16_1_c(void *dst16, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_16_1 cdef_filter_16_1_c

void cdef_filter_16_2_c(void *dst16, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_16_2 cdef_filter_16_2_c

void cdef_filter_16_3_c(void *dst16, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_16_3 cdef_filter_16_3_c

void cdef_filter_8_0_c(void *dst8, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_8_0 cdef_filter_8_0_c

void cdef_filter_8_1_c(void *dst8, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_8_1 cdef_filter_8_1_c

void cdef_filter_8_2_c(void *dst8, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_8_2 cdef_filter_8_2_c

void cdef_filter_8_3_c(void *dst8, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, int coeff_shift, int block_width, int block_height);
#define cdef_filter_8_3 cdef_filter_8_3_c

int cdef_find_dir_c(const uint16_t *img, int stride, int32_t *var, int coeff_shift);
#define cdef_find_dir cdef_find_dir_c

void cdef_find_dir_dual_c(const uint16_t *img1, const uint16_t *img2, int stride, int32_t *var1, int32_t *var2, int coeff_shift, int *out1, int *out2);
#define cdef_find_dir_dual cdef_find_dir_dual_c

cfl_subsample_lbd_fn cfl_get_luma_subsampling_420_lbd_c(TX_SIZE tx_size);
#define cfl_get_luma_subsampling_420_lbd cfl_get_luma_subsampling_420_lbd_c

cfl_subsample_lbd_fn cfl_get_luma_subsampling_422_lbd_c(TX_SIZE tx_size);
#define cfl_get_luma_subsampling_422_lbd cfl_get_luma_subsampling_422_lbd_c

cfl_subsample_lbd_fn cfl_get_luma_subsampling_444_lbd_c(TX_SIZE tx_size);
#define cfl_get_luma_subsampling_444_lbd cfl_get_luma_subsampling_444_lbd_c

cfl_predict_lbd_fn cfl_get_predict_lbd_fn_c(TX_SIZE tx_size);
#define cfl_get_predict_lbd_fn cfl_get_predict_lbd_fn_c

cfl_subtract_average_fn cfl_get_subtract_average_fn_c(TX_SIZE tx_size);
#define cfl_get_subtract_average_fn cfl_get_subtract_average_fn_c

void av1_rtcd(void);

#include "config/aom_config.h"

#ifdef RTCD_C
static void setup_rtcd_internal(void)
{
}
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_RTCD_H_
]==])

# Generate aom_version.h from CHANGELOG with CMake only (see cmake/version.cmake).
execute_process(COMMAND ${CMAKE_COMMAND}
                        -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                        -DAOM_ROOT=${AOM_ROOT} -P
                        "${AOM_ROOT}/cmake/version.cmake"
                RESULT_VARIABLE _arken_aom_version_result)
if(NOT _arken_aom_version_result EQUAL 0)
  message(FATAL_ERROR "Arken libaom recipe: aom_version.h generation failed.")
endif()
