#include "Server.h"

#include <quill/Quill.h>

#include <memory>
#include <optional>

#include "remotefs/tools/FuseOp.h"
#include "remotefs/tools/NarrowCast.h"

namespace {
thread_local unsigned long long active_registered_buffers = 0;  // bitmap of used buffers.

}  // namespace

namespace remotefs {
extern "C" {
static volatile auto log_requested = false;
static volatile auto stop_requested = false;

void signal_usr1_handler(int signal) {
    assert(signal == SIGUSR1);
    (void)signal;
    log_requested = true;
}

void signal_term_handler(int signal) {
    assert(signal == SIGTERM);
    (void)signal;
    stop_requested = true;
}
}

Server::Server(const std::string& address, int port, const Socket::Options& socket_options, bool metrics_on_stop,
               int ring_depth, int max_registered_buffers, int cached_buffers)
    : socket{},
      logger{quill::get_logger()},
      metric_registry{},
      io_uring{ring_depth},
      syscalls{io_uring},
      _metrics_on_stop{metrics_on_stop} {
    std::signal(SIGUSR1, signal_usr1_handler);
    std::signal(SIGTERM, signal_term_handler);
    std::signal(SIGPIPE, SIG_IGN);

    if (max_registered_buffers > 0) {
        io_uring.register_sparse_buffers(max_registered_buffers);
    }

    // For now, cached buffers have to be registered because the bitmap is used for both features.
    assert(cached_buffers <= max_registered_buffers);
    buffers_cache.resize(cached_buffers);

    const auto available_bits = std::numeric_limits<decltype(active_registered_buffers)>::digits;
    assert(max_registered_buffers <= available_bits);
    // If the value of the right operand (of the shift) is greater or equal to the number of bits in the promoted left
    // operand, the behavior is undefined
    active_registered_buffers = max_registered_buffers == 0 ? 0
                                                            : ~decltype(active_registered_buffers){} >>
                                                                  (available_bits - max_registered_buffers);

    LOG_INFO(logger, "Binding to {}", address);
    socket = remotefs::Socket::listen(address, port, socket_options);
}

Server::RegisteredBuffer Server::new_registered_buffer() {
    auto index = std::countr_zero(active_registered_buffers);  // 0-indexed

    if (index >= std::numeric_limits<decltype(active_registered_buffers)>::digits) {
        // no more index available
        LOG_TRACE_L1(logger, "No more index");
        return RegisteredBuffer(-1);
    }
    active_registered_buffers ^= 0b1ull << index;

    auto buffer = [&] {
        if (std::ssize(buffers_cache) > index) {
            if (auto& buffer = buffers_cache[index]) {
                LOG_TRACE_L1(logger, "Found in cache at index {}", index);
                return buffer.non_owning_copy();
            } else {
                LOG_TRACE_L1(logger, "Initializing new cache entry at index {}", index);
                auto new_buffer = (buffers_cache[index] = RegisteredBuffer(index)).non_owning_copy();
                io_uring.assign_buffer(narrow_cast<int>(index), new_buffer.view());
                return std::move(new_buffer);
            }
        } else {
            LOG_TRACE_L1(logger, "Creating untracked buffer");
            return RegisteredBuffer(index);
        }
    }();

    return buffer;
}

void Server::accept_callback(int client_socket, int pipeline) {
    if (client_socket >= 0) {
        LOG_INFO(logger, "Accepted a connection");
        for (auto i = 0; i < pipeline; i++) {
            auto buffer = new_registered_buffer();
            read(client_socket, 0, std::move(buffer),
                 [this, client_socket = Socket{client_socket}](int32_t syscall_ret, auto&& buffer,
                                                               std::span<std::byte> data) mutable {
                     read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(buffer)>(buffer), data);
                 });
        }
    } else {
        LOG_ERROR(logger, "Error accepting a connection {}", std::strerror(-client_socket));
    }
}

