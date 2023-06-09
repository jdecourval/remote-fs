#include "Syscalls.h"

#include <fuse_lowlevel.h>
#include <quill/Quill.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>

#include "remotefs/uring/IoUring.h"

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

Syscalls::Syscalls(IoUring& ring, InodeCache& cache)
    : logger{quill::get_logger()},
      uring{ring},
      inode_cache{cache} {}

void Syscalls::lookup(messages::requests::Lookup& message, int socket) {
    auto ino = message.ino;
    auto root_path = std::filesystem::path{inode_cache.inode_from_ino(ino).first};
    auto path = std::make_unique<std::string>(root_path / message.path.data());
    LOG_DEBUG(logger, "Looking up path={}, relative={}, root={}", *path, &message.path[0], root_path.string());

    if (auto found = inode_cache.find(*path)) {
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

    auto* path_ptr = path.get();

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
            logger, "Sending FuseReplyEntry req={}, ino={}", static_cast<void*>(response->get_storage().req),
            response->get_storage().attr.ino
        );
        uring.write_fixed(socket, std::move(response));
    });

    LOG_INFO(logger, "PATH: {}", *path_ptr);
    uring.queue_statx(AT_FDCWD, *path_ptr, std::move(callback));
}

void Syscalls::getattr(messages::requests::GetAttr& message, int socket) {
    const auto& entry = inode_cache.inode_from_ino(message.ino);
    auto callback = uring.get_callback<messages::responses::FuseReplyAttr>([](int) {}, message.req, entry.second.stat);
    LOG_TRACE_L2(
        logger, "Sending FuseReplyAttr req={}, ino={}", static_cast<void*>(callback->get_storage().req),
        entry.second.stat.st_ino
    );
    uring.write_fixed(socket, std::move(callback));
}

void Syscalls::readdir(messages::requests::ReadDir& message, int socket) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.ino;
    auto off = message.offset + 1;
    auto callable = [](int) {};
    auto callback =
        uring.get_callback<messages::responses::FuseReplyBuf<IoUring::MaxPayloadForCallback<decltype(callable)>()>>(
            std::move(callable), message.req, message.size
        );
    LOG_TRACE_L1(
        logger, "Received readdir for ino {} with size {} and offset {} for req {}", ino, message.size, off,
        static_cast<void*>(message.req)
    );

    const auto& root_entry = inode_cache.inode_from_ino(ino);

    [&]() {
        if (off == 1) {  // off must start at 1
            LOG_TRACE_L3(logger, "Adding . to buffer");
            if (!callback->get_storage().add_directory_entry(".", root_entry.second.stat, off)) {
                return;
            }

            off++;
        }

        if (off == 2) {
            LOG_TRACE_L3(logger, "Adding .. to buffer");
            const struct stat stbuf = {
                .st_ino = 1, .st_mode = std::to_underlying(std::filesystem::status("..").permissions())};

            if (!callback->get_storage().add_directory_entry("..", stbuf, off)) {
                return;
            }

            off++;
        }

        for (const auto& entry : std::next(
                 std::filesystem::directory_iterator{std::filesystem::path{root_entry.first}}, std::max(0l, off - 3)
             )) {
            LOG_TRACE_L3(logger, "Adding {} to buffer at offset {}", entry.path().filename(), off);

            const struct stat stbuf {
                .st_ino = 2,  // This is of course wrong
                    .st_mode = std::to_underlying(entry.status().permissions())
                // Other fields are not currently used by fuse
            };

            if (!callback->get_storage().add_directory_entry(entry.path(), stbuf, off)) {
                return;
            }

            off++;
        }
    }();

    LOG_TRACE_L2(
        logger, "Sending FuseReplyBuf req={}, size={}", static_cast<void*>(callback->get_storage().req),
        callback->get_storage().payload_size
    );
    auto view = callback->get_storage().outer_view();
    uring.write_fixed(socket, view, std::move(callback));
}

void Syscalls::read(messages::requests::Read& message, int socket) {
    LOG_TRACE_L1(
        logger, "Received read for ino {}, with size {} and offset {}, req={}", message.ino, message.size,
        message.offset, static_cast<void*>(message.req)
    );
    auto file_handle = inode_cache.inode_from_ino(message.ino).second.handle();

    auto callable = [this, socket](int ret, auto old_callback) {
        if (ret >= 0) [[likely]] {
            auto callback = uring.get_callback([](int) {}, std::move(old_callback));
            callback->get_storage().set_size(ret);
            LOG_TRACE_L1(
                logger, "Sending FuseReplyBuf req={}, inner size={}, outer size={}",
                static_cast<void*>(callback->get_storage().req), callback->get_storage().read_view().size(),
                callback->get_storage().outer_view().size()
            );
            auto view = callback->get_storage().outer_view();
            uring.write_fixed(socket, view, std::move(callback));
        } else {
            auto callback_error = uring.get_callback<messages::responses::FuseReplyErr>(
                [](int) {}, old_callback->get_storage().req, -ret
            );
            LOG_TRACE_L1(logger, "Sending FuseReplyErr");
            uring.write_fixed(socket, std::move(callback_error));
        }
    };

    auto callback = uring.get_callback<
        messages::responses::FuseReplyBuf<IoUring::MaxPayloadForCallback<decltype([this, socket](int) {})>()>>(
        std::move(callable), message.req, message.size
    );
    auto buffer_view = callback->get_storage().write_view();
    uring.read_fixed(file_handle, buffer_view, message.offset, std::move(callback));
}

void Syscalls::open(messages::requests::Open& message, int socket) {
    auto ino = message.ino;
    auto file_info = message.file_info;

    // TODO: Add FOPEN_PARALLEL_DIRECT_WRITES to flags. Probably better doing it client side.
    if (!(file_info.flags & (O_RDWR | O_WRONLY))) {
        // Only read-only for now
        auto& inode = inode_cache.inode_from_ino(ino);
        InodeCache::open(inode);  // TODO: Handle errors
        auto callback = uring.get_callback<messages::responses::FuseReplyOpen>([](int) {}, message.req, file_info);
        LOG_TRACE_L2(logger, "Sending FuseReplyOpen");
        uring.write_fixed(socket, std::move(callback));
    } else {
        auto callback = uring.get_callback<messages::responses::FuseReplyErr>([](int) {}, message.req, EACCES);
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
