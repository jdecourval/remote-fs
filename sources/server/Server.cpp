#include "Server.h"

#include <quill/Quill.h>

#include <memory>
#include <optional>

#include "remotefs/tools/FuseOp.h"
#include "remotefs/tools/NarrowCast.h"

namespace {
thread_local remotefs::IoUring* io_uring_static_thread_ptr = nullptr;
thread_local unsigned long long active_registered_buffers = 0;  // bitmap of used buffers.

}  // namespace

namespace remotefs {
extern "C" {
static volatile auto log_requested = false;
static volatile auto stop_requested = false;

void signal_usr1_handler(int signal) {
    assert(signal == SIGUSR1);
    (void)signal;
    log_requested = true;
}

void signal_term_handler(int signal) {
    assert(signal == SIGTERM);
    (void)signal;
    stop_requested = true;
}
}

Server::Server(const std::string& address, int port, const Socket::Options& socket_options, bool metrics_on_stop,
               int ring_depth, int max_registered_buffers)
    : socket{},
      logger{quill::get_logger()},
      metric_registry{},
      io_uring{ring_depth},
      syscalls{io_uring},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
    std::signal(SIGPIPE, SIG_IGN);

    if (max_registered_buffers > 0) {
        io_uring.register_sparse_buffers(max_registered_buffers);
    }

    const auto available_bits = std::numeric_limits<decltype(active_registered_buffers)>::digits;
    assert(max_registered_buffers <= available_bits);
    active_registered_buffers = ~decltype(active_registered_buffers){} >> (available_bits - max_registered_buffers);

    LOG_INFO(logger, "Binding to {}", address);
    socket = remotefs::Socket::listen(address, port, socket_options);
}

void Server::accept_callback(int syscall_ret, int pipeline) {
    if (syscall_ret >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < pipeline; i++) {
            auto buffer = new_registered_buffer();
            auto buffer_view = buffer->view();
            io_uring.read(
                syscall_ret, buffer_view, 0,
                [this, client_socket = Socket{syscall_ret}, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                    read_callback(syscall_ret, std::move(client_socket), std::move(buffer));
                });
        }
    } else {
        LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-syscall_ret));
    }
}

void Server::read_callback(int syscall_ret, Socket&& client_socket, std::unique_ptr<RegisteredBuffer> buffer) {
    auto buffer_view = buffer->view();
    auto buffer_index = buffer->get_index();

    if (syscall_ret < 0) [[unlikely]] {
        if (syscall_ret == -ECONNRESET) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        auto client_socket_int = static_cast<int>(client_socket);
        io_uring.read_fixed(
            client_socket_int, buffer_view, buffer_index, 0,
            [this, client_socket = std::move(client_socket), buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                read_callback(syscall_ret, std::move(client_socket), std::move(buffer));
            });
        return;
    }

    if (syscall_ret == 0) [[unlikely]] {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        return;
    }

    LOG_TRACE_L1(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(buffer->view()[0]));
    switch (buffer->view()[0]) {
        case 1: {
            syscalls.open(*reinterpret_cast<messages::requests::Open*>(buffer->view().data()), client_socket);
            break;
        }
        case 2: {
            syscalls.lookup(*reinterpret_cast<messages::requests::Lookup*>(buffer->view().data()), client_socket);
            break;
        }
        case 3: {
            syscalls.getattr(*reinterpret_cast<messages::requests::GetAttr*>(buffer->view().data()), client_socket);
            break;
        }
        case 4: {
            syscalls.readdir(*reinterpret_cast<messages::requests::ReadDir*>(buffer->view().data()), client_socket);
            break;
        }
        case 5:
            syscalls.read(*reinterpret_cast<messages::requests::Read*>(buffer->view().data()), client_socket);
            break;
        case 6:
            syscalls.release(*reinterpret_cast<messages::requests::Release*>(buffer->view().data()));
            break;
        case 7: {
            auto& message = *reinterpret_cast<messages::both::Ping*>(buffer->view().data());
            //    message.middle = std::chrono::high_resolution_clock::now();
            write(std::move(client_socket), std::move(buffer), [](int ret, auto&&) {
                if (ret == -EPIPE) [[unlikely]] {
                    LOG_INFO(quill::get_logger(), "SIGPIPE, closing socket");
                } else if (ret < 0) [[unlikely]] {
                    throw std::system_error(-ret, std::system_category(), "Failed to write to socket");
                }
            });
            break;
        }
        default:
            assert(false);
    }
    if (!buffer) {
        buffer = new_registered_buffer();
        buffer_view = buffer->view();
    }
    auto client_socket_int = static_cast<int>(client_socket);
    io_uring.read(
        client_socket_int, buffer_view, 0,
        [this, client_socket = std::move(client_socket), buffer = std::move(buffer)](int32_t syscall_ret) mutable {
            read_callback(syscall_ret, std::move(client_socket), std::move(buffer));
        });
}

