#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>
#include <vector>

#include "Config.h"
#include "Syscalls.h"
#include "remotefs/metrics/Metrics.h"
#include "remotefs/sockets/Socket.h"
#include "remotefs/uring/IoUring.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Server {
   public:
    explicit Server(
        const std::string& address, int port, const Socket::Options& socket_options, bool metrics_on_stop = false,
        int ring_depth = remotefs::IoUring::queue_depth_default, int max_registered_buffers = 64, int max_clients = 64
    );
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring);

    template <auto MaxBufferSize>
    void read_callback(
        int syscall_ret, Socket client_socket,
        IoUring::CallbackWithStorageAbstractUniquePtr<std::array<std::byte, MaxBufferSize>> old_callback
    );
    void accept_callback(int client_socket, int pipeline);

   private:
    remotefs::Socket socket;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    IoUring io_uring;
    Syscalls syscalls;
    bool _metrics_on_stop;
    bool register_fd = false;  // Turning this on crashes Linux 6.2.8!
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
