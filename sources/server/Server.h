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
    using BufferStorage = std::array<std::byte, settings::MAX_MESSAGE_SIZE>;

    class RegisteredBuffer {
       public:
        RegisteredBuffer()
            : buffer{nullptr},
              index{-1} {}

        explicit RegisteredBuffer(int idx)
            : buffer{new BufferStorage},
              index{idx} {}

        RegisteredBuffer(BufferStorage* _buffer, int idx)
            : buffer{_buffer},
              index{idx} {}

        RegisteredBuffer(const RegisteredBuffer&) = delete;

        RegisteredBuffer(RegisteredBuffer&& source) noexcept {
            assert(this != &source);
            assert(source);
            index = source.index;
            buffer = std::move(source.buffer);
            source.index = -1;
            source.buffer = nullptr;
        }

        RegisteredBuffer& operator=(RegisteredBuffer&& source) noexcept {
            assert(this != &source);
            assert(source);
            index = source.index;
            source.index = -1;
            buffer = std::move(source.buffer);
            source.buffer = nullptr;
            return *this;
        }

        RegisteredBuffer& operator=(const RegisteredBuffer&) = delete;

        ~RegisteredBuffer();

        [[nodiscard]] bool is_registered() const;
        [[nodiscard]] bool is_owner() const;
        [[nodiscard]] int get_index() const;
        [[nodiscard]] std::span<std::byte> view();
        [[nodiscard]] std::span<const std::byte> view() const;
        [[nodiscard]] RegisteredBuffer non_owning_copy() const;
        [[nodiscard]] explicit operator bool() const;

       private:
        std::variant<BufferStorage*, std::unique_ptr<BufferStorage>> buffer;
        int index = -1;
    };

   public:
    explicit Server(const std::string& address, int port, const Socket::Options& socket_options,
                    bool metrics_on_stop = false, int ring_depth = remotefs::IoUring::queue_depth_default,
                    int max_registered_buffers = 64, int cached_buffers = 64);
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring);
    void read_callback(int syscall_ret, Socket&& client_socket, RegisteredBuffer buffer, std::span<std::byte> result);
    void accept_callback(int client_socket, int pipeline);

    template <typename Callable>
    void read(int client_socket, int offset, RegisteredBuffer&& buffer, Callable&& callback);

    template <typename Callable>
    void write(int client_socket, RegisteredBuffer&& buffer, std::span<std::byte> source, Callable&& callback);

   private:
    RegisteredBuffer new_registered_buffer();

    remotefs::Socket socket;
    quill::Logger* logger;
    MetricRegistry<settings::DISABLE_METRICS> metric_registry;
    IoUring io_uring;
    Syscalls syscalls;
    std::vector<RegisteredBuffer> buffers_cache;
    bool _metrics_on_stop;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SERVER_H
