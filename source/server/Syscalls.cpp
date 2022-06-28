#include "Syscalls.h"

#include <fuse_lowlevel.h>
#include <quill/Quill.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>

namespace remotefs {

Syscalls::Syscalls() noexcept
    : logger{quill::get_logger()} {
    // Inodes 0 and 1 are special
    inode_cache.lookup(".");
    inode_cache.lookup(".");
}

MessageReceiver Syscalls::lookup(MessageReceiver& message) {
    auto path = message.get_usr_data_string(1);
    auto response = message.respond();

    if (auto entry = inode_cache.lookup(path); entry != nullptr) {
        auto result = fuse_entry_param{
            .ino = entry->stats.st_ino, .generation = 0, .attr = entry->stats, .attr_timeout = 1, .entry_timeout = 1};
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

    const auto& entry = inode_cache.inode_from_ino(ino);
    response.add_raw(&entry.stats, sizeof(entry.stats));
    LOG_TRACE_L1(logger, "getattr: ino found in cache{}", ino);

    return response;
}

MessageReceiver Syscalls::readdir(MessageReceiver& message) {
    // Probably not important to return a valid inode number
    // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto size = message.get_usr_data<size_t>(1);
    auto off = message.get_usr_data<off_t>(2) + 1;
    auto total_size = 0ull;
    auto response = message.respond();
    std::array<char, 1024> buffer;
    LOG_TRACE_L1(logger, "Received readdir for ino {} with size {} and offset {}", ino, size, off);

    const auto& root_entry = inode_cache.inode_from_ino(ino);

    if (off == 0) {
        LOG_TRACE_L2(logger, "Adding . to buffer");
        // off must start at 1
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), ".", &root_entry.stats, off++);
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
        stbuf.st_ino = 1;
        stbuf.st_mode = std::to_underlying(permissions);
        auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), "..", &stbuf, off++);
        if ((total_size += entry_size) > size) {
            LOG_TRACE_L1(logger, "readdir responded with {} parts and size {}", off - 2, total_size - entry_size);
            return response;
        }
        response.add_raw(buffer.data(), entry_size);
    }

    for (const auto& entry : std::next(std::filesystem::directory_iterator{std::filesystem::path{root_entry.path}},
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

MessageReceiver Syscalls::read(MessageReceiver& message) {
    auto ino = message.get<fuse_ino_t>(3);
    auto to_read = message.get<size_t>(4);
    auto off = message.get<off_t>(5);
    std::array<char, 1024> buffer;
    LOG_TRACE_L1(logger, "Received read for ino {}, with size {} and offset {}", ino, to_read, off);
    auto response = message.respond();

    const auto& path = inode_cache.inode_from_ino(ino).path;
    auto file = std::ifstream(path.data(), std::ios::in | std::ios::binary);
    if (!file.fail()) {
        file.seekg(off);
        while (file && to_read > 0) {
            file.read(buffer.data(), std::min(buffer.size(), to_read));
            auto written = file.gcount();
            to_read -= file.gcount();
            response.add_raw(buffer.data(), written);
        }
    }

    return response;
}
}  // namespace remotefs
