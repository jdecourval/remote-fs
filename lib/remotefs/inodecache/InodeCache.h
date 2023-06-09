#ifndef REMOTE_FS_INODECACHE_H
#define REMOTE_FS_INODECACHE_H

#include <sys/stat.h>

#include <cassert>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace remotefs {

class InodeCache {
   public:
    class InodeValue {
        using FileDescriptor = int;
        static const FileDescriptor unassigned = -1;

       public:
        explicit InodeValue(const struct stat& s);
        ~InodeValue() noexcept;
        void open(std::string_view path);
        void close();
        [[nodiscard]] bool is_open() const;
        [[nodiscard]] FileDescriptor handle() const;

        struct stat stat;

       private:
        FileDescriptor _handle;
    };

    using CacheType = std::unordered_map<std::string, InodeValue>;
    using Inode = CacheType::value_type;
    using fuse_ino_t = std::uint64_t;

    InodeCache();
    const Inode* find(const std::string& path) const;
    Inode* lookup(std::string path);
    static void open(Inode& inode);
    static void close(Inode& inode);

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

    // TODO: Make private again
    template <typename... Ts>
    Inode& create_inode(Ts&&... params) {
        auto lock = std::scoped_lock{cache_lock};
        auto [inode_iter, inserted] = cache.emplace(std::forward<Ts>(params)...);
        inode_iter->second.stat.st_ino = reinterpret_cast<fuse_ino_t>(&*inode_iter);
        return *inode_iter;
    }

   private:
    mutable std::mutex cache_lock{};
    CacheType cache;
    Inode& root;
};

}  // namespace remotefs

#endif  // REMOTE_FS_INODECACHE_H
