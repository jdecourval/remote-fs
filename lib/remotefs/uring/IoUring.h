#ifndef REMOTE_FS_IOURING_H
#define REMOTE_FS_IOURING_H

#include <poll.h>
#include <sys/epoll.h>

#include <cassert>
#include <concepts>
#include <memory_resource>
#include <type_traits>

#include "RegisteredBufferCache.h"
#include "cstdlib"
#include "filesystem"
#include "liburing.h"
#include "memory"
#include "remotefs/tools/Bytes.h"
#include "remotefs/tools/Casts.h"
#include "span"
#include "sys/sysmacros.h"

namespace remotefs {

class IoUring {
   public:
    static constexpr auto queue_depth_default = 64;
    static constexpr auto wait_min_batch_size_default = 1;
    static constexpr auto wait_timeout_default = std::chrono::seconds{1};
    static constexpr auto buffers_count_default = 64;
    static constexpr auto max_wait_min_batch_size = 16;  // Compile time limit
    static constexpr auto buffers_alignment = 8;
    static constexpr auto buffers_size = 1052672 + 100 + 20;

    using MemoryResource = CachedRegisteredBuffersResource<buffers_size>;

    template <class T>
    static std::pmr::polymorphic_allocator<T> get_allocator() {
        return std::pmr::polymorphic_allocator<T>{&get_pool()};
    }

    template <class Callable, typename Storage = void>
    class Callback;

    class CallbackErased;

    using CallbackDeleter = decltype([](CallbackErased* ptr) { get_allocator<CallbackErased>().delete_object(ptr); });
    using CallbackErasedUniquePtr = std::unique_ptr<CallbackErased, CallbackDeleter>;

    class CallbackErased {
       public:
        virtual ~CallbackErased() = default;

       private:
        friend IoUring;
        virtual void operator()(int res) = 0;
        virtual void operator()(int res, std::unique_ptr<CallbackErased, CallbackDeleter>) = 0;
    };

    template <class Callable>
    class Callback<Callable, void> final : public CallbackErased {
       public:
        explicit Callback(Callable&& c)
            : callable{std::forward<Callable>(c)} {}

       private:
        void inline operator()(int res) final {
            callable(res);
            CallbackDeleter{}(this);
        }

        void inline operator()(int res, std::unique_ptr<CallbackErased, CallbackDeleter>) final {
            callable(res);
        }

       private:
        Callable callable;
    };

    template <typename Storage>
    class CallbackWithStorageAbstract : public CallbackErased {
       public:
        template <typename... Ts>
        explicit CallbackWithStorageAbstract(Ts&&... args)
            : storage{std::forward<Ts>(args)...} {}

        alignas(buffers_alignment) alignas(Storage) Storage storage;
    };

    template <typename Callable, typename Storage>
    class Callback final : public CallbackWithStorageAbstract<Storage> {
        static_assert(!std::is_same_v<Storage, void>);

       public:
        template <typename... Ts>
        explicit Callback(Callable&& callable, Ts&&... args)
            : CallbackWithStorageAbstract<Storage>{std::forward<Ts>(args)...},
              callable{std::forward<Callable>(callable)} {}

       private:
        void inline operator()(int res) final {
            if constexpr (std::is_invocable_v<Callable, int>) {
                callable(res);
            } else if constexpr (std::is_invocable_v<
                                     Callable, int,
                                     std::unique_ptr<CallbackWithStorageAbstract<Storage>, CallbackDeleter>>) {
                assert(false);
            } else {
                static_assert(std::is_invocable_v<Callable, int>);
            }
        }

        void inline operator()(int res, std::unique_ptr<CallbackErased, CallbackDeleter> self) final {
            if constexpr (std::is_invocable_v<Callable, int>) {
                callable(res);
            } else if constexpr (std::is_invocable_v<
                                     Callable, int,
                                     std::unique_ptr<CallbackWithStorageAbstract<Storage>, CallbackDeleter>>) {
                // TODO: This is not exception safe
                callable(
                    res,
                    CallbackWithStorageAbstractUniquePtr<Storage>{
                        polymorphic_downcast<CallbackWithStorageAbstract<Storage>*>(self.release())}
                );
            } else {
                static_assert(std::is_invocable_v<
                              Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>, CallbackDeleter>>);
            }
        }