void Server::read_callback(int syscall_ret, Socket&& client_socket, RegisteredBuffer buffer,
                           std::span<std::byte> result) {
    // TODO: Perhaps RegisteredBuffer should contain an offset of where its data is located? That would avoid passing a
    // span around.

    auto client_socket_int = static_cast<int>(client_socket);

    if (syscall_ret < 0) [[unlikely]] {
        if (syscall_ret == -ECONNRESET || syscall_ret == -EPIPE) {
            LOG_INFO(logger, "Connection reset by peer. Closing socket.");
            return;
        }

        LOG_ERROR(logger, "Read failed, retrying: {}", std::strerror(-syscall_ret));
        read(client_socket_int, 0, std::move(buffer),
             [this, client_socket = std::move(client_socket)](int32_t syscall_ret, auto&& buffer,
                                                              std::span<std::byte> data) mutable {
                 read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(buffer)>(buffer), data);
             });
        return;
    }

    if (syscall_ret == 0) [[unlikely]] {
        LOG_INFO(logger, "End of file detected. Closing socket.");
        return;
    }

    LOG_TRACE_L2(logger, "Read {} bytes of {}", syscall_ret, static_cast<int>(result[0]));
    switch (result[0]) {
        case messages::requests::Open().tag: {
            syscalls.open(*reinterpret_cast<messages::requests::Open*>(result.data()), client_socket);
            break;
        }
        case messages::requests::Lookup().tag: {
            syscalls.lookup(*reinterpret_cast<messages::requests::Lookup*>(result.data()), client_socket);
            break;
        }
        case messages::requests::GetAttr().tag: {
            syscalls.getattr(*reinterpret_cast<messages::requests::GetAttr*>(result.data()), client_socket);
            break;
        }
        case messages::requests::ReadDir().tag: {
            syscalls.readdir(*reinterpret_cast<messages::requests::ReadDir*>(result.data()), client_socket);
            break;
        }
        case messages::requests::Read().tag:
            syscalls.read(*reinterpret_cast<messages::requests::Read*>(result.data()), client_socket);
            break;
        case messages::requests::Release().tag:
            syscalls.release(*reinterpret_cast<messages::requests::Release*>(result.data()));
            break;
        case std::byte{7}: {
            //            auto& message = *reinterpret_cast<messages::both::Ping*>(result.data());
            //    message.middle = std::chrono::high_resolution_clock::now();
            write(client_socket_int, std::move(buffer), result, [](int ret) {
                if (ret == -EPIPE) [[unlikely]] {
                    LOG_INFO(quill::get_logger(), "SIGPIPE, closing socket");
                } else if (ret < 0) [[unlikely]] {
                    throw std::system_error(-ret, std::system_category(), "Failed to write to socket");
                }
            });
            break;
        }
        default:
            assert(false);
    }
    if (!buffer) {  // NOLINT(bugprone-use-after-move)
        buffer = new_registered_buffer();
    }

    read(client_socket_int, 0, std::move(buffer),
         [this, client_socket = std::move(client_socket)](int32_t syscall_ret, RegisteredBuffer&& buffer,
                                                          std::span<std::byte> data) mutable {
             read_callback(syscall_ret, std::move(client_socket), std::forward<decltype(buffer)>(buffer), data);
         });
}

void Server::start(int pipeline, int min_batch_size, std::chrono::nanoseconds wait_timeout, bool register_ring) {
    std::signal(SIGPIPE, SIG_IGN);

    if (register_ring) {
        io_uring.register_ring();
    }

    //    auto& loop_breaked = metric_registry.create_counter("loop-break");
    auto& wait_time = metric_registry.create_timer("message-received");
    //    auto& getattr_timing = metric_registry.create_histogram("message-received-getattr");
    //    auto& readdir_timing = metric_registry.create_histogram("message-received-readdir");
    //    auto& open_timing = metric_registry.create_histogram("message-received-open");
    //    auto& read_timing = metric_registry.create_histogram("message-received-read");
    //    auto& lookup_timing = metric_registry.create_histogram("message-received-lookup");
    //    auto& release_timing = metric_registry.create_histogram("message-received-release");
    //    auto& send_timing = metric_registry.create_histogram("message-sent");

    io_uring.accept(socket, [this, pipeline](int32_t syscall_ret) { accept_callback(syscall_ret, pipeline); });

    while (!stop_requested) [[likely]] {
        {
            auto tracker = wait_time.track_scope();
            io_uring.queue_wait(min_batch_size, wait_timeout);
        }

        if (log_requested) [[unlikely]] {
            log_requested = false;
            std::cerr << metric_registry << std::flush;
        }
    }

    if (_metrics_on_stop) {
        std::cerr << metric_registry << std::flush;
    }
}

