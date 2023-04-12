#include <quill/Quill.h>

#include <argparse/argparse.hpp>

#include "Server.h"

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("remotefs server");
    int verbosity = 0;

    // Positional arguments
    program.add_argument("address").help("address to bind to");
    program.add_argument("port").help("port to bind to").scan<'d', int>().default_value(6512);

    // Basic options
    program.add_argument("-v", "--verbose")
        .help("increase output verbosity, up to four times")
        .action([&](const auto&) { ++verbosity; })
        .append()
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("-m", "--metrics")
        .help("report metrics when the application terminates")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-j", "--threads")
        .help("how many threads to use")
        .default_value(1)
        //        .implicit_value(nproc)
        .scan<'d', int>();
    program.add_argument("-p", "--pipeline")
        .help("How many operations per thread, socket and stream to schedule at a time.")
        .default_value(1)
        .scan<'d', int>();

    // Socket options
    program.add_argument("-r", "--rx-buffer-size")
        .help("How big the socket's RX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    program.add_argument("-s", "--tx-buffer-size")
        .help("How big the socket's TX buffer is.")
        .default_value(1024 * 1024l)
        .scan<'d', long>();
    program.add_argument("-c", "--chunk-size")
        .help("Deliver data to the application in chunk this big.")
        .default_value(65475 - 20)
        .scan<'d', int>();
    program.add_argument("--fragment-size")
        .help("Fragment chunks on the network to at most this big (bytes). Default to the PMTU.")
        .default_value(0);
    program.add_argument("-O", "--ordered-delivery")
        .help("Enable SCTP ordered delivery.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-n", "--nagle").help("Enable the nagle's algorithm.").default_value(false);
    program.add_argument("-R", "--disable-fragment")
        .help(
            "Enforce that no SCTP fragmentation occurs. "
            "Have no effect if --chunk-size is smaller than --fragment-size.")
        .default_value(false)
        .implicit_value(true);

    // Advanced options
    program.add_argument("-R", "--share-ring")
        .help("Share io uring between all threads.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--register-ring")
        .help("Register io uring ring's fd .")
        .implicit_value(true)
        .default_value(false);
    program.add_argument("-D", "--ring-depth")
        .help("io uring queue depth.")
        .scan<'d', int>()
        .default_value(remotefs::IoUring::queue_depth_default);
    program.add_argument("-B", "--register-buffers")
        .help("This amount of sparse buffers will be registered in io uring per thread.")
        .scan<'d', int>()
        .default_value(64);
    program.add_argument("--cached-buffers")
        .help("Cache this number of buffers in the application instead of returning them to the memory allocator.")
        .scan<'d', int>()
        .default_value(64);
    program.add_argument("-V", "--register-sockets")
        .help("Register sockets in io uring.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--min-batch")
        .help("Process at least this many messages in an iteration of the event loop.")
        .scan<'d', int>()
        .default_value(remotefs::IoUring::wait_min_batch_size_default);
    program.add_argument("--batch-wait-timeout")
        .help("How long to maximally wait for --min-batch.")
        .scan<'d', long>()
        .default_value(duration_cast<std::chrono::nanoseconds>(remotefs::IoUring::wait_timeout_default).count());
    program.add_argument("--buffers-alignment")
        .help("Override default buffers alignment")
        .scan<'d', std::size_t>()
        .default_value(remotefs::buffers_alignment);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    auto cfg = quill::Config{.enable_console_colours = true};
    quill::configure(cfg);
    quill::start(true);

    quill::Logger* logger = quill::get_logger();

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

    // enable a backtrace that will get flushed when we log CRITICAL
    logger->init_backtrace(2, quill::LogLevel::Critical);

    auto socket_options = remotefs::Socket::Options{program.get<long>("--rx-buffer-size"),
                                                    program.get<long>("--tx-buffer-size"),
                                                    program.get<int>("--chunk-size"),
                                                    program.get<int>("--fragment-size"),
                                                    64,
                                                    program.get<bool>("--ordered-delivery"),
                                                    !program.get<bool>("--nagle"),
                                                    program.get<bool>("--disable-fragment")};

    auto server = remotefs::Server(
        program.get("address"), program.get<int>("port"), socket_options, program.get<bool>("--metrics"),
        program.get<int>("--ring-depth"), program.get<int>("--register-buffers")
    );

    LOG_DEBUG(logger, "Ready to start");
    if (program.get<int>("--threads") > 1) {
        auto threads = std::vector<std::jthread>{};
        for (auto i = 0; i < program.get<int>("--threads"); i++) {
            threads.emplace_back([&] {
                server.start(program.get<int>("--pipeline"), program.get<int>("--min-batch"),
                             std::chrono::nanoseconds{program.get<long>("--batch-wait-timeout")},
                             program.get<bool>("--register-ring"));
            });
        }

        LOG_INFO(logger, "Waiting for workers");
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        server.start(program.get<int>("--pipeline"), program.get<int>("--min-batch"),
                     std::chrono::nanoseconds{program.get<long>("--batch-wait-timeout")},
                     program.get<bool>("--register-ring"));
    }

    LOG_DEBUG(logger, "Cleanly exited");
    return 0;
}
