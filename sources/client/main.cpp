#include <quill/Quill.h>

#include <span>
#include "Client.h"

int main(int argc, char* argv[]) {
    quill::enable_console_colours();
    quill::start(true);

    quill::Logger* logger = quill::get_logger();
    logger->set_log_level(quill::LogLevel::TraceL3);

    // enable a backtrace that will get flushed when we log CRITICAL
    logger->init_backtrace(2, quill::LogLevel::Critical);

    auto args = std::span(argv, argc);

    auto l = remotefs::Client(argc - 1, argv);
    LOG_DEBUG(logger, "Ready do start");
    l.start(args.back());

    return 0;
}
