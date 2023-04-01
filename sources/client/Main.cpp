#include <quill/Quill.h>

#include "Client.h"

int main(int argc, char* argv[]) {
    auto cfg = quill::Config{.enable_console_colours = true};
    quill::configure(cfg);
    quill::start(true);

    quill::Logger* logger = quill::get_logger();
    logger->set_log_level(quill::LogLevel::TraceL3);

    // enable a backtrace that will get flushed when we log CRITICAL
    logger->init_backtrace(2, quill::LogLevel::Critical);

    auto args = std::span(argv, argc);

    auto threads = std::vector<std::jthread>{};
    for (auto i = 0; i < 1; i++) {
        threads.emplace_back(
            [](int argc, char* argv[], auto args) {
                LOG_DEBUG(quill::get_logger(), "Ready to start");
                auto client = remotefs::Client(argc - 1, argv);
                client.start(args.back());
            },
            argc, argv, args
        );
    }

    LOG_INFO(logger, "Waiting for workers");
    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}
