#include "Server.h"

#include <quill/Quill.h>

#include <optional>

#include "MessageReceiver.h"
#include "remotefs/FuseOp.h"

namespace remotefs {
Server::Server()
    : context(),
      socket(context, zmqpp::socket_type::xreply),
      logger{quill::get_logger()} {}

void Server::start(const std::string& address) {
#ifndef NDEBUG
    socket.set(zmqpp::socket_option::router_mandatory, true);
#endif
    LOG_INFO(logger, "Binding to {}", address);
    socket.bind(address);

    while (true) {
        MessageReceiver message;
        socket.receive(message);

        LOG_DEBUG(logger, "Received {}, with {} parts", static_cast<int>(message.op()), message.parts());
        quill::flush();

        auto response = [&]() {
            switch (message.op()) {
                case LOOKUP:
                    return syscalls.lookup(message);
                case GETATTR:
                    return syscalls.getattr(message);
                case READDIR: {
                    return syscalls.readdir(message);
                }
                case OPEN: {
                    throw std::logic_error("Not implemented");
                }
                case READ: {
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
        socket.send(response);
    }
}
}  // namespace remotefs
