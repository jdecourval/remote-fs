#include <quill/Quill.h>

#include <argparse/argparse.hpp>

#include "Server.h"


int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("remotefs server");
    int verbosity = 0;

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

    program.add_argument("address").help("address to bind to");

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    quill::enable_console_colours();
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

    auto server = remotefs::Server(program.get<bool>("--metrics"));
    LOG_DEBUG(logger, "Ready to start");
    server.start(program.get("address"));

    LOG_DEBUG(logger, "Cleanly exited");
    return 0;
}
