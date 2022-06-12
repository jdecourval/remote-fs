#ifndef REMOTE_FS_CLIENT_H
#define REMOTE_FS_CLIENT_H

#include <fuse_lowlevel.h>
#include <string>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

namespace remotefs
{
enum FuseOp : uint8_t;

template <typename... Ts> void sendcall(FuseOp op, fuse_req_t req, Ts... args);

class client
{
  public:
    explicit client(int argc, char *argv[]);
    ~client();
    void start(const std::string &address);

    template <typename... Ts> friend void sendcall(FuseOp op, fuse_req_t req, Ts... args);

  private:
    zmqpp::context context;
    zmqpp::socket socket;
    struct fuse_session *fuse_session;
    struct fuse_args args;
    bool foreground;
};
} // namespace remotefs

#endif // REMOTE_FS_CLIENT_H
