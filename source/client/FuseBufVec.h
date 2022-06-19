#ifndef REMOTE_FS_FUSEBUFVEC_H
#define REMOTE_FS_FUSEBUFVEC_H

#include <fuse_lowlevel.h>

#include <array>

template <size_t total_size>
struct FuseBufVec {
    decltype(fuse_bufvec::count) count{};
    decltype(fuse_bufvec::idx) idx{};
    decltype(fuse_bufvec::off) off{};
    std::array<fuse_buf, total_size - sizeof(count) - sizeof(idx) - sizeof(off)> buf;
};

#endif  // REMOTE_FS_FUSEBUFVEC_H
