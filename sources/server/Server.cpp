#include "Server.h"

#include <netdb.h>
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
    : logger{quill::get_logger()},
      metric_registry{},
      syscalls{io_uring},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
}

void Server::start(const std::string& address) {
    class GetAddrInfoErrorCategory : public std::error_category {
        const char* name() const noexcept override {
            return "getaddrinfo";
        }
        std::string message(int i) const override {
            return gai_strerror(i);
        }
    };

    LOG_INFO(logger, "Binding to {}", address);
    struct addrinfo hosthints {
        .ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_DCCP, .ai_protocol = IPPROTO_DCCP
    };
    struct addrinfo* hostinfo;  // TODO: unique_ptr with freeaddrinfo
    if (auto ret = getaddrinfo(address.c_str(), "5001", &hosthints, &hostinfo); ret < 0) {
        throw std::system_error(ret, GetAddrInfoErrorCategory(), "Failed to resolve address");
    }

    socket = ::socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol);
    if (socket < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure socket");
    }

    const auto enable = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        throw std::system_error(errno, std::system_category(), "Failed to set REUSEADDR");

    if (::bind(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to bind socket");
    }

    if (::listen(socket, 10) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to listen to socket");
    }

    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_histogram("message-received");
    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    auto& open_timing = metric_registry.create_histogram("message-received-open");
    auto& read_timing = metric_registry.create_histogram("message-received-read");
    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    auto& release_timing = metric_registry.create_histogram("message-received-release");
    auto& send_timing = metric_registry.create_histogram("message-sent");

    io_uring.accept(socket, [this](int32_t syscall_ret) {
        if (client_socket = syscall_ret; client_socket >= 0) {
            LOG_INFO(logger, "Accepted a connection");
        } else {
            LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-syscall_ret));
        }
        return nullptr;
    });

    while (true) {
        {
            auto tracker = wait_time.track_scope();
            while (client_socket && read_counter < 10) {
                LOG_TRACE_L3(logger, "Queuing read, in progress={}", reinterpret_cast<int>(read_counter));
                read_counter++;
                auto buffer = std::make_unique<std::array<char, settings::MAX_MESSAGE_SIZE>>(
                    std::array<char, settings::MAX_MESSAGE_SIZE>{});
                auto buffer_view = std::span{buffer->data(), buffer->size()};
                io_uring.read(client_socket, buffer_view, 0, [this, buffer = std::move(buffer)](int32_t syscall_ret) {
                    read_counter--;
                    if (syscall_ret < 0) {
                        LOG_ERROR(logger, "Read failed: {}", std::strerror(-syscall_ret));
                        return nullptr;
                    }

                    if (syscall_ret == 0) {
                        LOG_DEBUG(logger, "Read NULL message");
                        return nullptr;
                    }

                    LOG_TRACE_L1(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(buffer->at(0)));
                    switch (buffer->at(0)) {
                        case 1: {
                            syscalls.open(*reinterpret_cast<messages::requests::Open*>(buffer->data()), client_socket);
                            break;
                        }
                        case 2: {
                            syscalls.lookup(*reinterpret_cast<messages::requests::Lookup*>(buffer->data()),
                                            client_socket);
                            break;
                        }
                        case 3: {
                            syscalls.getattr(*reinterpret_cast<messages::requests::GetAttr*>(buffer->data()),
                                             client_socket);
                            break;
                        }
                        case 4: {
                            syscalls.readdir(*reinterpret_cast<messages::requests::ReadDir*>(buffer->data()),
                                             client_socket);
                            break;
                        }
                        case 5:
                            syscalls.read(*reinterpret_cast<messages::requests::Read*>(buffer->data()), client_socket);
                            break;
                        case 6:
                            syscalls.release(*reinterpret_cast<messages::requests::Release*>(buffer->data()));
                            break;
                        default:
                            assert(false);
                    }
                    return nullptr;
                });
            }
            io_uring.submit();
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
