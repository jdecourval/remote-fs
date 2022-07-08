#ifndef REMOTE_FS_INODECACHE_H
#define REMOTE_FS_INODECACHE_H

#include <sys/stat.h>

#include <cassert>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace remotefs {

class InodeCache {
   public:
    struct CacheValue {
        CacheValue(struct stat&& s)
            : stat(std::move(s)),
              handle{nullptr, &std::fclose} {}

        struct stat stat;
        std::unique_ptr<std::FILE, decltype(&std::fclose)> handle;
    };
    using CacheType = std::unordered_map<std::string, CacheValue>;
    using Inode = CacheType::value_type;
    using fuse_ino_t = std::uint64_t;

    InodeCache()
        : root{*lookup(".")} {
        root.second.stat.st_ino = 1;
    }

    Inode* lookup(std::string path);

    [[nodiscard]] inline const Inode& inode_from_ino(fuse_ino_t ino) const {
        if (ino == 1) {
            return root;
        }

        return *reinterpret_cast<const Inode*>(ino);
    }

    [[nodiscard]] inline Inode& inode_from_ino(fuse_ino_t ino) {
        if (ino == 1) {
            return root;
        }

        return *reinterpret_cast<Inode*>(ino);
    }

    static void open(Inode& inode) {
        inode.second.handle.reset(std::fopen(inode.first.c_str(), "rb"));
    }

    static void close(Inode& inode) {
        inode.second.handle.reset();
    }

   private:
    template <typename... Ts>
    Inode& create_inode(Ts&&... params) {
        auto pair = cache.emplace(std::forward<Ts>(params)...);
        assert(pair.second);
        auto& inode = pair.first;
        inode->second.stat.st_ino = reinterpret_cast<fuse_ino_t>(&*inode);
        return *inode;
    }

    CacheType cache;
    Inode& root;
};

}  // namespace remotefs

#endif  // REMOTE_FS_INODECACHE_H
