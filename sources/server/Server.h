#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>

#include "Config.h"
#include "Syscalls.h"
#include "remotefs/metrics/Metrics.h"
#include "remotefs/uring/IoUring.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Server {
   public:
    explicit Server(bool metrics_on_stop = false);
    void start(const std::string& address);

   private:
    int socket = 0;
    int client_socket = 0;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    Syscalls syscalls;
    IoUring io_uring;
    volatile int read_counter = 0;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