       public:
        Callable callable;
    };

    template <typename Callable, typename Storage = void>
    using CallbackUniquePtr = std::unique_ptr<Callback<Callable, Storage>, CallbackDeleter>;

    template <typename Storage>
    using CallbackWithStorageAbstractUniquePtr = std::unique_ptr<CallbackWithStorageAbstract<Storage>, CallbackDeleter>;

    //    template <typename Callable>
    //    using RegisteredBuffer = CallbackUniquePtr<Callable, std::array<std::byte, BufferSize>>;

    template <auto Alignement>
    class alignas(Alignement) AlignedTestClass {};

    template <typename Callable>
    static inline constexpr auto MAX_CALLBACK_PAYLOAD_SIZE =
        2UL * buffers_size - sizeof(Callable) -
        sizeof(Callback<Callable, std::array<std::byte, buffers_size - sizeof(Callable)>>);

    explicit IoUring(int queue_depth = queue_depth_default, short registered_buffers = buffers_count_default);
    IoUring(IoUring&& source) noexcept;
    IoUring& operator=(IoUring source) noexcept;
    IoUring(const IoUring& source) = delete;
    IoUring& operator=(const IoUring& source) = delete;
    ~IoUring();

    // Careful, path must remain alive until the ring is submitted.
    void queue_statx(int dir_fd, const std::string& path, struct statx* result, CallbackErasedUniquePtr callback);

    // Careful, path must remain alive until the ring is submitted.
    void queue_statx(int dir_fd, const std::string& path, CallbackWithStorageAbstractUniquePtr<struct statx> callback);

    void add_fd(int fd, CallbackErasedUniquePtr callback);

    void accept(int socket, CallbackErasedUniquePtr callback);

    void accept_fixed(int socket, CallbackErasedUniquePtr callback);

    void read(int fd, std::span<std::byte> destination, size_t offset, CallbackErasedUniquePtr callback);

    // For registered buffer: target must be in callback
    // TODO: CallbackWithStorageAbstractUniquePtr<Storage> is mainly used to distinguish between registered buffers and
    // regular
    //  buffers. Find a typesafe way instead.
    template <typename Storage>
    void read_fixed(int fd, std::span<std::byte> target, CallbackWithStorageAbstractUniquePtr<Storage> callback);

    template <typename Callable>
    void read_fixed(int fd, Callable&& callable);

    void write(int fd, std::span<std::byte> source, CallbackErasedUniquePtr callback);

    // For registered buffer: source must be in callback
    template <typename Storage>
    void write_fixed(int fd, std::span<std::byte> source, CallbackWithStorageAbstractUniquePtr<Storage> callback);

    template <typename Storage>
    void write_fixed(int fd, CallbackWithStorageAbstractUniquePtr<Storage> callback);

    template <size_t size>
    void write_vector(int fd, std::span<const iovec, size> sources, CallbackErasedUniquePtr callback);

    unsigned queue_wait(
        int min_batch_size = wait_min_batch_size_default, std::chrono::nanoseconds wait_timeout = wait_timeout_default
    );

    void register_ring();
    void register_sparse_files(int count);
    void register_sparse_buffers(int count);
    void assign_buffer(int idx, std::span<const std::byte> buffer);
    void assign_file(int idx, int file);

    template <typename Storage = void, typename Callable, typename... Ts>
    [[nodiscard]] CallbackWithStorageAbstractUniquePtr<Storage> get_callback(
        Callable&& callable, Ts&&... storage_args
    ) {
        auto* ptr = get_allocator<Callback<Callable, Storage>>().template new_object<Callback<Callable, Storage>>(
            std::forward<Callable>(callable), std::forward<Ts>(storage_args)...
        );
        assert(ptr);
        return CallbackWithStorageAbstractUniquePtr<Storage>{ptr};
    }

    template <typename Callable>
    [[nodiscard]] CallbackErasedUniquePtr get_callback(Callable&& callable) {
        auto* ptr =
            get_allocator<Callback<Callable>>().template new_object<Callback<Callable>>(std::forward<Callable>(callable)
            );
        assert(ptr);
        return CallbackErasedUniquePtr{ptr};
    }

   private:
    static MemoryResource& get_pool();

    io_uring ring{};

