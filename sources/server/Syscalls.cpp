#include "Syscalls.h"

#include <fuse_lowlevel.h>
#include <quill/Quill.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>

#include "IoUring.h"

namespace remotefs {
namespace {
struct stat statx_to_stat(const struct statx input) {
    using Stat = struct stat;
    return Stat{.st_dev = makedev(input.stx_dev_major, input.stx_dev_minor),
                .st_ino = input.stx_ino,
                .st_nlink = input.stx_nlink,
                .st_mode = input.stx_mode,
                .st_uid = input.stx_uid,
                .st_gid = input.stx_gid,
                .st_rdev = makedev(input.stx_rdev_major, input.stx_rdev_minor),
                .st_size = static_cast<__off_t>(input.stx_size),
                .st_blksize = input.stx_blksize,
                .st_blocks = static_cast<__blkcnt_t>(input.stx_blocks),
                .st_atim = {input.stx_atime.tv_sec, input.stx_atime.tv_nsec},
                .st_mtim = {input.stx_mtime.tv_sec, input.stx_mtime.tv_nsec},
                .st_ctim = {input.stx_ctime.tv_sec, input.stx_ctime.tv_nsec}};
}

struct UserCb {
    MessageReceiver msg;
    InodeCache& inode_cache;
};

auto lookup_cb = [](int32_t syscall_ret, IoUring<MessageReceiver>::CallbackErased* ptr) -> MessageReceiver&& {
    auto cb = dynamic_cast<IoUring<MessageReceiver>::CallbackStatic<UserCb, struct statx>*>(ptr);
    auto& [captured, inode_cache] = cb->user_data;
    LOG_TRACE_L1(quill::get_logger(), "In intermediary callback, ret={}, path={}", syscall_ret, cb->path.data());
    if (syscall_ret >= 0) [[likely]] {
    } else if (syscall_ret == -EAGAIN) {
        return std::move(captured);
    } else {
        throw std::system_error(-syscall_ret, std::generic_category(), "Read result");
    }

    auto& statx = cb->buffer;
    auto stat = statx_to_stat(statx);
    LOG_TRACE_L1(quill::get_logger(), "In intermediary callback success, uid={}, size={}", stat.st_uid, stat.st_size);

    auto fuse_entry =
        fuse_entry_param{.ino = reinterpret_cast<fuse_ino_t>(&inode_cache.create_inode(std::move(cb->path), stat)),
                         .generation = 0,
                         .attr = stat,
                         .attr_timeout = 1,
                         .entry_timeout = 1};
    captured.add(&fuse_entry);
    return std::move(captured);
};
}  // namespace

Syscalls::Syscalls()
    : logger{quill::get_logger()} {}

std::optional<MessageReceiver> Syscalls::lookup(MessageReceiver& message, Syscalls::IoUringImpl& uring) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto relative_path = message.get_usr_data_string(1);
    auto root_path = std::filesystem::path{inode_cache.inode_from_ino(ino).first};
    auto path = root_path / relative_path;
    LOG_DEBUG(logger, "Looking up path={}, relative={}, root={}", path.string(), relative_path, root_path);

    auto found = inode_cache.find(path);
    if (found) {
        auto response = message.respond();
        auto fuse_entry = fuse_entry_param{.ino = found->second.stat.st_ino,
                                           .generation = 0,
                                           .attr = found->second.stat,
                                           .attr_timeout = 1,
                                           .entry_timeout = 1};
        response.add(&fuse_entry);
        return response;
    }

    uring.queue_statx<UserCb>(AT_FDCWD, path, lookup_cb, message.respond(), inode_cache);

    return {};
}

MessageReceiver Syscalls::getattr(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto response = message.respond();

    const auto& entry = inode_cache.inode_from_ino(ino);
    response.add(&entry.second.stat);
    LOG_TRACE_L1(logger, "getattr: ino found in cache{}", ino);

    return response;
}

