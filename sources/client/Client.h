#ifndef REMOTE_FS_CLIENT_H
#define REMOTE_FS_CLIENT_H

#include <fuse3/fuse_i.h>
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
class Client {
   public:
    explicit Client(int argc, char* argv[]);
    ~Client();
    void start(const std::string& address);

   private:
    void common_init(int argc, char* argv[]);
    void read_callback(int syscall_ret, std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer);
    void fuse_callback(int syscall_ret, std::unique_ptr<char[]>&& buffer, size_t bufsize);
    void fuse_reply_data(std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer);
    quill::Logger* logger;
    int socket = 0;
    IoUring io_uring;
    struct fuse_session* fuse_session;
    int fuse_fd;
    fuse_chan fuse_channel;

    static thread_local Client* self;
    static struct fuse_session* static_fuse_session;
    static std::atomic_flag common_init_done;
};
}  // namespace remotefs

#endif  // REMOTE_FS_CLIENT_H