void Server::start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring) {
    std::signal(SIGPIPE, SIG_IGN);
    assert(io_uring_static_thread_ptr == nullptr);
    io_uring_static_thread_ptr = &io_uring;

    if (register_ring) {
        io_uring.register_ring();
    }

    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_timer("message-received");
    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    auto& open_timing = metric_registry.create_histogram("message-received-open");
    auto& read_timing = metric_registry.create_histogram("message-received-read");
    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    auto& release_timing = metric_registry.create_histogram("message-received-release");
    auto& send_timing = metric_registry.create_histogram("message-sent");

    io_uring.accept(socket, [this, pipeline](int32_t syscall_ret) { accept_callback(syscall_ret, pipeline); });

    while (!stop_requested) [[likely]] {
            {
                auto tracker = wait_time.track_scope();
                io_uring.queue_wait(min_batch_size, wait_timeout);
            }

            if (log_requested) [[unlikely]] {
                log_requested = false;
                std::cerr << metric_registry << std::flush;
            }
        }

    if (_metrics_on_stop) {
        std::cerr << metric_registry << std::flush;
    }
}

std::unique_ptr<Server::RegisteredBuffer> Server::new_registered_buffer() {
    auto index = std::countr_zero(active_registered_buffers);  // 0-indexed

    if (index < 0) {
        // no more index available
        return std::make_unique<Server::RegisteredBuffer>(index);
    }
    active_registered_buffers ^= 0b1ull << index;

    auto buffer = std::make_unique<Server::RegisteredBuffer>(index);
    io_uring.assign_buffer(narrow_cast<int>(index), buffer->view());
    return buffer;
}

template <typename Callable>
void Server::write(Socket&& client_socket, int offset,
                   std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>> buffer, Callable&& callback) {
    auto buffer_view = std::span{reinterpret_cast<char*>(buffer.get()), buffer->size()};

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) {
        return callback(ret, std::move(buffer));
    };

    io_uring.write(static_cast<int>(client_socket), buffer_view, offset, std::move(callback_with_buffer));
}

template <typename Callable>
void Server::write(Socket&& client_socket, std::unique_ptr<RegisteredBuffer> buffer, Callable&& callback) {
    auto buffer_view = buffer->view();
    auto buffer_index = buffer->get_index();

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) {
        return callback(ret, std::move(buffer));
    };

    if (buffer_index > 0) {
        io_uring.write_fixed(static_cast<int>(client_socket), buffer_view, buffer_index,
                             std::move(callback_with_buffer));
    } else {
        io_uring.write(static_cast<int>(client_socket), buffer_view, std::move(callback_with_buffer));
    }
}

template <typename Callable>
void Server::read(Socket&& client_socket, int offset,
                  std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>> buffer, Callable&& callback) {
    auto buffer_view = std::span{reinterpret_cast<char*>(buffer.get()), buffer->size()};

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) {
        return callback(ret, std::move(buffer));
    };

    io_uring.read(static_cast<int>(client_socket), buffer_view, offset, std::move(callback_with_buffer));
}

template <typename Callable>
void Server::read(Socket&& client_socket, int offset, std::unique_ptr<RegisteredBuffer> buffer, Callable&& callback) {
    auto buffer_view = buffer->view();
    auto buffer_index = buffer->get_index();

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) {
        return callback(ret, std::move(buffer));
    };

    if (buffer_index > 0) {
        io_uring.read_fixed(static_cast<int>(client_socket), buffer_view, buffer_index, offset,
                            std::move(callback_with_buffer));
    } else {
        io_uring.read(static_cast<int>(client_socket), buffer_view, offset, std::move(callback_with_buffer));
    }
}

Server::RegisteredBuffer::~RegisteredBuffer() {
    active_registered_buffers ^= 0b1ull << index;
}

std::span<char> Server::RegisteredBuffer::view() {
    return std::span{storage};
}

std::span<const char> Server::RegisteredBuffer::view() const {
    return std::span{storage};
}

int Server::RegisteredBuffer::get_index() const {
    return index;
}
}  // namespace remotefs
