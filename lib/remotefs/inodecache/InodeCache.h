#ifndef REMOTE_FS_INODECACHE_H
#define REMOTE_FS_INODECACHE_H

#include <sys/stat.h>

#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>

namespace remotefs {

class InodeCache {
   public:
    using CacheType = std::unordered_map<std::string, struct stat>;
    using Inode = CacheType::value_type;
    using fuse_ino_t = std::uint64_t;

    InodeCache()
        : root{*lookup(".")} {
        root.second.st_ino = 1;
    }

    Inode* lookup(std::string path);

    [[nodiscard]] inline const Inode& inode_from_ino(fuse_ino_t ino) const {
        if (ino == 1) {
            return root;
        }

        return *reinterpret_cast<const Inode*>(ino);
    }

   private:
    template <typename... Ts>
    Inode& create_inode(Ts&&... params) {
        auto pair = cache.emplace(std::forward<Ts>(params)...);
        assert(pair.second);
        auto& entry = pair.first;
        entry->second.st_ino = reinterpret_cast<fuse_ino_t>(&*entry);
        return *entry;
    }

    CacheType cache;
    Inode& root;
};

}  // namespace remotefs

#endif  // REMOTE_FS_INODECACHE_H
