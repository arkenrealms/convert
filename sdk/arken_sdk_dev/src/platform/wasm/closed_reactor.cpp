// Explicit capability-free profile for deterministic, memory-only reactors.
// This is not the filesystem/browser host: no files, clock, environment or
// process capabilities are provided. Future imports still fail host admission.
#include <wasi/api.h>
#include <cstdint>
#include <cstddef>

extern "C" __wasi_errno_t __wrap___wasi_environ_sizes_get(__wasi_size_t* count,
    __wasi_size_t* bytes) noexcept {
    *count = 0; *bytes = 0; return __WASI_ERRNO_SUCCESS;
}
extern "C" __wasi_errno_t __wrap___wasi_environ_get(std::uint8_t**,
    std::uint8_t*) noexcept { return __WASI_ERRNO_SUCCESS; }
extern "C" __wasi_errno_t __wrap___wasi_clock_time_get(__wasi_clockid_t,
    __wasi_timestamp_t, __wasi_timestamp_t*) noexcept { return __WASI_ERRNO_NOSYS; }
extern "C" __wasi_errno_t __wrap___wasi_fd_close(__wasi_fd_t) noexcept {
    return __WASI_ERRNO_BADF;
}
extern "C" __wasi_errno_t __wrap___wasi_fd_seek(__wasi_fd_t, __wasi_filedelta_t,
    __wasi_whence_t, __wasi_filesize_t*) noexcept { return __WASI_ERRNO_BADF; }
extern "C" __wasi_errno_t __wrap___wasi_fd_write(__wasi_fd_t,
    const __wasi_ciovec_t*, std::size_t, __wasi_size_t* written) noexcept {
    *written = 0; return __WASI_ERRNO_BADF;
}
extern "C" [[noreturn]] void __wrap___wasi_proc_exit(__wasi_exitcode_t) noexcept {
    __builtin_trap();
}
