#ifndef REMOTE_FS_FUSECMDLINEOPTSWRAPPER_H
#define REMOTE_FS_FUSECMDLINEOPTSWRAPPER_H

#include <fuse_lowlevel.h>

#include <stdexcept>

namespace remotefs {

struct FuseCmdlineOptsWrapper : public fuse_cmdline_opts {
   public:
    explicit FuseCmdlineOptsWrapper(fuse_args &args);
    ~FuseCmdlineOptsWrapper() noexcept;
};

}  // namespace remotefs

#endif  // REMOTE_FS_FUSECMDLINEOPTSWRAPPER_H
