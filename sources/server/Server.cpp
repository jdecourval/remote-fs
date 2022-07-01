#include "Server.h"

#include <quill/Quill.h>

#include <optional>
#include <zmqpp/reactor.hpp>

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
    socket.monitor("inproc://monitor", zmqpp::event::accepted | zmqpp::event::closed | zmqpp::event::disconnected);
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

    auto monitoring_socket = zmqpp::socket{context, zmqpp::socket_type::pair};
    monitoring_socket.connect("inproc://monitor");

    auto reactor = zmqpp::reactor();
    reactor.add(socket, [&] {
        MessageReceiver message;
        socket.receive(message);

        if (message.parts() == 0) {
            loop_breaked.increment();
            LOG_WARNING(logger, "Received empty message");
            return;
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
    });

    reactor.add(monitoring_socket, [&] {
        auto message = zmqpp::message{};
        monitoring_socket.receive(message);

        if (message.parts() == 0) {
            loop_breaked.increment();
            LOG_WARNING(logger, "Received empty monitoring message");
            return;
        }

        struct __attribute__((packed)) event {
            uint16_t number;
            uint32_t value;
        };

        auto event = reinterpret_cast<const struct event*>(message.raw_data(0));
        LOG_DEBUG(logger, "Received event message, with {} parts, event number={}, event value={}, address={}",
                  message.parts(), event->number, event->value, message.get(1));
    });

    while (true) {
        {
            auto tracker = wait_time.track_scope();
            reactor.poll();
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