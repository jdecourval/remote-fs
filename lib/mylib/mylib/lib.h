#ifndef CLEANCPPPROJECT_LIB_H
#define CLEANCPPPROJECT_LIB_H

#include <functional>

namespace remotefs
{
enum FuseOp : uint8_t
{
    LOOKUP,
    GETATTR,
    READDIR,
    OPEN,
    READ
};
} // namespace remotefs

#endif // CLEANCPPPROJECT_LIB_H