MessageReceiver Syscalls::readdir(MessageReceiver& message) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto size = message.get_usr_data<size_t>(1);
    auto off = message.get_usr_data<off_t>(2) + 1;  // off must start at 1
    auto total_size = 0ull;
    auto response = message.respond();
    std::array<char, 1024> buffer;  // NOLINT(cppcoreguidelines-pro-type-member-init)
    LOG_TRACE_L1(logger, "Received readdir for ino {} with size {} and offset {}", ino, size, off);

    const auto& root_entry = inode_cache.inode_from_ino(ino);

    if (off == 0) {
        LOG_TRACE_L2(logger, "Adding . to buffer");
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), ".", &root_entry.second.stat, off++);
        if ((total_size += entry_size) > size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
            return response;
        }
        response.add_raw(buffer.data(), entry_size);
    }

    if (off == 1) {
        auto permissions = std::filesystem::status("..").permissions();
        LOG_TRACE_L2(logger, "Adding .. to buffer");
        struct stat stbuf = {};
        // Other fields are not currently used by fuse
        stbuf.st_ino = 1;
        stbuf.st_mode = std::to_underlying(permissions);
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), "..", &stbuf, off++);
        if ((total_size += entry_size) > size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
            return response;
        }
        response.add_raw(buffer.data(), entry_size);
    }

    for (const auto& entry : std::next(std::filesystem::directory_iterator{std::filesystem::path{root_entry.first}},
                                       std::max(0l, off - 2))) {
        auto filename = entry.path().filename();
        LOG_TRACE_L2(logger, "Adding {} to buffer at offset {}", filename, off);
        auto permissions = entry.status().permissions();
        struct stat stbuf {};
        stbuf.st_ino = 2;  // This is of course wrong
        stbuf.st_mode = std::to_underlying(permissions);
        // fuse_req_t is ignored (1st parameter)
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), filename.c_str(), &stbuf, off++);

        if ((total_size += entry_size) > size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
            return response;
        }

        if (entry_size > buffer.size()) {
            throw std::runtime_error("Buffer too small");
        }

        response.add_raw(buffer.data(), entry_size);
    }

    LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - 1, total_size);
    return response;
}

struct Callback {
    size_t size;
    MessageReceiver message;
};

auto read_callback = [](int32_t syscall_ret, IoUring<MessageReceiver>::CallbackErased* ptr) -> MessageReceiver&& {
    auto& callback = *dynamic_cast<IoUring<MessageReceiver>::CallbackFam<Callback>*>(ptr);
    if (syscall_ret >= 0) [[likely]] {
        callback.user_data.message.add_nocopy(
            callback.user_buffer, callback.user_data.size, [](auto, auto p) { ::operator delete(p); },
            reinterpret_cast<void*>(ptr));
        return std::move(callback.user_data.message);
    } else if (syscall_ret == -EAGAIN) {
        return std::move(callback.user_data.message);
    } else {
        throw std::system_error(-syscall_ret, std::generic_category(), "Read result");
    }
};
void Syscalls::read(MessageReceiver& message, Syscalls::IoUringImpl& uring) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto to_read = message.get_usr_data<size_t>(1);
    auto off = message.get_usr_data<off_t>(2);
    LOG_TRACE_L1(logger, "Received read for ino {}, with size {} and offset {}", ino, to_read, off);
    auto file_handle = inode_cache.inode_from_ino(ino).second.handle();
    // TODO: Test two modes of allocation:
    //  1. Always allocate a pagesize: Wasteful for small files
    //  2. Dynamic, minimally sized

    uring.queue_read<Callback>(file_handle, to_read, off, read_callback, to_read, message.respond());
}

MessageReceiver Syscalls::open(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto file_info = message.copy_usr_data<fuse_file_info>(1);
    auto response = message.respond();

    if (!(file_info.flags & (O_RDWR | O_WRONLY))) {
        // Only read-only for now
        auto& inode = inode_cache.inode_from_ino(ino);
        InodeCache::open(inode);

        response.add(&file_info);
    }

    return response;
}

MessageReceiver Syscalls::release(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto& inode = inode_cache.inode_from_ino(ino);
    InodeCache::close(inode);
    return message.respond();
}

}  // namespace remotefs
