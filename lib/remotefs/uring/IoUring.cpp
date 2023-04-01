#include "remotefs/uring/IoUring.h"

#include <cassert>
#include <iostream>

namespace {
std::unique_ptr<remotefs::IoUring::MemoryResource> pool_static = nullptr;
}

remotefs::IoUring::IoUring(int queue_depth, short registered_buffers) {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }

    // TODO: else
    if (registered_buffers > 0) {
        register_sparse_buffers(registered_buffers);
    }

    pool_static = std::make_unique<MemoryResource>(registered_buffers);
    for (auto&& [idx, buffer] : pool_static->view()) {
        assign_buffer(idx, buffer);
    }
}

remotefs::IoUring::~IoUring() {
    if (ring.ring_fd != 0) {
        io_uring_queue_exit(&ring);
    }
}

remotefs::IoUring::IoUring(remotefs::IoUring&& source) noexcept {
    std::swap(ring, source.ring);
}

remotefs::IoUring& remotefs::IoUring::operator=(remotefs::IoUring source) noexcept {
    std::swap(ring, source.ring);
    return *this;
}

unsigned remotefs::IoUring::queue_wait(int min_batch_size, std::chrono::nanoseconds wait_timeout) {
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
        if (callback != nullptr) [[likely]] {
            if (cqe->flags & IORING_CQE_F_MORE) {
                (*callback)(cqe->res);
            } else {
                (*callback)(cqe->res, CallbackErasedUniquePtr{callback});
            }
        }
    }
    io_uring_cq_advance(&ring, completed);
    return completed;
}

void remotefs::IoUring::register_ring() {
    if (auto ret = io_uring_register_ring_fd(&ring); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register queue fd");
    }
}

void remotefs::IoUring::register_sparse_buffers(int count) {
    if (auto ret = io_uring_register_buffers_sparse(&ring, count); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register buffers");
    }
}

void remotefs::IoUring::assign_buffer(int idx, std::span<const std::byte> buffer) {
    auto buffer_descriptor = iovec{.iov_base = const_cast<std::byte*>(buffer.data()), .iov_len = buffer.size()};

    if (auto ret = io_uring_register_buffers_update_tag(&ring, idx, &buffer_descriptor, nullptr, 1); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to update a registered buffer");
    }
}

void remotefs::IoUring::register_sparse_files(int count) {
    if (auto ret = io_uring_register_files_sparse(&ring, count); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to register files");
    }
}

void remotefs::IoUring::assign_file(int idx, int file) {
    if (auto ret = io_uring_register_files_update(&ring, idx, &file, 1); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to update a registered file");
    }
}

remotefs::IoUring::MemoryResource& remotefs::IoUring::get_pool() {
    assert(pool_static);
    return *pool_static;
}
