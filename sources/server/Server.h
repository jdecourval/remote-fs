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
    void read_callback(int syscall_ret, int client_socket,
                       std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>&& buffer);
    void accept_callback(int syscall_ret);

   private:
    int socket = 0;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    Syscalls syscalls;
    IoUring io_uring;
    volatile int read_counter = 0;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
