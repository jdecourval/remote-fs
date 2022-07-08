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
}

MessageReceiver Syscalls::lookup(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto path = message.get_usr_data_string(1);
    auto response = message.respond();

    if (auto entry = inode_cache.lookup((std::filesystem::path{inode_cache.inode_from_ino(ino).first} / path).c_str());
        entry != nullptr) {
        auto result = fuse_entry_param{.ino = entry->second.stat.st_ino,
                                       .generation = 0,
                                       .attr = entry->second.stat,
                                       .attr_timeout = 1,
                                       .entry_timeout = 1};
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
    response.add_raw(&entry.second.stat, sizeof(entry.second.stat));
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

MessageReceiver Syscalls::read(MessageReceiver& message) {
    auto ino = message.get_usr_data<fuse_ino_t>(0);
    auto to_read = message.get_usr_data<size_t>(1);
    auto off = message.get_usr_data<off_t>(2);
    std::array<char, 1024> buffer;
    LOG_TRACE_L1(logger, "Received read for ino {}, with size {} and offset {}", ino, to_read, off);
    auto response = message.respond();

    auto file_handle = inode_cache.inode_from_ino(ino).second.handle.get();
    std::fseek(file_handle, off, SEEK_SET);

    while (to_read > 0) {
        auto read = std::fread(buffer.data(), 1, std::min(buffer.size(), to_read), file_handle);
        if (read == 0) {
            break;
        }

        to_read -= read;
        response.add_raw(buffer.data(), read);
    }

    return response;
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
