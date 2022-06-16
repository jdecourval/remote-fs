#include "InodeFinder.h"

#include <quill/Quill.h>

#include <cstring>

namespace remotefs {

InodeFinder::InodeFinder()
    // Seems OK to cast away the const after looking at one fts_open implementation
    : file_system{fts_open(const_cast<char* const*>(start), FTS_PHYSICAL | FTS_XDEV, nullptr), fts_close} {}

std::optional<std::filesystem::path> InodeFinder::path(ino64_t inode, unsigned int fts_info_mask) {
    for (auto file = fts_read(file_system.get()); file != nullptr; file = fts_read(file_system.get())) {
        if (file->fts_statp->st_ino == inode && file->fts_info & fts_info_mask) {
            LOG_TRACE_L1(quill::get_logger(), "Found file: {} for ino: {}", file->fts_path, inode);
            return {file->fts_path};
        }
    }

    return {};
}

std::optional<struct stat> InodeFinder::stats(ino64_t inode, unsigned int fts_info_mask) {
    return path(inode, fts_info_mask).transform([](const auto& path) {
        struct stat result {};
        if (stat(path.c_str(), &result) < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
        return result;
    });
}
}  // namespace remotefs
