add_library(fuse-kernel INTERFACE
        fuse3/fuse_kernel.h
        fuse3/fuse_i.h
        )
target_include_directories(fuse-kernel SYSTEM INTERFACE .)
