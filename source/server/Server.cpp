#include "Server.h"

#include <quill/Quill.h>

#include <optional>

#include "MessageReceiver.h"

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
    : context{},
      socket{context, zmqpp::socket_type::xreply},
      logger{quill::get_logger()},
      metric_registry{},
      syscalls{},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
}

void Server::start(const std::string& address) {
#ifndef NDEBUG
    socket.set(zmqpp::socket_option::router_mandatory, true);
#endif
    LOG_INFO(logger, "Binding to {}", address);
    socket.bind(address);
    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_histogram("message-received");
    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    auto& open_timing = metric_registry.create_histogram("message-received-open");
    auto& read_timing = metric_registry.create_histogram("message-received-read");
    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    auto& send_timing = metric_registry.create_histogram("message-sent");

    while (true) {
        if (log_requested) {
            log_requested = false;
            std::cerr << metric_registry << std::flush;
        }

        if (stop_requested) {
            stop_requested = false;
            LOG_INFO(logger, "Received SIGTERM");
            break;
        }

        MessageReceiver message;
        {
            auto tracker = wait_time.track_scope();
            socket.receive(message);
        }

        if (message.parts() == 0) {
            loop_breaked.increment();
            continue;
        }

        LOG_DEBUG(logger, "Received {}, with {} parts", static_cast<int>(message.op()), message.parts());

        auto response = [&]() {
            switch (message.op()) {
                case LOOKUP: {
                    auto tracker = lookup_timing.track_scope();
                    return syscalls.lookup(message);
                }
                case GETATTR: {
                    auto tracker = getattr_timing.track_scope();
                    return syscalls.getattr(message);
                }
                case READDIR: {
                    auto tracker = readdir_timing.track_scope();
                    return syscalls.readdir(message);
                }
                case OPEN: {
                    auto tracker = open_timing.track_scope();
                    throw std::logic_error("Not implemented");
                }
                case READ: {
                    auto tracker = read_timing.track_scope();
                    return syscalls.read(message);
                }
                    //        case OPENDIR: {
                    //            // Return some number, add the open directory_entry to
                    //            a cache
                    //        }
                default:
                    throw std::logic_error("Not implemented");
            }
        }();
        {
            auto tracker = send_timing.track_scope();
            socket.send(response);
        }
    }

    if (_metrics_on_stop) {
        std::cerr << metric_registry << std::flush;
    }
}
}  // namespace remotefs
