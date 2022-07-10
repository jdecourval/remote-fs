#ifndef REMOTE_FS_CLIENT_H
#define REMOTE_FS_CLIENT_H

#include <fuse_lowlevel.h>

#include <string>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include "remotefs/tools/FuseOp.h"

namespace quill {
class Logger;
}

namespace remotefs {

template <typename... Ts>
void sendcall(FuseOp op, fuse_req_t req, Ts... args);

class Client {
   public:
    explicit Client(int argc, char* argv[]);
    ~Client();
    void start(const std::string& address);

    template <typename... Ts>
    friend void sendcall(FuseOp op, fuse_req_t req, Ts... args);

   private:
    quill::Logger* logger;
    zmqpp::context context;
    zmqpp::socket socket;
    struct fuse_session* fuse_session;
    bool foreground;
};
}  // namespace remotefs

#endif  // REMOTE_FS_CLIENT_H
