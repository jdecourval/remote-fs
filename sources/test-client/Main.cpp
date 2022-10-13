#include <netdb.h>
#include <netinet/sctp.h>
#include <quill/Quill.h>

#include <argparse/argparse.hpp>
#include <cstddef>

#include "TestClient.h"
#include "remotefs/messages/Messages.h"
#include "remotefs/tools/GetAddrInfoErrorCategory.h"

using remotefs::messages::both::Ping;
using namespace std::chrono_literals;

namespace {
volatile bool stop_requested = false;
}

void signal_handler(int signal) {
    std::cout << "Received signal: " << signal << std::endl;
    stop_requested = true;
}

int connect_socket(const std::string& address, long rx_buffer_size, long tx_buffer_size, int delivery_point,
                   int fragment_size, uint16_t max_streams, bool ordered, bool nodelay, bool nofragment) {
    struct addrinfo const hosthints{.ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_SCTP};
    struct addrinfo* hostinfo;  // TODO: unique_ptr with freeaddrinfo
    if (auto ret = getaddrinfo(address.c_str(), "5001", &hosthints, &hostinfo); ret < 0) {
        throw std::system_error(ret, GetAddrInfoErrorCategory(), "Failed to resolve address");
    }

    auto socket = ::socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol);
    if (socket < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure socket");
    }

    struct sctp_initmsg initmsg {
        .sinit_num_ostreams = max_streams, .sinit_max_instreams = max_streams,
    };
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(struct sctp_initmsg)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure sctp init message");
    }

    auto sctp_flags = sctp_sndrcvinfo{};
    socklen_t sctp_flags_size = sizeof(sctp_flags);
    if (getsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, &sctp_flags_size) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to get default SCTP options");
    }
    sctp_flags.sinfo_flags |= SCTP_UNORDERED && !ordered;
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_DEFAULT_SEND_PARAM, &sctp_flags, (socklen_t)sizeof(sctp_flags)) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure SCTP default send options");
    }

    {
        int nofragment_int = static_cast<int>(nofragment);
        if (setsockopt(socket, IPPROTO_SCTP, SCTP_DISABLE_FRAGMENTS, &nofragment_int,
                       (socklen_t)sizeof(nofragment_int)) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to disable SCTP fragments");
        }
    }
    {
        int nodelay_int = static_cast<int>(nodelay);
        if (setsockopt(socket, IPPROTO_SCTP, SCTP_NODELAY, &nodelay_int, (socklen_t)sizeof(nodelay_int)) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to disable nagle's algorithm");
        }
    }
    // TODO: Check that max-seg is not greater than PMTU, because SCTP fragmentation happens at PMTU anyway and so
    // larger values have no effect.
    auto fragment_size_kernel = sctp_assoc_value{.assoc_id = 0,  // ignored for 1:1 associations
                                                 .assoc_value = static_cast<uint32_t>(fragment_size)};
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_MAXSEG, &fragment_size_kernel, sizeof(fragment_size_kernel))) {
        throw std::system_error(errno, std::system_category(), "Failed to set fragment size");
    }
    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rx_buffer_size, sizeof(rx_buffer_size))) {
        throw std::system_error(errno, std::system_category(), "Failed to set receive buffer size");
    }
    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &tx_buffer_size, sizeof(tx_buffer_size))) {
        throw std::system_error(errno, std::system_category(), "Failed to set transmit buffer size");
    }
    if (setsockopt(socket, IPPROTO_SCTP, SCTP_PARTIAL_DELIVERY_POINT, &delivery_point, sizeof(delivery_point))) {
        throw std::system_error(errno, std::system_category(), "Failed to set delivery point");
    }

    if (connect(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to connect socket");
    }

    return socket;
}

