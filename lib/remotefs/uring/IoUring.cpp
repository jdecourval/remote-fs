#include "remotefs/uring/IoUring.h"

#include <cassert>
#include <iostream>

remotefs::IoUring::IoUring(int queue_depth) {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
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

void remotefs::IoUring::queue_wait(int min_batch_size, std::chrono::nanoseconds wait_timeout) {
    assert(min_batch_size <= max_wait_min_batch_size);
    auto cqes = std::array<io_uring_cqe*, max_wait_min_batch_size>{};
    auto seconds = floor<std::chrono::seconds>(wait_timeout);
    auto timeout = __kernel_timespec{seconds.count(), (wait_timeout - seconds).count()};
    if (int ret = io_uring_submit_and_wait_timeout(&ring, cqes.data(), min_batch_size, &timeout, nullptr);
        ret == -ETIME || ret == -EINTR) [[unlikely]] {
        return;
    } else if (ret < 0) [[unlikely]] {
        throw std::system_error(-ret, std::generic_category(), "io_uring_submit_and_wait_timeout failed");
    }

    unsigned completed = 0;
    unsigned head;
    struct io_uring_cqe* cqe;
    io_uring_for_each_cqe(&ring, head, cqe) {
        assert(cqe);
        ++completed;
        auto callback = reinterpret_cast<CallbackErased*>(io_uring_cqe_get_data(cqe));
        if (callback != nullptr) {
            (*callback)(cqe->res);
        } else {
            std::cout << "nullptr, flags:" << cqe->flags << ", res:" << cqe->res << std::endl;
        }
        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            delete callback;
        }
    }
    io_uring_cq_advance(&ring, completed);
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

void remotefs::IoUring::assign_buffer(int idx, std::span<std::byte> buffer) {
    auto buffer_descriptor = iovec{.iov_base = buffer.data(), .iov_len = buffer.size()};

    if (auto ret = io_uring_register_buffers_update_tag(&ring, idx, &buffer_descriptor, nullptr, 1); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Failed to update a registered buffer");
    }
}
