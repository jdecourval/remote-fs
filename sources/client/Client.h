#ifndef REMOTE_FS_CLIENT_H
#define REMOTE_FS_CLIENT_H

#include <fuse3/fuse_i.h>
#include <fuse_lowlevel.h>

#include <string>

#include "Config.h"
#include "remotefs/messages/Messages.h"
#include "remotefs/sockets/Socket.h"
#include "remotefs/tools/FuseOp.h"
#include "remotefs/uring/IoUring.h"

namespace quill {
class Logger;
}

namespace remotefs {
class Client {
    static const auto PAGE_SIZE = 4096;
    static const auto FUSE_REQUEST_SIZE = FUSE_MAX_MAX_PAGES * PAGE_SIZE + FUSE_BUFFER_HEADER_SIZE;
    using FuseReplyBuf = messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>;

   public:
    explicit Client(int argc, char* argv[]);
    ~Client();
    void start(const std::string& address);

   private:
    void common_init(int argc, char* argv[]);

    template <auto BufferSize>
    void read_callback(
        int syscall_ret, std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, BufferSize>>> old_callback
    );

    void fuse_callback(
        int syscall_ret, std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, FUSE_REQUEST_SIZE>>> buffer
    );

    template <auto BufferSize>
    void fuse_reply_data(std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, BufferSize>>> buffer);
    quill::Logger* logger;
    remotefs::Socket socket;
    IoUring io_uring;
    struct fuse_session* fuse_session;
    int fuse_fd;
    int fuse_uring_idx;
    int socket_uring_idx;
    fuse_chan fuse_channel;

    static thread_local Client* self;
    static struct fuse_session* static_fuse_session;
    static std::atomic_flag common_init_done;
};
}  // namespace remotefs

#endif  // REMOTE_FS_CLIENT_H
