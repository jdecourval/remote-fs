#include "Syscalls.h"

#include <fuse_lowlevel.h>
#include <quill/Quill.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>

namespace remotefs {
namespace {
struct stat statx_to_stat(const struct statx input) {
    using Stat = struct stat;
    return Stat{
        .st_dev = makedev(input.stx_dev_major, input.stx_dev_minor),
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

}  // namespace

Syscalls::Syscalls(IoUring& ring)
    : logger{quill::get_logger()},
      uring{ring} {}

void Syscalls::lookup(messages::requests::Lookup& message, int socket) {
    auto ino = message.ino;
    auto root_path = std::filesystem::path{inode_cache.inode_from_ino(ino).first};
    auto path = std::make_unique<std::filesystem::path>(root_path / message.path.data());
    LOG_DEBUG(logger, "Looking up path={}, relative={}, root={}", *path, &message.path[0], root_path.string());

    auto found = inode_cache.find(*path);
    if (found) {
        auto callback = uring.get_callback<messages::responses::FuseReplyEntry>(
            [](int) {}, message.req,
            fuse_entry_param{
                .ino = found->second.stat.st_ino,
                .generation = 0,
                .attr = found->second.stat,
                .attr_timeout = 1,
                .entry_timeout = 1}
        );
        uring.write_fixed(socket, std::move(callback));
        return;
    }

    auto path_ptr = path.get();

    // path is moved into the closure because it needs to stay alive until iouring submit.
    auto callback = uring.get_callback<struct statx>([this, req = message.req, socket,
                                                      path = std::move(path)](int ret, auto callback) mutable {
        auto response = uring.get_callback<messages::responses::FuseReplyEntry>([](int) {}, req);

        if (ret < 0) [[unlikely]] {
            LOG_DEBUG(logger, "queue_statx callback failure, ret={}: {}", -ret, std::strerror(-ret));
            auto error_response = uring.get_callback<messages::responses::FuseReplyErr>([](int) {}, req, -ret);
            uring.write_fixed(socket, std::move(error_response));
            return;
        }

        auto stat = statx_to_stat(callback->get_storage());
        LOG_TRACE_L1(logger, "queue_statx callback success, uid={}, size={}", stat.st_uid, stat.st_size);

        response->get_storage().attr = fuse_entry_param{
            .ino = reinterpret_cast<fuse_ino_t>(&inode_cache.create_inode(std::move(*path), stat)),
            .generation = 0,
            .attr = stat,
            .attr_timeout = 1,
            .entry_timeout = 1};
        response->get_storage().attr.attr.st_ino = response->get_storage().attr.ino;
        LOG_TRACE_L2(
            logger, "Sending FuseReplyEntry req={}, ino={}", reinterpret_cast<uintptr_t>(response->get_storage().req),
            response->get_storage().attr.ino
        );
        uring.write_fixed(socket, std::move(response));
    });

    LOG_INFO(logger, "PATH: {}", *path_ptr);
    uring.queue_statx(AT_FDCWD, path_ptr->string(), std::move(callback));
}

void Syscalls::getattr(messages::requests::GetAttr& message, int socket) {
    const auto& entry = inode_cache.inode_from_ino(message.ino);
    auto callback = uring.get_callback<messages::responses::FuseReplyAttr>([](int) {}, message.req, entry.second.stat);
    LOG_TRACE_L2(
        logger, "Sending FuseReplyAttr req={}, ino={}", reinterpret_cast<uintptr_t>(callback->get_storage().req),
        entry.second.stat.st_ino
    );
    uring.write_fixed(socket, std::move(callback));
}

void Syscalls::readdir(messages::requests::ReadDir& message, int socket) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.ino;
    auto size = message.size;
    auto off = message.offset + 1;
    auto total_size = 0ull;
    auto callable = [](int) {};
    auto callback = uring.get_callback<messages::responses::FuseReplyBuf<>>(std::move(callable), message.req);
    LOG_TRACE_L1(
        logger, "Received readdir for ino {} with size {} and offset {} for req {}", ino, size, off,
        reinterpret_cast<uint64_t>(message.req)
    );

    const auto& root_entry = inode_cache.inode_from_ino(ino);

    if (off == 1) {  // off must start at 1
        LOG_TRACE_L3(logger, "Adding . to buffer");
        auto free_space = callback->get_storage().free_space();
        auto entry_size = fuse_add_direntry(
            nullptr, reinterpret_cast<char*>(callback->get_storage().data.data() + callback->get_storage().data_size),
            free_space, ".", &root_entry.second.stat, off
        );
        assert(entry_size <= callback->get_storage().data.size());
        assert(entry_size <= free_space);

        callback->get_storage().data_size += entry_size;
        off++;
        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - message.offset - 1, total_size);
            LOG_TRACE_L2(
                logger, "Sending FuseReplyBuf req={}, size={}",
                reinterpret_cast<uintptr_t>(callback->get_storage().req), callback->get_storage().data_size
            );
            uring.write_fixed(socket, std::move(callback));
            return;
        }
    }

