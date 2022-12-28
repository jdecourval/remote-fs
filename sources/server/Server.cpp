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
               int ring_depth, int max_registered_buffers, int cached_buffers)
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

    // For now, cached buffers have to be registered because the bitmap is used for both features.
    assert(cached_buffers <= max_registered_buffers);
    buffers_cache.resize(cached_buffers);

    const auto available_bits = std::numeric_limits<decltype(active_registered_buffers)>::digits;
    assert(max_registered_buffers <= available_bits);
    active_registered_buffers = ~decltype(active_registered_buffers){} >> (available_bits - max_registered_buffers);

    LOG_INFO(logger, "Binding to {}", address);
    socket = remotefs::Socket::listen(address, port, socket_options);
}

Server::RegisteredBuffer Server::new_registered_buffer() {
    auto index = std::countr_zero(active_registered_buffers);  // 0-indexed

    if (index >= std::numeric_limits<decltype(active_registered_buffers)>::digits) {
        // no more index available
        LOG_TRACE_L1(logger, "No more index");
        return RegisteredBuffer(index);
    }
    active_registered_buffers ^= 0b1ull << index;

    auto buffer = [&] {
        if (std::ssize(buffers_cache) > index) {
            if (auto&& buffer = buffers_cache[index]) {
                LOG_TRACE_L1(logger, "Found in cache");
                return buffer.non_owning_copy();
            } else {
                LOG_TRACE_L1(logger, "Initializing new cache entry");
                return (buffers_cache[index] = RegisteredBuffer(index)).non_owning_copy();
            }
        } else {
            LOG_TRACE_L1(logger, "Creating untracked buffer");
            return RegisteredBuffer(index);
        }
    }();

    io_uring.assign_buffer(narrow_cast<int>(index), buffer.view());
    return buffer;
}

void Server::accept_callback(int syscall_ret, int pipeline) {
    if (syscall_ret >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < pipeline; i++) {
            auto buffer = new_registered_buffer();
            read(syscall_ret, 0, std::move(buffer),
                 [this, client_socket = Socket{syscall_ret}](int32_t syscall_ret, auto&& i) mutable {
                     read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(i)>(i));
                 });
        }
    } else {
        LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-syscall_ret));
    }
}

void Server::read_callback(int syscall_ret, Socket&& client_socket, RegisteredBuffer&& buffer) {
    auto client_socket_int = static_cast<int>(client_socket);

    if (syscall_ret < 0) [[unlikely]] {
        if (syscall_ret == -ECONNRESET) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        read(client_socket_int, 0, std::move(buffer),
             [this, client_socket = std::move(client_socket)](int32_t syscall_ret, auto&& i) mutable {
                 read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(i)>(i));
             });
        return;
    }

    if (syscall_ret == 0) [[unlikely]] {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        return;
    }

    LOG_TRACE_L1(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(buffer.view()[0]));
    switch (buffer.view()[0]) {
        case messages::requests::Open().tag: {
            syscalls.open(*reinterpret_cast<messages::requests::Open*>(buffer.view().data()), client_socket);
            break;
        }
        case messages::requests::Lookup().tag: {
            syscalls.lookup(*reinterpret_cast<messages::requests::Lookup*>(buffer.view().data()), client_socket);
            break;
        }
        case messages::requests::GetAttr().tag: {
            syscalls.getattr(*reinterpret_cast<messages::requests::GetAttr*>(buffer.view().data()), client_socket);
            break;
        }
        case messages::requests::ReadDir().tag: {
            syscalls.readdir(*reinterpret_cast<messages::requests::ReadDir*>(buffer.view().data()), client_socket);
            break;
        }
        case messages::requests::Read().tag:
            syscalls.read(*reinterpret_cast<messages::requests::Read*>(buffer.view().data()), client_socket);
            break;
        case messages::requests::Release().tag:
            syscalls.release(*reinterpret_cast<messages::requests::Release*>(buffer.view().data()));
            break;
        case std::byte{7}: {
            //            auto& message = *reinterpret_cast<messages::both::Ping*>(buffer.view().data());
            //    message.middle = std::chrono::high_resolution_clock::now();
            write(client_socket_int, std::move(buffer), [](int ret, auto&&) {
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
    if (!buffer) {  // NOLINT(bugprone-use-after-move)
        buffer = new_registered_buffer();
    }

    read(client_socket_int, 0, std::move(buffer),
         [this, client_socket = std::move(client_socket)](int32_t syscall_ret, auto&& buffer) mutable {
             read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(buffer)>(buffer));
         });
}

void Server::start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring) {
    std::signal(SIGPIPE, SIG_IGN);
    assert(io_uring_static_thread_ptr == nullptr);
    io_uring_static_thread_ptr = &io_uring;

    if (register_ring) {
        io_uring.register_ring();
    }

    //    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_timer("message-received");
    //    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    //    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    //    auto& open_timing = metric_registry.create_histogram("message-received-open");
    //    auto& read_timing = metric_registry.create_histogram("message-received-read");
    //    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    //    auto& release_timing = metric_registry.create_histogram("message-received-release");
    //    auto& send_timing = metric_registry.create_histogram("message-sent");

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

template <typename Callable>
void Server::write(int client_socket, RegisteredBuffer&& buffer, Callable&& callback) {
    auto buffer_view = buffer.view();
    auto buffer_index = buffer.get_index();

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) {
        return callback(ret, std::move(buffer));
    };

    if (buffer_index > 0) {
        io_uring.write_fixed(client_socket, buffer_view, buffer_index, std::move(callback_with_buffer));
    } else {
        io_uring.write(client_socket, buffer_view, std::move(callback_with_buffer));
    }
}

template <typename Callable>
void Server::read(int client_socket, int offset, RegisteredBuffer&& buffer, Callable&& callback) {
    auto buffer_view = buffer.view();
    auto buffer_index = buffer.get_index();

    auto callback_with_buffer = [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](
                                    int ret) mutable { return callback(ret, std::move(buffer)); };

    if (buffer_index > 0) {
        io_uring.read_fixed(client_socket, buffer_view, buffer_index, offset, std::move(callback_with_buffer));
    } else {
        io_uring.read(client_socket, buffer_view, offset, std::move(callback_with_buffer));
    }
}

Server::RegisteredBuffer::~RegisteredBuffer() {
    if (is_registered()) {
        active_registered_buffers ^= 0b1ull << index;
    }
}

std::span<std::byte> Server::RegisteredBuffer::view() {
    return std::visit([](auto&& i) { return std::span{*i}; }, buffer);
}

std::span<const std::byte> Server::RegisteredBuffer::view() const {
    return std::visit([](auto&& i) { return std::span{*i}; }, buffer);
}

int Server::RegisteredBuffer::get_index() const {
    return index;
}

bool Server::RegisteredBuffer::is_registered() const {
    return index >= 0;
}

bool Server::RegisteredBuffer::is_owner() const {
    return std::holds_alternative<std::unique_ptr<BufferStorage>>(buffer);
}

Server::RegisteredBuffer Server::RegisteredBuffer::non_owning_copy() const {
    return RegisteredBuffer{std::visit([](auto&& i) { return std::to_address(i); }, buffer), index};
}

Server::RegisteredBuffer::operator bool() const {
    return std::visit([](auto&& i) { return std::to_address(i) != nullptr; }, buffer);
}
}  // namespace remotefs
