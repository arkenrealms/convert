// Use the same bounded scalar implementation on native and browser targets.
// Upstream source and license are admitted by skcms-f0bbebb-v1.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#define SKCMS_PORTABLE
#include <skcms.cc>
