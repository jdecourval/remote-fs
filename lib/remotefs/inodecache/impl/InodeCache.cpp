#include "remotefs/inodecache/InodeCache.h"

#include <fcntl.h>
#include <unistd.h>

#include <system_error>

namespace remotefs {
const InodeCache::Inode* InodeCache::find(const std::string& path) const {
    auto lock = std::scoped_lock{cache_lock};
    if (auto found = cache.find(path); found != cache.end()) {
        return &*(found);
    }

    return nullptr;
}

InodeCache::Inode* InodeCache::lookup(std::string path) {
    using Stat = struct stat;

    {
        auto lock = std::scoped_lock{cache_lock};
        if (auto found = cache.find(path); found != cache.end()) {
            return &*(found);
        }
    }

    auto stats = Stat{};
    if (stat(path.data(), &stats) >= 0) {
        return &create_inode(std::move(path), stats);
    }

    return nullptr;
}

InodeCache::InodeCache()
    : root{*lookup(".")} {
    root.second.stat.st_ino = 1;
}

void InodeCache::open(InodeCache::Inode& inode) {
    if (!inode.second.is_open()) [[likely]] {
        inode.second.open(inode.first);
    }
}

void InodeCache::close(InodeCache::Inode& inode) {
    inode.second.close();
}

InodeCache::InodeValue::InodeValue(const struct stat& s)
    : stat{s},
      _handle{unassigned} {}

InodeCache::InodeValue::~InodeValue() noexcept {
    if (_handle != unassigned) {
        ::close(_handle);
    }
    _handle = unassigned;
}

void InodeCache::InodeValue::open(std::string_view path) {
    assert(_handle == unassigned);
    if ((_handle = ::open(path.data(), O_RDONLY)) == -1) [[unlikely]] {
        throw std::system_error(errno, std::generic_category(), "Opening file");
    }
}

void InodeCache::InodeValue::close() {
    if (_handle != unassigned) return;
    if (::close(_handle)) [[unlikely]] {
        throw std::system_error(errno, std::generic_category(), "Closing file");
    }
    _handle = unassigned;
}

bool InodeCache::InodeValue::is_open() const {
    return _handle != unassigned;
}

InodeCache::InodeValue::FileDescriptor InodeCache::InodeValue::handle() const {
    assert(is_open());
    return _handle;
}

}  // namespace remotefs
