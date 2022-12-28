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

}  // namespace

Syscalls::Syscalls(IoUring& ring)
    : logger{quill::get_logger()},
      uring{ring} {}

void Syscalls::lookup(messages::requests::Lookup& message, int socket) {
    auto ino = message.ino;
    auto root_path = std::filesystem::path{inode_cache.inode_from_ino(ino).first};
    auto path = root_path / message.path;
    LOG_DEBUG(logger, "Looking up path={}, relative={}, root={}", path.string(), &message.path[0], root_path.string());

    auto found = inode_cache.find(path);
    if (found) {
        auto response = std::make_unique<messages::responses::FuseReplyEntry>(
            messages::responses::FuseReplyEntry{message.req, fuse_entry_param{.ino = found->second.stat.st_ino,
                                                                              .generation = 0,
                                                                              .attr = found->second.stat,
                                                                              .attr_timeout = 1,
                                                                              .entry_timeout = 1}});
        auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
        uring.write(socket, response_view, [response = std::move(response)](int) {});
        return;
    }

    struct ResponseAndPathStats final : public messages::responses::FuseReplyEntry {
        ResponseAndPathStats(fuse_req_t r, std::string&& p)
            : messages::responses::FuseReplyEntry{r},
              path{std::move(p)} {}
        std::string path;
        struct statx stats {};
    };
    // TODO: Can probably remove one of the two allocation
    auto response = std::make_unique<ResponseAndPathStats>(ResponseAndPathStats{message.req, std::move(path)});
    auto* path_ptr = &response->path;
    auto* statx_ptr = &response->stats;

    uring.queue_statx(AT_FDCWD, path_ptr, statx_ptr, [this, socket, response = std::move(response)](int ret) mutable {
        auto req = response->req;
        if (ret < 0) [[unlikely]] {
            LOG_DEBUG(logger, "queue_statx callback failure, ret={}: ", -ret, std::strerror(-ret));
            auto response =
                std::make_unique<messages::responses::FuseReplyErr>(messages::responses::FuseReplyErr{req, -ret});
            auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
            uring.write(socket, response_view, [response = std::move(response)](int) {});
            return;
        }

        auto stat = statx_to_stat(response->stats);
        LOG_TRACE_L1(logger, "queue_statx callback success, uid={}, size={}", stat.st_uid, stat.st_size);

        response->attr = fuse_entry_param{
            .ino = reinterpret_cast<fuse_ino_t>(&inode_cache.create_inode(std::move(response->path), stat)),
            .generation = 0,
            .attr = stat,
            .attr_timeout = 1,
            .entry_timeout = 1};
        response->attr.attr.st_ino = response->attr.ino;
        auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
        LOG_TRACE_L2(logger, "Sending FuseReplyEntry req={}, ino={}", reinterpret_cast<uintptr_t>(response->req),
                     response->attr.ino);
        uring.write(socket, response_view, [response = std::move(response)](int) {});
    });
}

void Syscalls::getattr(messages::requests::GetAttr& message, int socket) {
    const auto& entry = inode_cache.inode_from_ino(message.ino);
    auto response = std::make_unique<messages::responses::FuseReplyAttr>(
        messages::responses::FuseReplyAttr{message.req, entry.second.stat});
    auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
    LOG_TRACE_L2(logger, "Sending FuseReplyAttr req={}, ino={}", reinterpret_cast<uintptr_t>(response->req),
                 entry.second.stat.st_ino);
    uring.write(socket, response_view, [response = std::move(response)](int) {});
}

