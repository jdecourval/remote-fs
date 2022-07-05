#include "remotefs/inodecache/InodeCache.h"

namespace remotefs {

InodeCache::Inode* InodeCache::lookup(std::string path) {
    using Stat = struct stat;

    if (auto found = cache.find(path); found != cache.end()) {
        return &*(found);
    }

    auto stats = Stat{};
    if (stat(path.data(), &stats) >= 0) {
        return &create_inode(path, stats);
    }
    return nullptr;
}
}  // namespace remotefs
