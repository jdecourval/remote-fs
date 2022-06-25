#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include "Syscalls.h"
#include "config.h"
#include "remotefs/Metrics.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Server {
   public:
    Server(bool metrics_on_stop = false);
    void start(const std::string& address);

   private:
    zmqpp::context context;
    zmqpp::socket socket;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    Syscalls syscalls;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
