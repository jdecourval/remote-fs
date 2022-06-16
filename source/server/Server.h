#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include "Syscalls.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Server {
   public:
    Server();
    void start(const std::string& address);

   private:
    zmqpp::context context;
    zmqpp::socket socket;
    quill::Logger* logger;
    Syscalls syscalls;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
