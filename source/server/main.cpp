#include <quill/Quill.h>

#include <span>
#include <string_view>

#include "Server.h"

void print_help() {}

int main(int argc, char* argv[]) {
    quill::enable_console_colours();
    quill::start();

    quill::Logger* logger = quill::get_logger();
    logger->set_log_level(quill::LogLevel::TraceL3);

    // enable a backtrace that will get flushed when we log CRITICAL
    logger->init_backtrace(2, quill::LogLevel::Critical);

    auto args = std::span(argv, argc);

    // Print help if no arguments are given
    if (args.size() == 1) {
        print_help();
        return 0;
    }

    // process parameters
    for (auto arg : args) {
        auto tmp = std::string_view(arg);

        if (tmp == "--help" || tmp == "-h") {
            print_help();
            return 0;
        }
    }

    auto server = remotefs::Server();
    LOG_DEBUG(logger, "Ready do start");
    server.start(args.back());

    return 0;
}
