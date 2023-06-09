#include "TestClient.h"

#include <quill/Quill.h>
#include <remotefs/messages/Messages.h>

#include "EngFormat-Cpp/eng_format.hpp"

TestClient::TestClient(
    const std::string& address, int port, remotefs::Socket::Options socket_options, int threads_n, int sockets_n,
    int pipeline, size_t chunk_size, bool share_ring, int ring_depth, int register_buffers
) {
    assert(sockets_n >= 0);
    assert(threads_n > 0);

    auto total_sockets = std::max(1, sockets_n * threads_n);
    sockets.reserve(total_sockets);
    for (auto i = 0; i < total_sockets; i++) {
        sockets.push_back(remotefs::Socket::connect(address, port, socket_options));
    }

    auto uring_n = share_ring ? 1 : threads_n;
    urings.reserve(uring_n);
    for (auto i = 0; i < uring_n; i++) {
        urings.emplace_back(ring_depth, register_buffers);
    }

    threads.reserve(threads_n);
    for (auto i = 0; i < threads_n; i++) {
        auto& ring = urings.at(i % urings.size());
        auto& thread = threads.emplace_back(ring);
        auto& bandwidth_metric = thread.metrics.create_counter("bandwidth");
        auto& latency_metric = thread.metrics.create_timer("latency");

        for (auto j = 0; j < std::max(1, sockets_n) * pipeline; j++) {
            auto& socket = sockets.at((j * (std::max(1, sockets_n) * pipeline) + i) % sockets.size());
            thread.stages.push_back({socket, ring, *thread.stages_running, bandwidth_metric, latency_metric, chunk_size}
            );
        }
    }

    //    std::signal(SIGUSR1, signal_usr1_handler);
    //    std::signal(SIGTERM, signal_term_handler);
    std::signal(SIGPIPE, SIG_IGN);
}

void TestClient::ClientThread::PipelineStage::read_write(long max_size_thread) const {
    LOG_TRACE_L2(quill::get_logger(), "Scheduling");

    auto read_callable = [this, max_size_thread](int32_t syscall_ret) mutable {
        if (measure_latency) {
            latency += std::chrono::high_resolution_clock::now() - start_time;
        }

        assert(syscall_ret >= 0);
        LOG_TRACE_L1(quill::get_logger(), "Received data: {}", syscall_ret);

        bandwidth += syscall_ret;
        if (bandwidth.get() < max_size_thread) [[likely]] {
            read_write(max_size_thread);
        } else {
            stages_running--;
        }
    };

    using Ping = remotefs::messages::both::Ping<remotefs::IoUring::MaxPayloadForCallback<decltype(read_callable)>()>;

    {
        auto write_callback =
            chunk_size
                ? uring.get_callback<Ping>(
                      [](int ret) { LOG_TRACE_L1(quill::get_logger(), "Wrote data: {}", ret); }, chunk_size
                  )
                : uring.get_callback<Ping>([](int ret) { LOG_TRACE_L1(quill::get_logger(), "Wrote data: {}", ret); });
        auto view = write_callback->get_storage().view();
        uring.write_fixed(socket, view, std::move(write_callback));
    }

    auto read_callback = chunk_size ? uring.get_callback<Ping>(std::move(read_callable), chunk_size)
                                    : uring.get_callback<Ping>(std::move(read_callable));
    auto view = read_callback->get_storage().view();
    uring.read(socket, view, 0, std::move(read_callback));
}

void TestClient::start(int min_batch_size, std::chrono::nanoseconds wait_timeout, long max_size, bool register_ring) {
    for (auto& thread : threads) {
        *thread.stages_running = static_cast<int>(std::ssize(thread.stages));
        thread.thread = std::jthread{
            [&thread, min_batch_size, wait_timeout, register_ring,
             max_size_thread = max_size / static_cast<int>(threads.size())](std::stop_token stop_token) mutable {
                thread.start = std::chrono::high_resolution_clock::now();
                thread.uring.start();
                if (register_ring) {
                    thread.uring.register_ring();
                }

                for (auto& stage : thread.stages) {
                    stage.read_write(max_size_thread);
                }

                while (!stop_token.stop_requested() && *thread.stages_running > 0) [[likely]] {
                    thread.uring.queue_wait(min_batch_size, wait_timeout);
                }
                auto thread_time = std::chrono::duration_cast<std::chrono::duration<double>>(
                    std::chrono::high_resolution_clock::now() - thread.start
                );
                std::cout << "thread-time:" << to_engineering_string(thread_time.count(), 3, eng_prefixed, "s")
                          << std::endl;
                std::cout << to_engineering_string(
                                 static_cast<double>(thread.stages.back().bandwidth.get()) /
                                     std::chrono::duration_cast<std::chrono::duration<double>>(thread_time).count(),
                                 3, eng_prefixed, "B/s"
                             )
                          << std::endl;
                std::cout << thread.metrics << std::endl;
            }};
    }
}

void TestClient::register_sockets() {}

bool TestClient::done() const {
    for (auto& thread : threads) {
        if (thread.stages_running->load() > 0) {
            return false;
        }
    }

    return true;
}

TestClient::~TestClient() {
    for (auto& thread : threads) {
        thread.thread.get_stop_source().request_stop();
    }
    for (auto& thread : threads) {
        thread.thread.join();
    }
}

TestClient::ClientThread::ClientThread(remotefs::IoUring& ring)
    : uring{ring} {}
