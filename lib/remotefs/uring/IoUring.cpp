#include "remotefs/uring/IoUring.h"

#include <cassert>
#include <iostream>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
remotefs::IoUring::IoUring() {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }
}
#pragma clang diagnostic pop

remotefs::IoUring::~IoUring() {
    io_uring_queue_exit(&ring);
}

void remotefs::IoUring::queue_wait() {
    auto cqes = std::array<io_uring_cqe*, wait_min_batch_size>{};
    auto timeout = timespec{wait_timeout_s, wait_timeout_ns};
    if (int ret = io_uring_submit_and_wait_timeout(&ring, cqes.data(), wait_min_batch_size,
                                                   reinterpret_cast<__kernel_timespec*>(&timeout), nullptr);
        ret == -ETIME || ret == -EINTR) [[unlikely]] {
        return;
    } else if (ret < 0) [[unlikely]] {
        throw std::system_error(-ret, std::generic_category(), "io_uring_wait_cqes failed");
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
