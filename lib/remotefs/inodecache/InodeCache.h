#ifndef REMOTE_FS_INODECACHE_H
#define REMOTE_FS_INODECACHE_H

#include <sys/stat.h>

#include <string>
#include <string_view>
#include <vector>

namespace remotefs {

class InodeCache {
   public:
    using fuse_ino_t = std::uint64_t;
    struct Inode {
        Inode(std::string_view path_, struct stat stats_)
            : path(path_),
              stats(stats_) {}
        std::string path;
        struct stat stats;
    };

    Inode* lookup(std::string_view path);

    [[nodiscard]] inline const Inode& inode_from_ino(fuse_ino_t ino) const {
        return cache.at(ino);
    }

   private:
    template <typename... Ts>
    Inode& create_inode(Ts&&... params) {
        auto& entry = cache.emplace_back(std::forward<Ts>(params)...);
        entry.stats.st_ino = cache.size() - 1;
        return entry;
    }

    std::vector<Inode> cache;
};

}  // namespace remotefs

#endif  // REMOTE_FS_INODECACHE_H
