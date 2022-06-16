#ifndef REMOTE_FS_INODEFINDER_H
#define REMOTE_FS_INODEFINDER_H

#include <fts.h>
#include <sys/stat.h>

#include <filesystem>
#include <optional>

namespace remotefs {
class InodeFinder {
   public:
    explicit InodeFinder();
    std::optional<std::filesystem::path> path(ino64_t inode, unsigned fts_info_mask);
    std::optional<struct stat> stats(ino64_t inode, unsigned fts_info_mask = FTS_D | FTS_F);

   private:
    const char* start[2] = {".", nullptr};
    std::unique_ptr<FTS, decltype(&fts_close)> file_system;
};
}  // namespace remotefs

#endif  // REMOTE_FS_INODEFINDER_H
