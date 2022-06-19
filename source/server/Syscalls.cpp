#include "Syscalls.h"

#include <fts.h>
#include <fuse_lowlevel.h>
#include <quill/Quill.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>

#include "InodeFinder.h"

namespace remotefs {
Syscalls::Syscalls() noexcept
    : logger{quill::get_logger()} {}

MessageReceiver Syscalls::lookup(MessageReceiver& message) {
    auto path = message.get_usr_data_string(1);
    auto result = fuse_entry_param{};
    auto response = message.respond();

    if (stat(path.data(), &result.attr) >= 0) {
        result.ino = result.attr.st_ino;
        result.generation = 0;
        result.attr_timeout = 1;
        result.entry_timeout = 1;
        response.add_raw(&result, sizeof(result));
        LOG_TRACE_L1(logger, "stat succeeded for path: {} -> ino: {}", path.data(), result.ino);
    } else {
        LOG_TRACE_L1(logger, "stat failed for path: {}: {}", path, std::strerror(errno));
    }

    return response;
}

MessageReceiver Syscalls::getattr(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto response = message.respond();

    if (ino == 1u) {
        struct stat result {};
        if (stat(".", &result) < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
        LOG_TRACE_L1(logger, "getattr for ino {} is .", ino);
        response.add_raw(&result, sizeof(result));
    } else if (auto stats = InodeFinder().stats(ino, FTS_D | FTS_F); stats) {
        LOG_TRACE_L1(logger, "getattr for ino {} found", ino);
        response.add_raw(&stats.value(), sizeof(stats.value()));
    } else {
        LOG_TRACE_L1(logger, "getattr: ino not found {}", ino);
    }

    return response;
}

MessageReceiver Syscalls::readdir(MessageReceiver& message) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto size = message.get_usr_data<size_t>(1);
    auto off = message.get_usr_data<off_t>(2) + 1;
    auto total_size = 0ull;
    //        const auto& fi = message.get_usr_data<fuse_file_info>(3);
    //        auto key = fi.fh;
    auto response = message.respond();
    std::array<char, 1024> buffer;
    LOG_TRACE_L1(logger, "Received readdir for ino {} with size {} and offset {}", ino, size, off);

    auto path = (ino == 1) ? std::optional{"."} : InodeFinder().path(ino, FTS_D);
    if (!path) {
        return response;
    }

    if (off == 0) {
        {
            auto permissions = std::filesystem::status(".").permissions();
            LOG_TRACE_L2(logger, "Adding . to buffer");
            struct stat stbuf {};
            stbuf.st_ino = 1;  // This is of course wrong
            stbuf.st_mode = std::to_underlying(permissions);
            // Must start at 1
            auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), ".", &stbuf, off++);
            if ((total_size += entry_size) > size) {
                LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
                return response;
            }
            response.add_raw(buffer.data(), entry_size);
        }

        {
            auto permissions = std::filesystem::status("..").permissions();
            LOG_TRACE_L2(logger, "Adding .. to buffer");
            struct stat stbuf = {};
            stbuf.st_ino = 1;  // This is of course wrong
            stbuf.st_mode = std::to_underlying(permissions);
            auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), "..", &stbuf, off++);
            if ((total_size += entry_size) > size) {
                LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
                return response;
            }
            response.add_raw(buffer.data(), entry_size);
        }
    }

    for (const auto& entry : std::filesystem::directory_iterator{std::filesystem::path{path.value()}} |
                                 std::views::drop(std::max(0l, off - 2))) {
        auto filename = entry.path().filename();
        LOG_TRACE_L2(logger, "Adding {} to buffer", filename);
        auto permissions = entry.status().permissions();
        struct stat stbuf {};
        stbuf.st_ino = 2;  // This is of course wrong
        stbuf.st_mode = std::to_underlying(permissions);
        // fuse_req_t is ignored
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), filename.c_str(), &stbuf, off++);

        if ((total_size += entry_size) > size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
            return response;
        }

        if (entry_size > buffer.size()) {
            throw std::runtime_error("Buffer too small");
        }

        // This does a copy
        response.add_raw(buffer.data(), entry_size);
    }

    LOG_TRACE_L1(logger, "readdir responded with {} parts and size", off - 1, total_size);
    return response;
}

MessageReceiver Syscalls::read(MessageReceiver& message) {
    auto ino = message.get<fuse_ino_t>(3);
    auto to_read = message.get<size_t>(4);
    auto off = message.get<off_t>(5);
    //        auto fi = reinterpret_cast<const struct fuse_file_info*>(message.raw_data(6));
    //        auto key = fi->fh;
    std::array<char, 1024> buffer;
    LOG_TRACE_L1(logger, "Received read for ino {}, with size {} and offset {}", ino, to_read, off);
    auto response = message.respond();

    if (auto path = remotefs::InodeFinder().path(ino, FTS_F); path) {
        auto file = std::ifstream(*path, std::ios::in | std::ios::binary);
        if (!file.fail()) {
            file.seekg(off);
            while (file && to_read > 0) {
                file.read(buffer.data(), std::min(buffer.size(), to_read));
                auto written = file.gcount();
                to_read -= file.gcount();
                response.add_raw(buffer.data(), written);
            }
        }
    } else {
        LOG_TRACE_L1(logger, "read: file not found: {}", ino);
    }

    return response;
}
}  // namespace remotefs
