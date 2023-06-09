#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>
#include <thread>
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
    class ServerThread {
       public:
        ServerThread(IoUring&& uring, remotefs::Socket&& socket, InodeCache& inode_cache);

        template <auto MaxBufferSize>
        void read_callback(
            int syscall_ret, Socket client_socket,
            std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, MaxBufferSize>>> old_callback
        );
        void accept_callback(int client_socket, int pipeline);
        void start(
            int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, int max_clients, bool register_ring
        );
        void join();

       private:
        std::jthread thread;
        IoUring io_uring;
        remotefs::Socket socket;
        Syscalls syscalls;
        quill::Logger* logger;
        MetricRegistry<settings::DISABLE_METRICS> metric_registry{};
        bool register_fd = false;  // Turning this on crashes Linux 6.2.8!
    };

   public:
    explicit Server(
        const std::string& address, int port, const Socket::Options& socket_options, bool metrics_on_stop = false,
        int ring_depth = remotefs::IoUring::queue_depth_default, int max_registered_buffers = 64, int thread_n = 1
    );
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start(
        int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, int max_clients, bool register_ring
    );
    void join();

   private:
    InodeCache inode_cache;
    std::vector<ServerThread> threads;
    quill::Logger* logger;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
