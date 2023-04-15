#ifndef REMOTE_FS_IOURING_H
#define REMOTE_FS_IOURING_H

#include <liburing.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cassert>
#include <concepts>
#include <cstdlib>
#include <filesystem>
#include <forward_list>
#include <memory>
#include <memory_resource>
#include <span>
#include <type_traits>

#include "Callbacks.h"
#include "CallbacksImpl.h"
#include "RegisteredBufferCache.h"
#include "remotefs/tools/Bytes.h"
#include "remotefs/tools/Casts.h"

namespace remotefs {

class IoUring {
   public:
    static constexpr auto queue_depth_default = 64;
    static constexpr auto wait_min_batch_size_default = 1;
    static constexpr auto wait_timeout_default = std::chrono::seconds{1};
    static constexpr auto buffers_count_default = 64;
    static constexpr auto max_wait_min_batch_size = 16;  // Compile time limit

    template <typename Callable>
    static inline constexpr size_t MaxPayloadForCallback() {
        using Tentative =
            details::CallbackWithStorage<Callable, std::array<std::byte, buffers_size - sizeof(Callable)>>;
        constexpr auto padding = sizeof(Tentative) - buffers_size;
        constexpr auto payload_size = buffers_size - sizeof(Callable) - padding;
        using Final = details::CallbackWithStorage<Callable, std::array<std::byte, payload_size>>;
        static_assert(sizeof(Final) == buffers_size);
        return payload_size;
    }

    template <class T>
    static std::pmr::polymorphic_allocator<T> get_allocator() {
        return std::pmr::polymorphic_allocator<T>{&get_pool()};
    }

    explicit IoUring(int queue_depth = queue_depth_default, int registered_buffers = buffers_count_default);
    IoUring(IoUring&& source) noexcept;
    IoUring& operator=(IoUring&& source) noexcept;
    IoUring(const IoUring& source) = delete;
    IoUring& operator=(const IoUring& source) = delete;
    ~IoUring();

    // Careful, path must remain alive until the ring is submitted.
    void queue_statx(int dir_fd, std::string_view path, struct statx* result, std::unique_ptr<CallbackErased> callback);

    // Careful, path must remain alive until the ring is submitted.
    void queue_statx(
        int dir_fd, std::string_view path, std::unique_ptr<CallbackWithStorageAbstract<struct statx>> callback
    );

    void add_fd(int fd, std::unique_ptr<CallbackErased> callback);

    void accept(int socket, std::unique_ptr<CallbackErased> callback);

    void accept_fixed(int socket, std::unique_ptr<CallbackErased> callback);

    void read(int fd, std::span<std::byte> target, size_t offset, std::unique_ptr<CallbackErased> callback);

    template <typename Storage>
    void read_fixed(
        int fd, std::span<std::byte> target, size_t offset,
        std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback
    );

    template <typename Callable>
    void read_fixed(int fd, size_t offset, Callable&& callable);

    void write(int fd, std::span<std::byte> source, std::unique_ptr<CallbackErased> callback);

    // For registered buffer: source must be in callback
    template <typename Storage>
    void write_fixed(
        int fd, std::span<std::byte> source, std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback
    );

    template <typename Storage>
    void write_fixed(int fd, std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback);

    template <size_t size>
    void write_vector(int fd, std::span<const iovec, size> sources, std::unique_ptr<CallbackErased> callback);

    unsigned queue_wait(
        int min_batch_size = wait_min_batch_size_default, std::chrono::nanoseconds wait_timeout = wait_timeout_default
    );

    void register_ring();
    void register_sparse_files(int count);
    void register_sparse_buffers(int count);
    void assign_buffer(int idx, std::span<const std::byte> buffer);
    void assign_file(int idx, int file);

    template <typename Storage, typename Callable, typename... Ts>
    [[nodiscard]] std::unique_ptr<CallbackWithStorageAbstract<Storage>> get_callback(
        Callable&& callable, Ts&&... storage_args
    ) {
        auto* ptr = get_allocator<details::CallbackWithStorage<Callable, Storage>>()
                        .template new_object<details::CallbackWithStorage<Callable, Storage>>(
                            std::forward<Callable>(callable), std::forward<Ts>(storage_args)...
                        );
        assert(ptr);
        return std::unique_ptr<CallbackWithStorageAbstract<Storage>>{ptr};
    }

