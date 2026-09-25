// Explicit capability-free profile for deterministic, memory-only reactors.
// This is not the filesystem/browser host: no files, clock, environment or
// process capabilities are provided. Future imports still fail host admission.
#include <wasi/api.h>
#include <cstdint>
#include <cstddef>
#include <type_traits>

extern "C" __wasi_errno_t __wrap___wasi_environ_sizes_get(__wasi_size_t* count,
    __wasi_size_t* bytes) noexcept {
    *count = 0; *bytes = 0; return __WASI_ERRNO_SUCCESS;
}
extern "C" __wasi_errno_t __wrap___wasi_environ_get(std::uint8_t**,
    std::uint8_t*) noexcept { return __WASI_ERRNO_SUCCESS; }
extern "C" __wasi_errno_t __wrap___wasi_clock_time_get(__wasi_clockid_t,
    __wasi_timestamp_t, __wasi_timestamp_t*) noexcept { return __WASI_ERRNO_NOSYS; }
// Standard-library wait paths may retain this import. Capability-free kernels
// cannot sleep, observe clocks, or poll host descriptors, including on error.
extern "C" __wasi_errno_t __wrap___wasi_poll_oneoff(const __wasi_subscription_t*,
    __wasi_event_t*, __wasi_size_t, __wasi_size_t* events) noexcept {
    *events = 0; return __WASI_ERRNO_NOSYS;
}
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_poll_oneoff), decltype(&__wasi_poll_oneoff)>);
extern "C" __wasi_errno_t __wrap___wasi_fd_close(__wasi_fd_t) noexcept {
    return __WASI_ERRNO_BADF;
}
extern "C" __wasi_errno_t __wrap___wasi_fd_seek(__wasi_fd_t, __wasi_filedelta_t,
    __wasi_whence_t, __wasi_filesize_t*) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_write(__wasi_fd_t,
    const __wasi_ciovec_t*, std::size_t, __wasi_size_t* written) noexcept {
    *written = 0; return __WASI_ERRNO_BADF;
}
// Locale/iostream code can retain these libc call sites even in a memory-only
// program. Explicit denial closes the reactor; no host implementation exists.
extern "C" __wasi_errno_t __wrap___wasi_fd_fdstat_get(__wasi_fd_t,
    __wasi_fdstat_t*) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_fdstat_set_flags(__wasi_fd_t,
    __wasi_fdflags_t) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_prestat_get(__wasi_fd_t,
    __wasi_prestat_t*) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_prestat_dir_name(__wasi_fd_t,
    std::uint8_t*, __wasi_size_t) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_read(__wasi_fd_t,
    const __wasi_iovec_t*, __wasi_size_t, __wasi_size_t* read) noexcept {
    *read = 0; return __WASI_ERRNO_BADF;
}
extern "C" __wasi_errno_t __wrap___wasi_path_open(__wasi_fd_t,
    __wasi_lookupflags_t, const char*, __wasi_oflags_t,
    __wasi_rights_t, __wasi_rights_t, __wasi_fdflags_t, __wasi_fd_t* opened) noexcept {
    *opened = static_cast<__wasi_fd_t>(-1); return __WASI_ERRNO_BADF;
}
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_path_open), decltype(&__wasi_path_open)>);
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_fd_read), decltype(&__wasi_fd_read)>);
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_fd_prestat_get), decltype(&__wasi_fd_prestat_get)>);
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_fd_prestat_dir_name), decltype(&__wasi_fd_prestat_dir_name)>);
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_fd_fdstat_get), decltype(&__wasi_fd_fdstat_get)>);
static_assert(std::is_convertible_v<decltype(&__wrap___wasi_fd_fdstat_set_flags), decltype(&__wasi_fd_fdstat_set_flags)>);
extern "C" [[noreturn]] void __wrap___wasi_proc_exit(__wasi_exitcode_t) noexcept {
    __builtin_trap();
}
