#ifndef REMOTE_FS_IOURING_H
#define REMOTE_FS_IOURING_H

#include <liburing.h>

#include <span>

namespace remotefs {

class IoUring {
    static const auto queue_depth = 64;

   public:
    IoUring();
    ~IoUring();
    void queue_read(int fd, std::span<char> buffer, std::size_t offset, void* user_data);
    std::pair<size_t, void*> queue_peek();
    void submit();
    [[nodiscard]] int fd() const;

   private:
    io_uring ring;
};

}  // namespace remotefs

#endif  // REMOTE_FS_IOURING_H
