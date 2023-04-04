#include "Server.h"

#include <quill/Quill.h>

#include <memory>
#include <optional>

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

Server::Server(
    const std::string& address, int port, const Socket::Options& socket_options, bool metrics_on_stop, int ring_depth,
    int max_registered_buffers, int cached_buffers, int max_clients
)
    : socket{},
      logger{quill::get_logger()},
      metric_registry{},
      io_uring{ring_depth},
      syscalls{io_uring},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
    std::signal(SIGPIPE, SIG_IGN);

    io_uring.register_sparse_files(max_clients);

    LOG_INFO(logger, "Binding to {}", address);
    socket = remotefs::Socket::listen(address, port, socket_options);
}

void Server::accept_callback(int client_socket, int pipeline) {
    if (client_socket >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < pipeline; i++) {
            io_uring.read_fixed(
                client_socket, 0,
                [this, client_socket = Socket{client_socket}](int syscall_ret, auto callback) mutable {
                    read_callback(syscall_ret, std::move(client_socket), std::move(callback));
                }
            );
        }
    } else {
        LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-client_socket));
    }
}

template <auto MaxBufferSize>
void Server::read_callback(
    int syscall_ret, Socket client_socket,
    IoUring::CallbackWithStorageAbstractUniquePtr<std::array<std::byte, MaxBufferSize>> old_callback
) {
    auto client_socket_int = static_cast<int>(client_socket);

    if (syscall_ret < 0) [[unlikely]] {
        if (syscall_ret == -ECONNRESET || syscall_ret == -EPIPE || syscall_ret == -EBADF) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        io_uring.read_fixed(
            client_socket_int, 0,
            [this, client_socket = std::move(client_socket)](int syscall_ret, auto callback) mutable {
                read_callback(syscall_ret, std::move(client_socket), std::move(callback));
            }
        );
        return;
    }

    if (syscall_ret == 0) [[unlikely]] {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        return;
    }

    LOG_TRACE_L2(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(old_callback->get_storage()[0]));
    switch (old_callback->get_storage()[0]) {
        case messages::requests::Open().tag: {
            syscalls.open(
                *reinterpret_cast<messages::requests::Open*>(old_callback->get_storage().data()), client_socket
            );
            break;
        }
        case messages::requests::Lookup().tag: {
            syscalls.lookup(
                *reinterpret_cast<messages::requests::Lookup*>(old_callback->get_storage().data()), client_socket
            );
            break;
        }
        case messages::requests::GetAttr().tag: {
            syscalls.getattr(
                *reinterpret_cast<messages::requests::GetAttr*>(old_callback->get_storage().data()), client_socket
            );
            break;
        }
        case messages::requests::ReadDir().tag: {
            syscalls.readdir(
                *reinterpret_cast<messages::requests::ReadDir*>(old_callback->get_storage().data()), client_socket
            );
            break;
        }
        case messages::requests::Read().tag:
            syscalls.read(
                *reinterpret_cast<messages::requests::Read*>(old_callback->get_storage().data()), client_socket
            );
            break;
        case messages::requests::Release().tag:
            syscalls.release(*reinterpret_cast<messages::requests::Release*>(old_callback->get_storage().data()));
            break;
        case std::byte{7}: {
            auto view = std::span{old_callback->get_storage()}.subspan(0, syscall_ret);
            auto callback = io_uring.get_callback(
                [](int ret) {
                    if (ret == -EPIPE) [[unlikely]] {
                        LOG_INFO(quill::get_logger(), "SIGPIPE, closing socket");
                    } else if (ret < 0) [[unlikely]] {
                        throw std::system_error(-ret, std::system_category(), "Failed to write to socket");
                    }
                },
                std::move(old_callback)
            );
            io_uring.write_fixed(client_socket_int, view, std::move(callback));
            break;
        }
        default:
            assert(false);
    }

    // Release memory a bit sooner
    old_callback.reset();

    io_uring.read_fixed(
        client_socket_int, 0,
        [this, client_socket = std::move(client_socket)](int syscall_ret, auto callback) mutable {
            read_callback(syscall_ret, std::move(client_socket), std::move(callback));
        }
    );
}

void Server::start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring) {
    std::signal(SIGPIPE, SIG_IGN);

    if (register_ring) {
        io_uring.register_ring();
    }

    //    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_timer("message-received");
    auto& tasks_per_iteration = metric_registry.create_histogram_double("tasks-per-iteration");
    //    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    //    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    //    auto& open_timing = metric_registry.create_histogram("message-received-open");
    //    auto& read_timing = metric_registry.create_histogram("message-received-read");
    //    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    //    auto& release_timing = metric_registry.create_histogram("message-received-release");
    //    auto& send_timing = metric_registry.create_histogram("message-sent");

    auto callback =
        io_uring.get_callback([this, pipeline](int32_t syscall_ret) { accept_callback(syscall_ret, pipeline); });
    if (register_fd) {
        io_uring.accept_fixed(socket, std::move(callback));
    } else {
        io_uring.accept(socket, std::move(callback));
    }

    while (!stop_requested) [[likely]] {
        //        LOG_INFO(logger, "loop");
        {
            auto tracker = wait_time.track_scope();
            if (auto tasks_run = io_uring.queue_wait(min_batch_size, wait_timeout)) {
                tasks_per_iteration += tasks_run;
            }
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

}  // namespace remotefs