    io_uring_sqe* get_sqe(CallbackErasedUniquePtr callable) {
        auto callable_ptr = callable.release();

        if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
            io_uring_sqe_set_data(sqe, callable_ptr);
            return sqe;
        }

        io_uring_submit(&ring);

        if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
            io_uring_sqe_set_data(sqe, callable_ptr);
            return sqe;
        }

        throw std::runtime_error("Failed to get an SQE from the ring");
    }
};

void IoUring::queue_statx(int dir_fd, const std::string& path, struct statx* result, CallbackErasedUniquePtr callback) {
    assert(dir_fd >= 0 || dir_fd == AT_FDCWD);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_statx(sqe, dir_fd, path.c_str(), 0, STATX_BASIC_STATS, result);
}

void IoUring::queue_statx(
    int dir_fd, const std::string& path, CallbackWithStorageAbstractUniquePtr<struct statx> callback
) {
    auto result = &callback->storage;
    queue_statx(dir_fd, path, result, std::move(callback));
}

// CallbackUniquePtr<Callable> when no result (because the lambda is already there to store something)
void IoUring::add_fd(int fd, CallbackErasedUniquePtr callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_poll_multishot(sqe, fd, POLLIN);
}

void IoUring::accept(int socket, CallbackErasedUniquePtr callback) {
    assert(socket >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_multishot_accept(sqe, socket, nullptr, nullptr, 0);
}

void IoUring::accept_fixed(int socket, CallbackErasedUniquePtr callback) {
    assert(socket >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    // TODO: Multishot requests should ask for a different kind of object. Should still take ownership of the pointer.
    io_uring_prep_multishot_accept_direct(sqe, socket, nullptr, nullptr, 0);
}

void IoUring::read(int fd, std::span<std::byte> destination, size_t offset, CallbackErasedUniquePtr callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read(sqe, fd, destination.data(), destination.size(), offset);
}

template <typename Storage>
void IoUring::read_fixed(int fd, std::span<std::byte> target, CallbackWithStorageAbstractUniquePtr<Storage> callback) {
    assert(fd >= 0);
    assert(callback);
    assert(target.data() >= reinterpret_cast<std::byte*>(callback.get()));
    assert(&target.back() <= reinterpret_cast<std::byte*>(callback.get() + sizeof(*callback)));
    auto index = get_pool().get_index(callback.get());
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read_fixed(sqe, fd, target.data(), target.size(), 0, index);
}

template <typename Callable>
void IoUring::read_fixed(int fd, Callable&& callable) {
    assert(fd >= 0);
    auto callback =
        get_callback<std::array<std::byte, MAX_CALLBACK_PAYLOAD_SIZE<Callable>>>(std::forward<Callable>(callable));
    auto index = get_pool().get_index(callback.get());
    auto view = singular_bytes(callback->storage);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read_fixed(sqe, fd, view.data(), view.size(), 0, index);
}

void IoUring::write(int fd, std::span<std::byte> source, CallbackErasedUniquePtr callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write(sqe, fd, source.data(), source.size(), 0);
}

// For registered buffer: source must be in callback
template <typename Storage>
void IoUring::write_fixed(int fd, std::span<std::byte> source, CallbackWithStorageAbstractUniquePtr<Storage> callback) {
    assert(fd >= 0);
    assert(callback);
    assert(source.data() >= reinterpret_cast<std::byte*>(callback.get()));
    assert(source.end().base() <= reinterpret_cast<std::byte*>(callback.get()) + sizeof(*callback));
    auto index = get_pool().get_index(callback.get());
    auto buffer_address = reinterpret_cast<std::byte*>(&callback->storage);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write_fixed(sqe, fd, buffer_address, source.size(), source.data() - buffer_address, index);
}

template <typename Storage>
void IoUring::write_fixed(int fd, CallbackWithStorageAbstractUniquePtr<Storage> callback) {
    assert(fd >= 0);
    assert(callback);
    auto view = singular_bytes(callback->storage);
    auto index = get_pool().get_index(callback.get());
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write_fixed(sqe, fd, view.data(), view.size(), 0, index);
}

template <size_t size>
void IoUring::write_vector(int fd, std::span<const iovec, size> sources, CallbackErasedUniquePtr callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_writev(sqe, fd, sources.data(), sources.size(), 0);
}

}  // namespace remotefs

#endif  // REMOTE_FS_IOURING_H
