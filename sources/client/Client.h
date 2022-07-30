#ifndef REMOTE_FS_CLIENT_H
#define REMOTE_FS_CLIENT_H

#include <fuse_lowlevel.h>

#include <string>

#include "Config.h"
#include "remotefs/messages/Messages.h"
#include "remotefs/tools/FuseOp.h"
#include "remotefs/uring/IoUring.h"

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

   private:
    void read_callback(int syscall_ret, std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer);
    quill::Logger* logger;
    int socket = 0;
    struct fuse_session* fuse_session;
    bool foreground;
    IoUring io_uring;
    volatile int read_counter = 0;
};
}  // namespace remotefs

#endif  // REMOTE_FS_CLIENT_H
