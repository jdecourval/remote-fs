#include "Server.h"

#include <netdb.h>
#include <netinet/sctp.h>
#include <quill/Quill.h>

#include <memory>
#include <optional>

#include "remotefs/tools/FuseOp.h"
#include "remotefs/tools/GetAddrInfoErrorCategory.h"

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
    std::signal(SIGPIPE, SIG_IGN);
}

void Server::accept_callback(int syscall_ret) {
    if (auto client_socket = syscall_ret; client_socket >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < 1; i++) {
            auto buffer = std::make_unique<std::array<char, settings::MAX_MESSAGE_SIZE>>();
            auto buffer_view = std::span{buffer->data(), buffer->size()};
            io_uring.read(client_socket, buffer_view, 0,
                          [this, client_socket, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                              read_callback(syscall_ret, client_socket, std::move(buffer));
                          });
        }
    } else {
        LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-syscall_ret));
    }
}

void Server::read_callback(int syscall_ret, int client_socket,
                           std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer) {
    auto buffer_view = std::span{buffer->data(), buffer->size()};

    if (syscall_ret < 0) {
        if (syscall_ret == -ECONNRESET) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            close(client_socket);
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        io_uring.read(client_socket, buffer_view, 0,
                      [this, client_socket, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                          read_callback(syscall_ret, client_socket, std::move(buffer));
                      });
        return;
    }

    if (syscall_ret == 0) {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        close(client_socket);
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
    io_uring.read(client_socket, buffer_view, 0,
                  [this, client_socket, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                      read_callback(syscall_ret, client_socket, std::move(buffer));
                  });
}

void Server::start(const std::string& address) {
    LOG_INFO(logger, "Binding to {}", address);
    struct addrinfo const hosthints{
        .ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_SCTP};
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

    const auto MAX_STREAM = 64;
    struct sctp_initmsg initmsg {};
    initmsg.sinit_num_ostreams = MAX_STREAM;
    initmsg.sinit_max_instreams = MAX_STREAM;
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(struct sctp_initmsg)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure sctp init message");
    }

    auto sctp_flags = sctp_sndrcvinfo{};
    socklen_t sctp_flags_size = sizeof(sctp_flags);
    if (getsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, &sctp_flags_size) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to get default SCTP options");
    }
    sctp_flags.sinfo_flags |= SCTP_UNORDERED;
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, (socklen_t)sizeof(sctp_flags)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure SCTP default send options");
    }

    int disable = 1;
    //    if (setsockopt(socket, IPPROTO_SCTP, SCTP_DISABLE_FRAGMENTS, &disable, (socklen_t)sizeof(disable)) != 0) {
    //        throw std::system_error(errno, std::system_category(), "Failed to disable SCTP fragments");
    //    }
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_NODELAY, &disable, (socklen_t)sizeof(disable)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to disable nagle's algorithm");
    }
    int socket_bufsize = 18203278;
    // TODO: SOL_SOCKET or IPPROTO_SCTP?
    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &socket_bufsize, (socklen_t)sizeof(socket_bufsize)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to set socket transmit buffer size");
    }
    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &socket_bufsize, (socklen_t)sizeof(socket_bufsize)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to set socket receive buffer size");
    }
    auto delivery_point = 785792;
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_PARTIAL_DELIVERY_POINT, &delivery_point, sizeof(delivery_point))) {
        throw std::system_error(errno, std::system_category(), "Failed to set delivery point");
    }

    if (::bind(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to bind socket");
    }

    if (::listen(socket, 10) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to listen to socket");
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
