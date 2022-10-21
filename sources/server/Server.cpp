#include "Server.h"

#include <quill/Quill.h>

#include <memory>
#include <optional>

#include "remotefs/tools/FuseOp.h"

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

Server::Server(bool metrics_on_stop)
    : socket{},
      logger{quill::get_logger()},
      metric_registry{},
      syscalls{io_uring},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
    std::signal(SIGPIPE, SIG_IGN);
}

void Server::accept_callback(int syscall_ret) {
    if (syscall_ret >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < 1; i++) {
            auto buffer = std::make_unique<std::array<char, settings::MAX_MESSAGE_SIZE>>();
            auto buffer_view = std::span{buffer->data(), buffer->size()};
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

void Server::read_callback(int syscall_ret, Socket&& client_socket,
                           std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer) {
    auto buffer_view = std::span{buffer->data(), buffer->size()};

    if (syscall_ret < 0) {
        if (syscall_ret == -ECONNRESET) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        auto client_socket_int = static_cast<int>(client_socket);
        io_uring.read(
            client_socket_int, buffer_view, 0,
            [this, client_socket = std::move(client_socket), buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                read_callback(syscall_ret, std::move(client_socket), std::move(buffer));
            });
        return;
    }

    if (syscall_ret == 0) {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        return;
    }

    LOG_TRACE_L1(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(buffer->at(0)));
    switch (buffer->at(0)) {
        case 1: {
            syscalls.open(*reinterpret_cast<messages::requests::Open*>(buffer->data()), client_socket);
            break;
        }
        case 2: {
            syscalls.lookup(*reinterpret_cast<messages::requests::Lookup*>(buffer->data()), client_socket);
            break;
        }
        case 3: {
            syscalls.getattr(*reinterpret_cast<messages::requests::GetAttr*>(buffer->data()), client_socket);
            break;
        }
        case 4: {
            syscalls.readdir(*reinterpret_cast<messages::requests::ReadDir*>(buffer->data()), client_socket);
            break;
        }
        case 5:
            syscalls.read(*reinterpret_cast<messages::requests::Read*>(buffer->data()), client_socket);
            break;
        case 6:
            syscalls.release(*reinterpret_cast<messages::requests::Release*>(buffer->data()));
            break;
        case 7:
            syscalls.ping(std::move(buffer), client_socket);
            break;
        default:
            assert(false);
    }
    if (!buffer) {
        buffer.reset(new std::array<char, settings::MAX_MESSAGE_SIZE>());
        buffer_view = std::span{buffer->data(), buffer->size()};
    }
    auto client_socket_int = static_cast<int>(client_socket);
    io_uring.read(
        client_socket_int, buffer_view, 0,
        [this, client_socket = std::move(client_socket), buffer = std::move(buffer)](int32_t syscall_ret) mutable {
            read_callback(syscall_ret, std::move(client_socket), std::move(buffer));
        });
}

void Server::start(const std::string& address) {
    LOG_INFO(logger, "Binding to {}", address);
    socket = remotefs::Socket::listen(address, 6512);

    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_timer("message-received");
    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    auto& open_timing = metric_registry.create_histogram("message-received-open");
    auto& read_timing = metric_registry.create_histogram("message-received-read");
    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    auto& release_timing = metric_registry.create_histogram("message-received-release");
    auto& send_timing = metric_registry.create_histogram("message-sent");

    io_uring.accept(socket, [this](int32_t syscall_ret) { accept_callback(syscall_ret); });

    while (true) {
        {
            auto tracker = wait_time.track_scope();
            io_uring.queue_wait();
        }

        if (log_requested) {
            log_requested = false;
            std::cerr << metric_registry << std::flush;
        }

        if (stop_requested) {
            stop_requested = false;
            LOG_INFO(logger, "Received SIGTERM");
            break;
        }
    }

    if (_metrics_on_stop) {
        std::cerr << metric_registry << std::flush;
    }
}

}  // namespace remotefs
