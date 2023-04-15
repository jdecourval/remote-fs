#ifndef REMOTE_FS_SOCKET_H
#define REMOTE_FS_SOCKET_H

#include <cstddef>
#include <optional>
#include <string>

namespace remotefs {
namespace detail {
struct Options {
    std::optional<long> rx_buffer_size = {};
    std::optional<long> tx_buffer_size = {};
    int delivery_point = 65536;
    int fragment_size = 0;  // 0 -> will not be limited, except by the PMTU
    std::uint16_t max_streams = 64;
    bool ordered = false;
    bool nodelay = true;
    bool nofragment = true;
};
}  // namespace detail

class Socket {
   public:
    using Options = detail::Options;  // GCC bug 88165 and clang 36684
    Socket() = default;
    explicit Socket(int s);
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    ~Socket();

    static Socket connect(const std::string& address, int port, const Options& options = {});
    static Socket listen(const std::string& address, int port, const Options& options = {});

    inline operator int() const {  // NOLINT(google-explicit-constructor)
        return socket;
    }

   private:
    void configure(const Options& options);
    int socket = -1;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SOCKET_H
