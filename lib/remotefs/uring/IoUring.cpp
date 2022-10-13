#include "remotefs/uring/IoUring.h"

#include <cassert>
#include <iostream>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
remotefs::IoUring::IoUring(bool register_ring_fd) {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }

    if (register_ring_fd) {
        if (auto ret = io_uring_register_ring_fd(&ring); ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "Failed to register queue fd");
        }
    }
}
#pragma clang diagnostic pop

remotefs::IoUring::~IoUring() {
    if (ring.ring_fd != 0) {
        io_uring_queue_exit(&ring);
    }
}

remotefs::IoUring::IoUring(remotefs::IoUring&& source) noexcept {
    std::swap(ring, source.ring);
}

remotefs::IoUring& remotefs::IoUring::operator=(remotefs::IoUring&& source) noexcept {
    std::swap(ring, source.ring);
    return *this;
}

void remotefs::IoUring::queue_wait() {
    auto cqes = std::array<io_uring_cqe*, wait_min_batch_size>{};
    auto timeout = __kernel_timespec{wait_timeout_s, wait_timeout_ns};
    if (int ret = io_uring_submit_and_wait_timeout(&ring, cqes.data(), wait_min_batch_size, &timeout, nullptr);
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
        (*callback)(cqe->res);
        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            delete callback;
        }
    }
    io_uring_cq_advance(&ring, completed);
}

void remotefs::IoUring::register_buffer(std::span<char> buffer) {
    auto buffer_descriptor = iovec{.iov_base = buffer.data(), .iov_len = buffer.size()};

    io_uring_register_buffers(&ring, &buffer_descriptor, 1);
}
