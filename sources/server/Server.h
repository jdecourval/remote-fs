#ifndef REMOTE_FS_SERVER_H
#define REMOTE_FS_SERVER_H

#include <string>

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
    class RegisteredBuffer {
       public:
        explicit RegisteredBuffer(int idx)  // NOLINT(cppcoreguidelines-pro-type-member-init)
            : index{idx} {}

        ~RegisteredBuffer();

        std::span<char> view();

        [[nodiscard]] std::span<const char> view() const;

        [[nodiscard]] int get_index() const;

       private:
        std::array<char, settings::MAX_MESSAGE_SIZE> storage;
        int index;
    };

   public:
    explicit Server(const std::string& address, int port, const Socket::Options& socket_options,
                    bool metrics_on_stop = false, int ring_depth = remotefs::IoUring::queue_depth_default,
                    int max_registered_buffers = 64);
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring);
    void read_callback(int syscall_ret, Socket&& client_socket, std::unique_ptr<RegisteredBuffer> buffer);
    void accept_callback(int syscall_ret, int pipeline);

    template <typename Callable>
    void read(Socket&& client_socket, int offset, std::unique_ptr<RegisteredBuffer> buffer, Callable&& callback);

    template <typename Callable>
    void read(Socket&& client_socket, int offset, std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>> buffer,
              Callable&& callback);

    template <typename Callable>
    void write(Socket&& client_socket, std::unique_ptr<RegisteredBuffer> buffer, Callable&& callback);

    template <typename Callable>
    void write(Socket&& client_socket, int offset, std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>> buffer,
               Callable&& callback);

   private:
    std::unique_ptr<RegisteredBuffer> new_registered_buffer();

    remotefs::Socket socket;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    IoUring io_uring;
    Syscalls syscalls;
    volatile int read_counter = 0;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