argparse::ArgumentParser program("test client");
int main(int argc, char* argv[]) {
    int verbosity = 0;
    auto data_limit = std::numeric_limits<size_t>::max();

    program.add_argument("-v", "--verbose")
        .help("Increase output verbosity, up to four times.")
        .action([&](const auto&) { ++verbosity; })
        .append()
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("-j", "--threads")
        .help("How many threads to use.")
        .default_value(1)
        //        .implicit_value(static_cast<int>(std::thread::hardware_concurrency()))
        .scan<'d', int>();

    program.add_argument("-S", "--sockets")
        .help("How many sockets to use per thread. Use 0 to share a single socket between all threads.")
        .default_value(1)
        .scan<'d', int>();

    program.add_argument("-s", "--max-size").help("Stop after transferring this much data.").default_value(data_limit);

    program.add_argument("-c", "--chunk-size")
        .help("Deliver data to the application in chunk this big.")
        .default_value(65475 - 20)
        .scan<'d', int>();
    program.add_argument("--fragment-size")
        .help("Fragment chunks on the network to at most this big (bytes). Default to the PMTU.")
        .default_value(0);
    program.add_argument("-p", "--pipeline")
        .help("How many operations per thread, socket and stream to schedule at a time.")
        .default_value(1)
        .scan<'d', int>();
    program.add_argument("-n", "--nagle").help("Enable the nagle's algorithm.").default_value(false);
    program.add_argument("-r", "--rx-buffer-size")
        .help("How big the socket's RX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    program.add_argument("-s", "--tx-buffer-size")
        .help("How big the socket's TX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    program.add_argument("-R", "--share-ring")
        .help("Share io uring between all threads.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-D", "--ring-depth").help("io uring queue depth.").default_value(64);
    program.add_argument("-B", "--register-buffers")
        .help("Register buffers in io uring.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-V", "--register-sockets")
        .help("Register sockets in io uring.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-R", "--disable-fragment")
        .help(
            "Enforce that no SCTP fragmentation occurs. "
            "Have no effect if --chunk-size is smaller than --fragment-size.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-O", "--ordered-delivery")
        .help("Enable SCTP ordered delivery.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--streams").help("Multiplex on this many SCTP streams per thread.").default_value(1);
    program.add_argument("--sockets")
        .help("Multiplex on this many sockets per thread.")
        .default_value(1)
        .scan<'d', int>();
    program.add_argument("--min-batch")
        .help("Process at least this many messages in an iteration of the event loop.")
        .default_value(1);
    program.add_argument("--buffers-alignment")
        .help("Override default buffers alignment")
        .scan<'d', size_t>()
        .default_value(alignof(remotefs::messages::both::Ping));

    program.add_argument("-L", "--latency")
        .help("Measure the round trip latency.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("address").help("Address to bind to.");

    quill::enable_console_colours();
    quill::start(true);
    quill::Logger* logger = quill::get_logger();
    logger->init_backtrace(2, quill::LogLevel::Critical);

    switch (verbosity) {
        case 0:
            logger->set_log_level(quill::LogLevel::Info);
            break;
        case 1:
            logger->set_log_level(quill::LogLevel::Debug);
            break;
        case 2:
            logger->set_log_level(quill::LogLevel::TraceL1);
            break;
        case 3:
            logger->set_log_level(quill::LogLevel::TraceL2);
            break;
        default:
            logger->set_log_level(quill::LogLevel::TraceL3);
            break;
    }

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    if (program.get<int>("--streams") != 1) {
        throw std::logic_error("--streams is unimplemented.");
    }

    auto threads = program.get<int>("--threads");
    auto sockets = program.get<int>("--sockets");
    auto pipeline = program.get<int>("--pipeline");
    auto socket_constructor = [] {
        return connect_socket(program.get("address"), program.get<long>("--rx-buffer-size"),
                              program.get<long>("--tx-buffer-size"), program.get<int>("--chunk-size"),
                              program.get<int>("--fragment-size"), program.get<int>("--streams"),
                              program.get<bool>("--ordered-delivery"), !program.get<bool>("--nagle"),
                              program.get<bool>("--disable-fragment"));
    };
    auto client = TestClient{socket_constructor,
                             threads,
                             sockets,
                             pipeline,
                             program.get<int>("--chunk-size"),
                             program.get<bool>("--share-ring")};

    if (program.get<bool>("--register-buffers")) {
        client.register_buffers();
    }

    if (program.get<bool>("--register-sockets")) {
        client.register_buffers();
    }

    client.start();

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    while (!stop_requested) {
        std::this_thread::sleep_for(1s);
    }

    LOG_INFO(logger, "Cleanly exited");
}
