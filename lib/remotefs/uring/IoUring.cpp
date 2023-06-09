#include "remotefs/uring/IoUring.h"

#include <cassert>
#include <iostream>

thread_local remotefs::CachedRegisteredBuffersResource<remotefs::buffers_size> pool_per_thread{};

namespace remotefs {

IoUring::IoUring(int queue_depth, int registered_buffers)
    : registered_buffers{registered_buffers} {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }

    // TODO: else
    if (registered_buffers > 0) {
        register_sparse_buffers(registered_buffers);
    }
}

void IoUring::start() {
    pool_per_thread = remotefs::CachedRegisteredBuffersResource<remotefs::buffers_size>{registered_buffers};
    for (auto&& [idx, buffer] : pool_per_thread.view()) {
        assign_buffer(idx, buffer);
    }
}

IoUring::~IoUring() {
    if (ring.ring_fd != 0) {
        io_uring_queue_exit(&ring);
    }
}

IoUring::IoUring(IoUring&& source) noexcept {
    assert(this != &source);
    ring = source.ring;
    source.ring = {};
    to_clean_on_submit = std::move(source.to_clean_on_submit);
    registered_buffers = source.registered_buffers;
}

IoUring& IoUring::operator=(IoUring&& source) noexcept {
    assert(this != &source);
    ring = source.ring;
    source.ring = {};
    to_clean_on_submit = std::move(source.to_clean_on_submit);
    registered_buffers = source.registered_buffers;
    return *this;
}

unsigned IoUring::queue_wait(int min_batch_size, std::chrono::nanoseconds wait_timeout) {
    assert(min_batch_size <= max_wait_min_batch_size);
    auto cqes = std::array<io_uring_cqe*, max_wait_min_batch_size>{};
    auto seconds = floor<std::chrono::seconds>(wait_timeout);
    auto timeout = __kernel_timespec{seconds.count(), (wait_timeout - seconds).count()};
    if (int ret = io_uring_submit_and_wait_timeout(&ring, cqes.data(), min_batch_size, &timeout, nullptr);
        ret == -ETIME || ret == -EINTR) [[unlikely]] {
        return 0;
    } else if (ret < 0) [[unlikely]] {
        throw std::system_error(-ret, std::generic_category(), "io_uring_submit_and_wait_timeout failed");
    }

    assert(!io_uring_cq_has_overflow(&ring));
    to_clean_on_submit.clear();

    unsigned completed = 0;
    unsigned head;
    struct io_uring_cqe* cqe;
    io_uring_for_each_cqe(&ring, head, cqe) {
        assert(cqe);
        ++completed;
        auto callback = static_cast<CallbackErased*>(io_uring_cqe_get_data(cqe));

        // IORING_CQE_F_NOTIF is used by zero copy requests to indicate the buffer can now be freed. See io_uring_enter.
        // For these requests, the initial callback shouldn't destroy or modify the buffer.
        // Zero copy is not compatible with SCTP and is therefore not used for the moment.
        assert(!(cqe->flags & IORING_CQE_F_NOTIF));
        if (callback != nullptr) {
            if (cqe->flags & IORING_CQE_F_MORE) {
                // There will be more data associated to this SQE, the callback should not be freed.
                (*callback)(cqe->res);
            } else {
                // Move the callback to the user (if he wants it). In all cases, it will be freed after by the ptr.
                static_assert(std::unique_ptr<CallbackErased>::deleter_type::is_proper_deleter);
                (*callback)(cqe->res, std::unique_ptr<CallbackErased>{callback});
            }
        }
    }
    io_uring_cq_advance(&ring, completed);
    return completed;
}

void IoUring::register_ring() {
    if (auto ret = io_uring_register_ring_fd(&ring); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register queue fd");
    }
}

void IoUring::register_sparse_buffers(int count) {
    if (auto ret = io_uring_register_buffers_sparse(&ring, count); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register buffers");
    }
}

void IoUring::assign_buffer(int idx, std::span<const std::byte> buffer) {
    auto buffer_descriptor = iovec{.iov_base = const_cast<std::byte*>(buffer.data()), .iov_len = buffer.size()};

    if (auto ret = io_uring_register_buffers_update_tag(&ring, idx, &buffer_descriptor, nullptr, 1); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to update a registered buffer");
    }
}

void IoUring::register_sparse_files(int count) {
    if (auto ret = io_uring_register_files_sparse(&ring, count); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register files");
    }
}

void IoUring::assign_file(int idx, int file) {
    if (auto ret = io_uring_register_files_update(&ring, idx, &file, 1); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to update a registered file");
    }
}

CachedRegisteredBuffersResource<buffers_size>& get_pool() {
    return pool_per_thread;
}

io_uring_sqe* IoUring::get_sqe(std::unique_ptr<CallbackErased> callable) {
    static_assert(decltype(callable)::deleter_type::is_proper_deleter);
    assert(callable);

    auto callable_ptr = callable.release();

    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        if (dynamic_cast<details::CallbackNop*>(callable_ptr)) {
            to_clean_on_submit.push_front(std::unique_ptr<CallbackErased>{callable_ptr});
            // Explicitly setting nullptr is not necessary
            io_uring_sqe_set_data(sqe, nullptr);
        } else {
            io_uring_sqe_set_data(sqe, callable_ptr);
        }
        return sqe;
    }

    // TODO: Metric/log
    io_uring_submit(&ring);

    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) [[likely]] {
        if (dynamic_cast<details::CallbackNop*>(callable_ptr)) {
            to_clean_on_submit.push_front(std::unique_ptr<CallbackErased>{callable_ptr});
            io_uring_sqe_set_data(sqe, nullptr);
        } else {
            io_uring_sqe_set_data(sqe, callable_ptr);
        }
        return sqe;
    }

    throw std::runtime_error("Failed to get an SQE from the ring");
}

void IoUring::queue_statx(
    int dir_fd, std::string_view path, struct statx* result, std::unique_ptr<CallbackErased> callback
) {
    assert(dir_fd >= 0 || dir_fd == AT_FDCWD);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_statx(sqe, dir_fd, path.data(), 0, STATX_BASIC_STATS, result);
}

void IoUring::queue_statx(
    int dir_fd, std::string_view path, std::unique_ptr<CallbackWithStorageAbstract<struct statx>> callback
) {
    auto* result = &callback->get_storage();
    queue_statx(dir_fd, path, result, std::move(callback));
}

// CallbackUniquePtr<Callable> when no result (because the lambda is already there to store something)
void IoUring::add_fd(int fd, std::unique_ptr<CallbackErased> callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_poll_multishot(sqe, fd, POLLIN);
}

void IoUring::accept(int socket, std::unique_ptr<CallbackErased> callback) {
    assert(socket >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_multishot_accept(sqe, socket, nullptr, nullptr, 0);
}

void IoUring::accept_fixed(int socket, std::unique_ptr<CallbackErased> callback) {
    assert(socket >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    // TODO: Multishot requests should ask for a different kind of object. Should still take ownership of the pointer.
    io_uring_prep_multishot_accept_direct(sqe, socket, nullptr, nullptr, 0);
}

void IoUring::read(int fd, std::span<std::byte> target, size_t offset, std::unique_ptr<CallbackErased> callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_read(sqe, fd, target.data(), target.size(), offset);
}

void IoUring::write(int fd, std::span<std::byte> source, std::unique_ptr<CallbackErased> callback) {
    assert(fd >= 0);
    assert(callback);
    auto* sqe = get_sqe(std::move(callback));
    io_uring_prep_write(sqe, fd, source.data(), source.size(), 0);
}

}  // namespace remotefs
