#include "IoUring.h"

namespace remotefs {

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
IoUring::IoUring() {
    if (auto ret = io_uring_queue_init(queue_depth, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "Queue initialization");
    }
}
#pragma clang diagnostic pop

IoUring::~IoUring() {
    io_uring_queue_exit(&ring);
}

void IoUring::queue_read(int fd, std::span<char> buffer, std::size_t offset, void* user_data) {
    if (auto* sqe = io_uring_get_sqe(&ring); sqe != nullptr) {
        io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), offset);
        io_uring_sqe_set_data(sqe, user_data);
    }
}

std::pair<size_t, void*> IoUring::queue_peak() {
    struct io_uring_cqe* cqe;
    if (auto ret = io_uring_peek_cqe(&ring, &cqe); ret == 0) {
        if (cqe->res > 0) {
            io_uring_cqe_seen(&ring, cqe);
            return {cqe->res, io_uring_cqe_get_data(cqe)};
        } else if (cqe->res == -EAGAIN) {
            io_uring_cqe_seen(&ring, cqe);
        } else {
            throw std::system_error(-ret, std::generic_category(), "Read result");
        }
    } else if (ret == -EAGAIN) {
    } else {
        throw std::system_error(-ret, std::generic_category(), "Queue peek");
    }

    return {0, nullptr};
}

void IoUring::submit() {
    io_uring_submit(&ring);
}

int IoUring::fd() const {
    return ring.ring_fd;
}

}  // namespace remotefs
