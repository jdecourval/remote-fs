#include <quill/Quill.h>

#include <argparse/argparse.hpp>
#include <cstddef>

#include "TestClient.h"
#include "remotefs/messages/Messages.h"

using remotefs::messages::both::Ping;
using namespace std::chrono_literals;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
namespace {
volatile bool stop_requested = false;
argparse::ArgumentParser program("test client");
auto verbosity = 0;
}  // namespace

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void signal_handler(int signal) {
    std::cout << "Received signal: " << signal << std::endl;
    stop_requested = true;
}

void configure_argument_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-v", "--verbose")
        .help("Increase output verbosity, up to four times.")
        .action([](const auto&) { ++verbosity; })
        .append()
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    parser.add_argument("-j", "--threads")
        .help("How many threads to use.")
        .default_value(1)
        //        .implicit_value(static_cast<int>(std::thread::hardware_concurrency()))
        .scan<'d', int>();

    parser.add_argument("-S", "--sockets")
        .help("How many sockets to use per thread. Use 0 to share a single socket between all threads.")
        .default_value(1)
        .scan<'d', int>();

    parser.add_argument("-s", "--max-size")
        .help("Stop after transferring this much data.")
        .scan<'d', long>()
        .default_value(std::numeric_limits<long>::max());

    parser.add_argument("-c", "--chunk-size")
        .help("Deliver data to the application in chunk this big.")
        .default_value(65475 - 20)
        .scan<'d', int>();
    parser.add_argument("--fragment-size")
        .help("Fragment chunks on the network to at most this big (bytes). Default to the PMTU.")
        .default_value(0);
    parser.add_argument("-p", "--pipeline")
        .help("How many operations per thread, socket and stream to schedule at a time.")  // TODO: Queue depth
        .default_value(1)
        .scan<'d', int>();
    parser.add_argument("-n", "--nagle")
        .help("Enable the nagle's algorithm.")
        .implicit_value(true)
        .default_value(false);
    parser.add_argument("-r", "--rx-buffer-size")
        .help("How big the socket's RX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    parser.add_argument("-s", "--tx-buffer-size")
        .help("How big the socket's TX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    parser.add_argument("-R", "--share-ring")
        .help("Share io uring between all threads.")
        .default_value(false)
        .implicit_value(true);
    parser.add_argument("--register-ring")
        .help("Register io uring ring's fd .")
        .implicit_value(true)
        .default_value(false);
    parser.add_argument("-D", "--ring-depth")
        .help("io uring queue depth.")
        .scan<'d', int>()
        .default_value(remotefs::IoUring::queue_depth_default);
    program.add_argument("-B", "--register-buffers")
        .help("This amount of sparse buffers will be registered in io uring per thread.")
        .scan<'d', int>()
        .default_value(64);
    parser.add_argument("-V", "--register-sockets")
        .help("Register sockets in io uring.")
        .default_value(false)
        .implicit_value(true);
    parser.add_argument("-R", "--disable-fragment")
        .help(
            "Enforce that no SCTP fragmentation occurs. "
            "Have no effect if --chunk-size is smaller than --fragment-size."
        )
        .default_value(false)
        .implicit_value(true);
    parser.add_argument("-O", "--ordered-delivery")
        .help("Enable SCTP ordered delivery.")
        .default_value(false)
        .implicit_value(true);
    parser.add_argument("--streams")
        .help("Multiplex on this many SCTP streams per thread.")
        .scan<'d', std::uint16_t>()
        .default_value(std::uint16_t{1});
    parser.add_argument("--sockets")
        .help("Multiplex on this many sockets per thread.")
        .default_value(1)
        .scan<'d', int>();
    parser.add_argument("--min-batch")
        .help("Process at least this many messages in an iteration of the event loop.")
        .scan<'d', int>()
        .default_value(remotefs::IoUring::wait_min_batch_size_default);
    parser.add_argument("--batch-wait-timeout")
        .help("How long to maximally wait for --min-batch.")
        .scan<'d', long>()
        .default_value(duration_cast<std::chrono::nanoseconds>(remotefs::IoUring::wait_timeout_default).count());
    parser.add_argument("--buffers-alignment")
        .help("Override default buffers alignment")
        .scan<'d', std::size_t>()
        .default_value(remotefs::IoUring::buffers_alignment);

    parser.add_argument("address").help("Address to connect to.");
    parser.add_argument("port").help("Port to connect to.").scan<'d', int>().default_value(6512);
}

int main(int argc, char* argv[]) {
    auto cfg = quill::Config{.enable_console_colours = true};
    quill::configure(cfg);
    quill::start(true);
    quill::Logger* logger = quill::get_logger();
    logger->init_backtrace(2, quill::LogLevel::Critical);

    configure_argument_parser(program);

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

    if (program.get<std::uint16_t>("--streams") != 1) {
        throw std::logic_error("--streams is unimplemented.");
    }

    if (program.is_used("--buffers-alignment")) {
        throw std::logic_error("--buffers-alignment is unimplemented.");
    }

    auto socket_options =
        remotefs::Socket::Options{program.get<long>("--rx-buffer-size"),   program.get<long>("--tx-buffer-size"),
                                  program.get<int>("--chunk-size"),        program.get<int>("--fragment-size"),
                                  program.get<std::uint16_t>("--streams"), program.get<bool>("--ordered-delivery"),
                                  !program.get<bool>("--nagle"),           program.get<bool>("--disable-fragment")};
    auto client = TestClient{
        program.get("address"),
        program.get<int>("port"),
        socket_options,
        program.get<int>("--threads"),
        program.get<int>("--sockets"),
        program.get<int>("--pipeline"),
        remotefs::narrow_cast<size_t>(program.get<int>("--chunk-size")),
        program.get<bool>("--share-ring"),
        program.get<int>("--ring-depth"),
        program.get<int>("--register-buffers")};

    if (program.get<bool>("--register-sockets")) {
        client.register_sockets();
        throw std::logic_error("--register-sockets is unimplemented.");
    }

    client.start(
        program.get<int>("--min-batch"), std::chrono::nanoseconds{program.get<long>("--batch-wait-timeout")},
        program.get<long>("--max-size"), program.get<bool>("--register-ring")
    );

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    while (!stop_requested) {
        if (client.done()) {
            break;
        }
        std::this_thread::sleep_for(1s);
    }

    LOG_INFO(logger, "Cleanly exited");
}
