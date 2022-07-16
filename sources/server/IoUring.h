#ifndef REMOTE_FS_IOURING_H
#define REMOTE_FS_IOURING_H

#include <liburing.h>
#include <quill/Quill.h>
#include <sys/sysmacros.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>

namespace remotefs {

template <typename PeekReturn>
class IoUring {
    static const auto queue_depth = 64;

   public:
    struct CallbackErased {
        using CallbackFn = PeekReturn && (int32_t syscall_ret, CallbackErased*);
        explicit CallbackErased(CallbackFn* cb)
            : callback(cb) {}
        virtual ~CallbackErased() = default;
        CallbackFn* callback;
    };

    template <class UserType, class BufferType>
    struct CallbackStatic final : public CallbackErased {
        template <typename... Ts>
        explicit CallbackStatic(typename CallbackErased::CallbackFn* cb, Ts&&... d)
            : CallbackErased{cb},
              user_data{std::forward<Ts>(d)...} {}
        // TODO: user_data could as well be the lambda capture
        UserType user_data;
        BufferType buffer;
        std::array<char, PATH_MAX> path{};
    };

    template <class UserType>
    struct CallbackFam final : public CallbackErased {
        UserType user_data;
        alignas(8) char user_buffer[];
    };

    IoUring();
    ~IoUring();

    template <typename UserData, typename... Ts>
    UserData& queue_statx(int dir_fd, const std::string& path, typename CallbackErased::CallbackFn* cb,
                          Ts&&... user_data);

    template <typename UserData, typename... Ts>
    void queue_read(int fd, std::size_t size, std::size_t offset, typename CallbackErased::CallbackFn* cb,
                    Ts&&... user_data);
    std::pair<int, std::unique_ptr<CallbackErased, void (*)(void*)>> queue_peek();
    void submit();
    [[nodiscard]] int fd() const;

   private:
    io_uring ring;
    quill::Logger* logger;
};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
template <typename PeekReturn>
IoUring<PeekReturn>::IoUring()
    : logger{quill::get_logger()} {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }
}
#pragma clang diagnostic pop

template <typename PeekReturn>
IoUring<PeekReturn>::~IoUring() {
    io_uring_queue_exit(&ring);
}

template <typename PeekReturn>
template <typename UserData, typename... Ts>
void IoUring<PeekReturn>::queue_read(int fd, std::size_t size, std::size_t offset,
                                     typename CallbackErased::CallbackFn* cb, Ts&&... user_data) {
    using Callback = CallbackFam<UserData>;
    auto callback = reinterpret_cast<Callback*>(::operator new(sizeof(Callback) + size));
    new (&callback->user_data) UserData({std::forward<Ts>(user_data)...});
    callback->callback = cb;

    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        io_uring_prep_read(sqe, fd, callback->user_buffer, size, offset);
        io_uring_sqe_set_data(sqe, callback);
    }
}

template <typename PeekReturn>
template <typename UserData, typename... Ts>
UserData& IoUring<PeekReturn>::queue_statx(int dir_fd, const std::string& path, typename CallbackErased::CallbackFn* cb,
                                           Ts&&... user_data) {
    using Statx = struct statx;
    static_assert(sizeof...(user_data) == 2);
    auto callback = new CallbackStatic<UserData, Statx>{cb, std::forward<Ts>(user_data)...};
    std::copy(path.cbegin(), path.cend(), callback->path.begin());

    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        LOG_TRACE_L1(logger, "Submitting to queue, dir={}, path={}", dir_fd, path.c_str());
        io_uring_prep_statx(sqe, dir_fd, callback->path.data(), 0, STATX_BASIC_STATS, &callback->buffer);
        io_uring_sqe_set_data(sqe, callback);
    } else {
        LOG_ERROR(logger, "Queue is full");
    }

    // TODO: On error, we should stop processing incoming messages and move on to submit the io queue.
    return callback->user_data;
}

template <typename PeekReturn>
std::pair<int, std::unique_ptr<typename IoUring<PeekReturn>::CallbackErased, void (*)(void*)>>
IoUring<PeekReturn>::queue_peek() {
    struct io_uring_cqe* cqe;
    if (auto ret = io_uring_peek_cqe(&ring, &cqe); ret == 0) [[likely]] {
        auto user_data = reinterpret_cast<CallbackErased*>(io_uring_cqe_get_data(cqe));
        io_uring_cqe_seen(&ring, cqe);
        return {ret, std::unique_ptr<typename IoUring<PeekReturn>::CallbackErased, void (*)(void*)>{
                         user_data, [](void* ptr) { ::operator delete(ptr); }}};
    } else if (ret == -EAGAIN) {
        return {ret, std::unique_ptr<typename IoUring<PeekReturn>::CallbackErased, void (*)(void*)>{
                         nullptr, [](void* ptr) { ::operator delete(ptr); }}};
    } else {
        throw std::system_error(-ret, std::generic_category(), "Queue peek. Not possible.");
    }
}

template <typename PeekReturn>
void IoUring<PeekReturn>::submit() {
    io_uring_submit(&ring);
}

template <typename PeekReturn>
int IoUring<PeekReturn>::fd() const {
    return ring.ring_fd;
}

}  // namespace remotefs

#endif  // REMOTE_FS_IOURING_H
