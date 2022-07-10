#ifndef REMOTEFS_LIB_H_FUSEOP
#define REMOTEFS_LIB_H_FUSEOP

#include <cstdint>

namespace remotefs {
enum FuseOp : uint8_t { LOOKUP, GETATTR, READDIR, OPEN, READ, RELEASE };
}  // namespace remotefs

#endif  // REMOTEFS_LIB_H_FUSEOP
