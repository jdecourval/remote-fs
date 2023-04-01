#include "Socket.h"

#include <netdb.h>
#include <netinet/sctp.h>
#include <unistd.h>

#include <memory>
#include <system_error>
#include <ztd/out_ptr/out_ptr.hpp>

namespace {
auto getaddrinfo(const char* address, int port, bool passive) {
    auto results = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>{nullptr, &freeaddrinfo};
    auto hosthints = addrinfo{.ai_flags = passive ? AI_PASSIVE : 0,
                              .ai_family = AF_INET,
                              .ai_socktype = SOCK_STREAM,
                              .ai_protocol = IPPROTO_SCTP};
    auto port_string = std::to_string(port);

    class GetAddrInfoErrorCategory : public std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "getaddrinfo";
        }
        [[nodiscard]] std::string message(int i) const override {
            return gai_strerror(i);
        }
    };

    if (auto ret = ::getaddrinfo(address, port_string.c_str(), &hosthints, ztd::out_ptr::out_ptr(results)); ret < 0) {
        throw std::system_error(ret, GetAddrInfoErrorCategory(), "Failed to resolve address");
    }

    return results;
}
}  // namespace

namespace remotefs {
Socket::Socket(int s)
    : socket{s} {}

Socket::Socket(Socket&& other) noexcept {
    std::swap(socket, other.socket);
}

Socket& Socket::operator=(Socket&& other) noexcept {
    socket = other.socket;
    other.socket = -1;
    return *this;
}

Socket::~Socket() {
    if (socket != -1) {
        ::close(socket);
    }

    socket = -1;
}

Socket Socket::connect(const std::string& address, int port, const Options& options) {
    auto hostinfo = getaddrinfo(address.c_str(), port, false);

    auto socket = Socket{::socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol)};
    if (socket < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure socket");
    }

    socket.configure(options);

    if (::connect(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to connect socket");
    }

    return socket;
}

Socket Socket::listen(const std::string& address, int port, const Options& options) {
    auto hostinfo = getaddrinfo(address.c_str(), port, true);
    auto socket = Socket{::socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol)};
    if (socket < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure socket");
    }

    const auto enable = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to set REUSEADDR");
    }

    socket.configure(options);

    if (::bind(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to bind socket");
    }

    if (::listen(socket, 10) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to listen to socket");
    }

    return socket;
}

void Socket::configure(const Options& options) {
    struct sctp_initmsg initmsg {
        .sinit_num_ostreams = options.max_streams, .sinit_max_instreams = options.max_streams,
    };
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(struct sctp_initmsg)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure sctp init message");
    }

    auto sctp_flags = sctp_sndrcvinfo{};
    socklen_t sctp_flags_size = sizeof(sctp_flags);
    if (getsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, &sctp_flags_size) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to get default SCTP options");
    }
    sctp_flags.sinfo_flags |= SCTP_UNORDERED && !options.ordered;
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, (socklen_t)sizeof(sctp_flags)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure SCTP default send options");
    }

    {
        int nofragment_int = options.nofragment ? 1 : 0;
        if (setsockopt(socket, IPPROTO_SCTP, SCTP_DISABLE_FRAGMENTS, &nofragment_int,
                       (socklen_t)sizeof(nofragment_int)) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to disable SCTP fragments");
        }
    }
    {
        int nodelay_int = options.nodelay ? 1 : 0;
        if (setsockopt(socket, IPPROTO_SCTP, SCTP_NODELAY, &nodelay_int, (socklen_t)sizeof(nodelay_int)) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to disable nagle's algorithm");
        }
    }
    // TODO: Check that max-seg is not greater than PMTU, because SCTP fragmentation happens at PMTU anyway and so
    // larger values have no effect.
    auto fragment_size_kernel = sctp_assoc_value{.assoc_id = 0,  // ignored for 1:1 associations
                                                 .assoc_value = static_cast<uint32_t>(options.fragment_size)};
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_MAXSEG, &fragment_size_kernel, sizeof(fragment_size_kernel))) {
        throw std::system_error(errno, std::system_category(), "Failed to set fragment size");
    }

    if (options.rx_buffer_size) {
        if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &options.rx_buffer_size.value(),
                       sizeof(options.rx_buffer_size))) {
            throw std::system_error(errno, std::system_category(), "Failed to set receive buffer size");
        }
    }
    if (options.tx_buffer_size) {
        if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &options.tx_buffer_size.value(),
                       sizeof(options.tx_buffer_size))) {
            throw std::system_error(errno, std::system_category(), "Failed to set transmit buffer size");
        }
    }
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_PARTIAL_DELIVERY_POINT, &options.delivery_point,
                   sizeof(options.delivery_point))) {
        throw std::system_error(errno, std::system_category(), "Failed to set delivery point");
    }
}
}  // namespace remotefs
