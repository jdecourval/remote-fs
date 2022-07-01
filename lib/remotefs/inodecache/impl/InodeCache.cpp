#include "remotefs/inodecache/InodeCache.h"

namespace remotefs {

InodeCache::Inode* InodeCache::lookup(std::string_view path) {
    using Stat = struct stat;

    auto stats = Stat{};
    if (stat(path.data(), &stats) >= 0) {
        return &create_inode(path, stats);
    }
    return nullptr;
}
}  // namespace remotefs