    template <typename Callable>
    [[nodiscard]] std::unique_ptr<CallbackErased> get_callback(Callable&& callable) {
        auto* ptr =
            get_allocator<details::CallbackEmpty<Callable>>().template new_object<details::CallbackEmpty<Callable>>(
                std::forward<Callable>(callable)
            );
        assert(ptr);
        return std::unique_ptr<CallbackErased>{ptr};
    }

    template <typename Storage, typename Callable>
    [[nodiscard]] std::unique_ptr<CallbackWithStorageAbstract<Storage>> get_callback(
        Callable&& callable, std::unique_ptr<CallbackWithStorageAbstract<Storage>> storage
    ) {
        static_assert(
            !std::is_base_of_v<details::CallbackWithAttachedStorageInterface, decltype(storage)>,
            "Only one level of chaining is supported for now"
        );
        auto* ptr = get_allocator<details::CallbackWithAttachedStorage<Callable, Storage>>()
                        .template new_object<details::CallbackWithAttachedStorage<Callable, Storage>>(
                            std::forward<Callable>(callable), std::move(storage)
                        );
        assert(ptr);
        return std::unique_ptr<CallbackWithStorageAbstract<Storage>>{ptr};
    }

    template <typename Storage>
    [[nodiscard]] std::unique_ptr<CallbackWithStorageAbstract<Storage>> get_callback(
        std::unique_ptr<CallbackWithStorageAbstract<Storage>> storage
    ) {
        static_assert(
            !std::is_base_of_v<details::CallbackWithAttachedStorageInterface, decltype(storage)>,
            "Only one level of chaining is supported for now"
        );
        assert(storage);
        auto* ptr = get_allocator<details::CallbackWithAttachedStorageNop<Storage>>()
                        .template new_object<details::CallbackWithAttachedStorageNop<Storage>>(std::move(storage));
        assert(ptr);
        return std::unique_ptr<CallbackWithStorageAbstract<Storage>>{ptr};
    }

   private:
    io_uring_sqe* get_sqe(std::unique_ptr<CallbackErased> callable);

    template <typename Storage>
    static short get_index(const CallbackWithStorageAbstract<Storage>& callback) {
        return get_pool().get_index(&callback.get_storage());
    }

    io_uring ring{};
    std::forward_list<std::unique_ptr<CallbackErased>> to_clean_on_submit;
};

template <typename Storage>
void IoUring::read_fixed(
    int fd, std::span<std::byte> target, size_t offset, std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback
) {
    assert(fd >= 0);
    assert(callback);

    // For registered buffers: target must be in callback
    [[maybe_unused]] auto storage = singular_bytes(callback->get_storage());
    assert(std::ranges::search(storage, target).begin() != storage.end());

    auto index = callback->get_index();
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read_fixed(sqe, fd, target.data(), target.size(), offset, index);
}

template <typename Callable>
void IoUring::read_fixed(int fd, size_t offset, Callable&& callable) {
    assert(fd >= 0);
    auto callback =
        get_callback<std::array<std::byte, MaxPayloadForCallback<Callable>()>>(std::forward<Callable>(callable));
    auto index = callback->get_index();
    auto view = singular_bytes(callback->get_storage());
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read_fixed(sqe, fd, view.data(), view.size(), offset, index);
}

// For registered buffer: source must be in callback
template <typename Storage>
void IoUring::write_fixed(
    int fd, std::span<std::byte> source, std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback
) {
    assert(fd >= 0);
    assert(callback);

    [[maybe_unused]] auto storage = singular_bytes(callback->get_storage());
    assert(std::ranges::search(storage, source).begin() != storage.end());

    auto index = callback->get_index();
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write_fixed(sqe, fd, source.data(), source.size(), 0, index);
}

template <typename Storage>
void IoUring::write_fixed(int fd, std::unique_ptr<CallbackWithStorageAbstract<Storage>> callback) {
    assert(fd >= 0);
    assert(callback);
    auto view = singular_bytes(callback->get_storage());
    auto index = callback->get_index();
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write_fixed(sqe, fd, view.data(), view.size(), 0, index);
}

template <size_t size>
void IoUring::write_vector(int fd, std::span<const iovec, size> sources, std::unique_ptr<CallbackErased> callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_writev(sqe, fd, sources.data(), sources.size(), 0);
}

}  // namespace remotefs

#endif  // REMOTE_FS_IOURING_H