template <typename Callable>
void Server::write(int client_socket, RegisteredBuffer&& buffer, std::span<std::byte> source, Callable&& callback) {
    static_assert(std::is_invocable_v<Callable, int>);
    auto buffer_index = buffer.get_index();
    auto buffer_total = buffer.view();

    // Must capture buffer until the call is completed
    auto callback_ptr = new (buffer_total.data()) IoUring::CallbackWithPointer{
        [buffer = std::move(buffer), callback = std::forward<Callable>(callback)](int ret) mutable {
            auto free_buffer_after_use = std::move(buffer);
            return callback(ret);
        }};
    // The most horrible of hack. Assume reading uses header at least as big as writes.
    assert((reinterpret_cast<std::byte*>(callback_ptr) + sizeof(*callback_ptr)) < source.data());

    if (buffer_index >= 0) {
        io_uring.write_fixed(client_socket, source, buffer_index, callback_ptr);
    } else {
        io_uring.write(client_socket, source, callback_ptr);
    }
}

template <typename Callable>
void Server::read(int client_socket, int offset, RegisteredBuffer&& buffer, Callable&& callback) {
    static_assert(std::is_invocable_v<Callable, int, RegisteredBuffer&&, std::span<std::byte>>);

    auto buffer_total = buffer.view();
    auto buffer_index = buffer.get_index();

    auto callback_ptr = new (buffer_total.data()) IoUring::CallbackWithPointer{
        [buffer = std::move(buffer), callback = std::forward<Callable>(callback),
         buffer_location = std::span<std::byte>{}](int res, std::span<std::byte> set = {}) mutable {
            if (!set.empty()) {
                buffer_location = set;
                return;
            }

            assert(!buffer_location.empty());
            callback(res, std::move(buffer), buffer_location);
        }};

    auto buffer_available =
        buffer_total.subspan(std::distance(reinterpret_cast<std::byte*>(callback_ptr), callback_ptr->buffer));
    callback_ptr->callable(0, buffer_available);  // Nasty hack, I know.

    if (buffer_index >= 0) {
        io_uring.read_fixed(client_socket, buffer_available, buffer_index, offset, callback_ptr);
    } else {
        io_uring.read(client_socket, buffer_available, offset, callback_ptr);
    }
}

Server::RegisteredBuffer::~RegisteredBuffer() {
    if (is_registered()) {
        LOG_DEBUG(quill::get_logger(), "Unregistering buffer {}", index);
        active_registered_buffers ^= 0b1ull << index;
    }
}

std::span<std::byte> Server::RegisteredBuffer::view() {
    return std::visit([](auto&& i) { return std::span{*i}; }, buffer);
}

std::span<const std::byte> Server::RegisteredBuffer::view() const {
    return std::visit([](auto&& i) { return std::span{*i}; }, buffer);
}

int Server::RegisteredBuffer::get_index() const {
    return index;
}

bool Server::RegisteredBuffer::is_registered() const {
    return index >= 0;
}

bool Server::RegisteredBuffer::is_owner() const {
    return std::holds_alternative<std::unique_ptr<BufferStorage>>(buffer);
}

Server::RegisteredBuffer Server::RegisteredBuffer::non_owning_copy() const {
    return RegisteredBuffer{std::visit([](auto&& i) { return std::to_address(i); }, buffer), index};
}

Server::RegisteredBuffer::operator bool() const {
    return std::visit([](auto&& i) { return std::to_address(i) != nullptr; }, buffer);
}
}  // namespace remotefs
