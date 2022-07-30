#ifndef REMOTE_FS_IOURING_H
#define REMOTE_FS_IOURING_H

#include <poll.h>
#include <sys/epoll.h>

#include <type_traits>

#include "cstdlib"
#include "filesystem"
#include "liburing.h"
#include "memory"
#include "span"
#include "sys/sysmacros.h"

namespace remotefs {

class IoUring {
    static const auto queue_depth = 16;
    static const auto wait_min_batch_size = 1;
    static const auto wait_max_batch_size = 100;
    static const auto wait_timeout_s = 1;

   public:
    struct CallbackErased {
        virtual void operator()(int res) = 0;
        virtual ~CallbackErased() = default;
    };

    // TODO: Can this class be replaced by a lambda and CallbackErased by an Invocable concept?
    template <class Callable>
    struct CallbackWithPointer final : public CallbackErased {
        static_assert(std::is_invocable_v<Callable, int>);
        void inline operator()(int res) final {
            callable(res);
        }
        explicit CallbackWithPointer(Callable&& c)
            : callable{std::move(c)} {}
        Callable callable;
    };

    IoUring();
    ~IoUring();

    template <typename Callable>
    void queue_statx(int dir_fd, const std::string* path, struct statx* result, Callable&& callable);

    template <typename Callable>
    void add_fd(int fd, Callable&& callable);

    template <typename Callable>
    void accept(int socket, Callable&& callable);

    template <typename Callable>
    void read(int fd, std::span<char> destination, size_t offset, Callable&& callable);

    template <typename Callable>
    void write(int fd, std::span<char> source, Callable&& callable);

    void queue_peek();
    void queue_wait();
    void submit();

   private:
    io_uring ring;
};

template <typename Callable>
void IoUring::queue_statx(int dir_fd, const std::string* path, struct statx* result, Callable&& callable) {
    auto callback = new CallbackWithPointer<Callable>{std::forward<Callable>(callable)};
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        // path must stay alive until submit
        io_uring_prep_statx(sqe, dir_fd, path->c_str(), 0, STATX_BASIC_STATS, result);
        io_uring_sqe_set_data(sqe, callback);
    } else {
        // TODO: On error, we should stop processing incoming messages and move on to submit the io queue.
        //        LOG_ERROR(logger, "Queue is full");
    }
}

template <typename Callable>
void IoUring::add_fd(int fd, Callable&& callable) {
    auto callback = new CallbackWithPointer<Callable>{std::forward<Callable>(callable)};
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        io_uring_prep_poll_multishot(sqe, fd, POLLIN);
        io_uring_sqe_set_data(sqe, callback);
    }
}

template <typename Callable>
void IoUring::accept(int socket, Callable&& callable) {
    auto callback = new CallbackWithPointer<Callable>{std::forward<Callable>(callable)};
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        io_uring_prep_accept(sqe, socket, nullptr, nullptr, 0);
        io_uring_sqe_set_data(sqe, callback);
    }
}

template <typename Callable>
void IoUring::read(int fd, std::span<char> destination, size_t offset, Callable&& callable) {
    auto callback = new CallbackWithPointer<Callable>{std::forward<Callable>(callable)};
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        io_uring_prep_read(sqe, fd, destination.data(), destination.size(), offset);
        io_uring_sqe_set_data(sqe, callback);
    }
}

template <typename Callable>
void IoUring::write(int fd, std::span<char> source, Callable&& callable) {
    auto callback = new CallbackWithPointer<Callable>{std::forward<Callable>(callable)};
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        io_uring_prep_write(sqe, fd, source.data(), source.size(), 0);
        io_uring_sqe_set_data(sqe, callback);
    }
}

}  // namespace remotefs

#endif  // REMOTE_FS_IOURING_H