void Syscalls::readdir(messages::requests::ReadDir& message, int socket) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.ino;
    auto size = message.size;
    auto off = message.offset + 1;
    auto total_size = 0ull;
    auto response = std::make_unique<messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>>(message.req);
    LOG_TRACE_L1(logger, "Received readdir for ino {} with size {} and offset {} for req {}", ino, size, off,
                 reinterpret_cast<uint64_t>(message.req));

    const auto& root_entry = inode_cache.inode_from_ino(ino);

    if (off == 1) {  // off must start at 1
        LOG_TRACE_L3(logger, "Adding . to buffer");
        auto free_space = response->free_space();
        auto entry_size =
            fuse_add_direntry(nullptr, reinterpret_cast<char*>(response->data.data() + response->data_size),
                              response->free_space(), ".", &root_entry.second.stat, off);
        assert(entry_size <= response->data.size());
        assert(entry_size <= free_space);

        response->data_size += entry_size;
        off++;
        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - message.offset - 1, total_size);
            auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), response->size()};
            LOG_TRACE_L2(logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(response->req),
                         response->data_size);
            uring.write(socket, response_view, [response = std::move(response)](int) {});
            return;
        }
    }

    if (off == 2) {
        auto permissions = std::filesystem::status("..").permissions();
        LOG_TRACE_L3(logger, "Adding .. to buffer");
        auto free_space = response->free_space();
        struct stat stbuf = {};
        // Other fields are not currently used by fuse
        stbuf.st_ino = 1;
        stbuf.st_mode = std::to_underlying(permissions);
        auto entry_size =
            fuse_add_direntry(nullptr, reinterpret_cast<char*>(response->data.data()) + response->data_size,
                              response->free_space(), "..", &stbuf, off);
        assert(entry_size <= response->data.size());
        assert(entry_size <= free_space);

        response->data_size += entry_size;
        off++;
        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - message.offset - 1, total_size);
            auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), response->size()};
            LOG_TRACE_L2(logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(response->req),
                         response->data_size);
            uring.write(socket, response_view, [response = std::move(response)](int) {});
            return;
        }
    }

    for (const auto& entry : std::next(std::filesystem::directory_iterator{std::filesystem::path{root_entry.first}},
                                       std::max(0l, off - 3))) {
        auto filename = entry.path().filename();
        LOG_TRACE_L3(logger, "Adding {} to buffer at offset {}", filename, off);
        auto free_space = response->free_space();
        auto permissions = entry.status().permissions();
        struct stat stbuf {};
        stbuf.st_ino = 2;  // This is of course wrong
        stbuf.st_mode = std::to_underlying(permissions);
        // fuse_req_t is ignored (1st parameter)
        auto entry_size =
            fuse_add_direntry(nullptr, reinterpret_cast<char*>(response->data.data()) + response->data_size,
                              response->free_space(), filename.c_str(), &stbuf, off);
        assert(entry_size <= free_space);
        response->data_size += entry_size;
        ++off;

        if ((total_size += entry_size) >= size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - message.offset - 1, total_size);
            auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), response->size()};
            LOG_TRACE_L2(logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(response->req),
                         response->data_size);
            uring.write(socket, response_view, [response = std::move(response)](int) {});
            return;
        }
    }

    auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), response->size()};
    LOG_TRACE_L2(logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(response->req),
                 response->data_size);
    uring.write(socket, response_view, [response = std::move(response)](int) {});
}

void Syscalls::read(messages::requests::Read& message, int socket) {
    auto ino = message.ino;
    auto to_read = static_cast<int>(message.size);
    auto off = message.offset;
    LOG_TRACE_L1(logger, "Received read for ino {}, with size {} and offset {}, req={}, {}", ino, to_read, off,
                 reinterpret_cast<uintptr_t>(message.req),
                 messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>::MAX_PAYLOAD_SIZE);
    auto file_handle = inode_cache.inode_from_ino(ino).second.handle();
    assert(to_read <= messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>::MAX_PAYLOAD_SIZE);
    // TODO: Test two modes of allocation:
    //  1. Always allocate a pagesize: Wasteful for small files
    //  2. Dynamic, minimally sized

    auto response = std::make_unique<messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>>(message.req);
    auto buffer = std::span{response->data.data(), static_cast<size_t>(to_read)};
    uring.read(file_handle, buffer, off, [this, socket, response = std::move(response)](int ret) mutable {
        if (ret >= 0) [[likely]] {
            response->data_size = ret;
            auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), response->size()};
            LOG_TRACE_L2(logger, "Sending FuseReplyBuf req={}, size={}", reinterpret_cast<uintptr_t>(response->req),
                         response->data_size);
            uring.write(socket, response_view, [response = std::move(response)](int ret) {
                if (ret < 0) {
                    throw std::system_error(-ret, std::system_category(), "Failed to write to socket");
                }
            });
        } else {
            auto error_response = std::make_unique<messages::responses::FuseReplyErr>(
                messages::responses::FuseReplyErr{response->req, -ret});
            auto response_view = std::span{reinterpret_cast<std::byte*>(error_response.get()), sizeof(*error_response)};
            LOG_TRACE_L2(logger, "Sending FuseReplyErr");
            uring.write(socket, response_view, [error_response = std::move(error_response)](int) {});
        }
    });
}

void Syscalls::open(messages::requests::Open& message, int socket) {
    auto ino = message.ino;
    auto file_info = message.file_info;

    // TODO: Add FOPEN_PARALLEL_DIRECT_WRITES to flags. Probably better doing it client side.
    if (!(file_info.flags & (O_RDWR | O_WRONLY))) {
        // Only read-only for now
        auto& inode = inode_cache.inode_from_ino(ino);
        InodeCache::open(inode);  // TODO: Handle errors
        auto response = std::make_unique<messages::responses::FuseReplyOpen>(
            messages::responses::FuseReplyOpen{message.req, file_info});
        auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
        LOG_TRACE_L2(logger, "Sending FuseReplyOpen");
        uring.write(socket, response_view, [response = std::move(response)](int) {});
    } else {
        auto response = std::make_unique<messages::responses::FuseReplyErr>(message.req, EACCES);
        auto response_view = std::span{reinterpret_cast<std::byte*>(response.get()), sizeof(*response)};
        LOG_TRACE_L2(logger, "Sending FuseReplyErr");
        uring.write(socket, response_view, [response = std::move(response)](int) {});
    }
}

void Syscalls::release(messages::requests::Release& message) {
    auto ino = message.ino;
    auto& inode = inode_cache.inode_from_ino(ino);
    InodeCache::close(inode);
}

void Syscalls::ping(std::unique_ptr<std::array<std::byte, settings::MAX_MESSAGE_SIZE>>&& buffer, int socket) {}

}  // namespace remotefs
