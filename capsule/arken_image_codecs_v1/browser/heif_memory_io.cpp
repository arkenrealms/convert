// Platform edge for the memory-only HEIF browser profile. libc++'s retained
// stdin buffer references this function even when the codec uses no file API.
// No file descriptor is admitted; never report a successful empty read.
#include <wasi/api.h>
extern "C" __wasi_errno_t __wasi_fd_read(__wasi_fd_t,const __wasi_iovec_t*,size_t,__wasi_size_t* read){
    if(read)*read=0;
    return __WASI_ERRNO_BADF;
}
