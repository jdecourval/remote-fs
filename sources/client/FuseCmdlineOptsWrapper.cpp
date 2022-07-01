#include "FuseCmdlineOptsWrapper.h"

namespace remotefs {
FuseCmdlineOptsWrapper::FuseCmdlineOptsWrapper(fuse_args& args)
    : fuse_cmdline_opts{} {
    if (fuse_parse_cmdline(&args, this) != 0) {
        throw std::logic_error("Failed to parse command line");
    }
}

FuseCmdlineOptsWrapper::~FuseCmdlineOptsWrapper() noexcept {
    free(mountpoint);
}
}  // namespace remotefs