    if (off == 2) {
        auto permissions = std::filesystem::status("..").permissions();
        LOG_TRACE_L3(logger, "Adding .. to buffer");
        auto free_space = callback->get_storage().free_space();
        struct stat stbuf = {};
        // Other fields are not currently used by fuse
        stbuf.st_ino = 1;
        stbuf.st_mode = std::to_underlying(permissions);
        auto entry_size = fuse_add_direntry(
            nullptr, reinterpret_cast<char*>(callback->get_storage().data.data()) + callback->get_storage().data_size,
            free_space, "..", &stbuf, off
        );
        assert(entry_size <= callback->get_storage().data.size());
        assert(entry_size <= free_space);

        callback->get_storage().data_size += entry_size;
        off++;
        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - message.offset - 1, total_size);
            LOG_TRACE_L2(
                logger, "Sending FuseReplyBuf req={}, size={}",
                reinterpret_cast<uintptr_t>(callback->get_storage().req), callback->get_storage().data_size
            );
            uring.write_fixed(socket, std::move(callback));
            return;
        }
    }

    for (const auto& entry : std::next(
             std::filesystem::directory_iterator{std::filesystem::path{root_entry.first}}, std::max(0l, off - 3)
         )) {
        auto filename = entry.path().filename();
        LOG_TRACE_L3(logger, "Adding {} to buffer at offset {}", filename, off);
        auto free_space = callback->get_storage().free_space();
        auto permissions = entry.status().permissions();

        struct stat stbuf {};

        stbuf.st_ino = 2;  // This is of course wrong
        stbuf.st_mode = std::to_underlying(permissions);
        // fuse_req_t is ignored (1st parameter)
        auto entry_size = fuse_add_direntry(
            nullptr, reinterpret_cast<char*>(callback->get_storage().data.data()) + callback->get_storage().data_size,
            free_space, filename.c_str(), &stbuf, off
        );
        assert(entry_size <= free_space);
        callback->get_storage().data_size += entry_size;
        ++off;

        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - message.offset - 1, total_size);
            LOG_TRACE_L2(
                logger, "Sending FuseReplyBuf req={}, size={}",
                reinterpret_cast<uintptr_t>(callback->get_storage().req), callback->get_storage().data_size
            );
            uring.write_fixed(socket, std::move(callback));
            return;
        }
    }

    LOG_TRACE_L2(
        logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(callback->get_storage().req),
        callback->get_storage().data_size
    );
    uring.write_fixed(socket, std::move(callback));
}

void Syscalls::read(messages::requests::Read& message, int socket) {
    auto ino = message.ino;
    auto to_read = static_cast<int>(message.size);
    auto off = message.offset;
    LOG_TRACE_L1(
        logger, "Received read for ino {}, with size {} and offset {}, req={}", ino, to_read, off,
        reinterpret_cast<uintptr_t>(message.req)
    );
    auto file_handle = inode_cache.inode_from_ino(ino).second.handle();

    auto callable = [this, socket](
                        int ret,
                        IoUring::CallbackWithStorageAbstractUniquePtr<messages::responses::FuseReplyBuf<>> old_callback
                    ) {
        if (ret >= 0) [[likely]] {
            auto callback = uring.get_callback([](int) {}, std::move(old_callback));
            callback->get_storage().data_size = ret;
            LOG_TRACE_L2(
                logger, "Sending FuseReplyBuf req={}, size={}",
                reinterpret_cast<uintptr_t>(callback->get_storage().req), callback->get_storage().data_size
            );
            auto view = singular_bytes(callback->get_storage()).subspan(0, callback->get_storage().transmit_size());
            uring.write_fixed(socket, view, std::move(callback));
        } else {
            auto callback_error = uring.get_callback<messages::responses::FuseReplyErr>(
                [](int) {}, old_callback->get_storage().req, -ret
            );
            LOG_TRACE_L2(logger, "Sending FuseReplyErr");
            uring.write_fixed(socket, std::move(callback_error));
        }
    };
    auto callback = uring.get_callback<messages::responses::FuseReplyBuf<>>(std::move(callable), message.req);
    auto buffer_view = std::span{callback->get_storage().data.begin(), narrow_cast<size_t>(to_read)};
    assert(to_read <= callback->get_storage().data.size());
    uring.read_fixed(file_handle, buffer_view, std::move(callback));
}

void Syscalls::open(messages::requests::Open& message, int socket) {
    auto ino = message.ino;
    auto file_info = message.file_info;

    // TODO: Add FOPEN_PARALLEL_DIRECT_WRITES to flags. Probably better doing it client side.
    auto callable = [](int) {};
    if (!(file_info.flags & (O_RDWR | O_WRONLY))) {
        // Only read-only for now
        auto& inode = inode_cache.inode_from_ino(ino);
        InodeCache::open(inode);  // TODO: Handle errors
        auto callback =
            uring.get_callback<messages::responses::FuseReplyOpen>(std::move(callable), message.req, file_info);
        LOG_TRACE_L2(logger, "Sending FuseReplyOpen");
        uring.write_fixed(socket, std::move(callback));
    } else {
        auto callback = uring.get_callback<messages::responses::FuseReplyErr>(std::move(callable), message.req, EACCES);
        LOG_TRACE_L2(logger, "Sending FuseReplyErr");
        uring.write_fixed(socket, std::move(callback));
    }
}

void Syscalls::release(messages::requests::Release& message) {
    auto ino = message.ino;
    auto& inode = inode_cache.inode_from_ino(ino);
    InodeCache::close(inode);
}

void Syscalls::ping(std::unique_ptr<std::array<std::byte, settings::MAX_MESSAGE_SIZE>>&&, int) {}

}  // namespace remotefs
