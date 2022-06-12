#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>
namespace remotefs
{

class server
{
  public:
    server();
    void start(const std::string &address);

  private:
    zmqpp::context context;
    zmqpp::socket socket;
};

} // namespace remotefs

#endif // REMOTE_FS_SERVER_H
