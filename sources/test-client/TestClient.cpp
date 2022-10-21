#include "TestClient.h"

#include <quill/Quill.h>
#include <remotefs/messages/Messages.h>

#include "EngFormat-Cpp/eng_format.hpp"

using remotefs::messages::both::Ping;

TestClient::TestClient(remotefs::Socket (*socket_constructor)(), int threads_n, int sockets_n, int pipeline,
                       int chunk_size, bool share_ring) {
    assert(sockets_n >= 0);
    assert(threads_n > 0);

    for (auto i = 0; i < std::max(1, sockets_n * threads_n); i++) {
        sockets.push_back(socket_constructor());
    }

    for (auto i = 0; i < (share_ring ? 1 : threads_n); i++) {
        urings.emplace_back(false);
    }

    for (auto i = 0; i < threads_n; i++) {
        auto& ring = urings.at(i % urings.size());
        auto& thread = threads.emplace_back(ring);
        auto& bandwidth_metric = thread.metrics.create_counter("bandwidth");
        auto& latency_metric = thread.metrics.create_timer("latency");

        for (auto j = 0; j < std::max(1, sockets_n) * pipeline; j++) {
            auto& socket = sockets.at((j * (std::max(1, sockets_n) * pipeline) + i) % sockets.size());
            thread.stages.push_back({socket, ring,
                                     std::pair{std::unique_ptr<Ping>{new (chunk_size) Ping(chunk_size)},
                                               std::unique_ptr<Ping>{new (chunk_size) Ping(chunk_size)}},
                                     bandwidth_metric, latency_metric});
        }
    }
}

void TestClient::ClientThread::PipelineStage::read_write() const {
    auto read_buffer_view = buffers.first->view();
    auto write_buffer_view = buffers.second->view();
    auto start_time = std::chrono::high_resolution_clock::now();

    uring.write(socket, write_buffer_view, [](int32_t syscall_ret) mutable { assert(syscall_ret >= 0); });

    uring.read(socket, read_buffer_view, 0, [this, start_time](int32_t syscall_ret) mutable {
        latency += std::chrono::high_resolution_clock::now() - start_time;
        assert(syscall_ret >= 0);

        bandwidth += syscall_ret;
        LOG_TRACE_L1(quill::get_logger(), "Received data: {}", syscall_ret);
        read_write();
    });
}

void TestClient::start() {
    for (auto& thread : threads) {
        thread.thread = std::jthread{[&thread](std::stop_token stop_token) {
            thread.start = std::chrono::high_resolution_clock::now();
            for (auto& stage : thread.stages) {
                stage.read_write();
            }

            while (!stop_token.stop_requested()) {
                thread.uring.queue_wait();
                //                std::cout << thread.metrics << "\n";
            }
            auto thread_time = std::chrono::high_resolution_clock::now() - thread.start;
            std::cout << "thread-time:" << thread_time << std::endl;
            std::cout << to_engineering_string(
                             static_cast<double>(thread.stages.back().bandwidth.get()) /
                                 std::chrono::duration_cast<std::chrono::duration<double>>(thread_time).count(),
                             3, eng_prefixed, "B/s")
                      << std::endl;
            std::cout << thread.metrics << std::endl;
        }};
    }
}

void TestClient::register_buffers() {}

void TestClient::register_sockets() {}

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
