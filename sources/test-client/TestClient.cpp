#include "TestClient.h"

#include <quill/Quill.h>
#include <remotefs/messages/Messages.h>

#include "EngFormat-Cpp/eng_format.hpp"

using remotefs::messages::both::Ping;

TestClient::TestClient(const std::string& address, int port, remotefs::Socket::Options socket_options, int threads_n,
                       int sockets_n, int pipeline, int chunk_size, bool share_ring, int ring_depth)
    : shared_ring{share_ring} {
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
        urings.emplace_back(ring_depth);
    }

    threads.reserve(threads_n);
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
                                     *thread.stages_running, bandwidth_metric, latency_metric});
        }
    }
}

void TestClient::ClientThread::PipelineStage::read_write(long max_size_thread) const {
    auto read_buffer_view = buffers.first->view();
    auto write_buffer_view = buffers.second->view();

    if (buffers_indexes.second >= 0) {
        uring.write_fixed(socket, write_buffer_view, buffers_indexes.second, [](int32_t syscall_ret) mutable {
            if (syscall_ret < 0) {
                throw std::system_error(-syscall_ret, std::system_category(), "Failed to write to socket");
            }
        });
    } else {
        uring.write(socket, write_buffer_view, [](int32_t syscall_ret) mutable {
            if (syscall_ret < 0) {
                throw std::system_error(-syscall_ret, std::system_category(), "Failed to write to socket");
            }
        });
    }

    auto read_callback = [this, max_size_thread](int32_t syscall_ret) mutable {
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
        };
    };

    if (buffers_indexes.first >= 0) {
        uring.read_fixed(socket, read_buffer_view, buffers_indexes.first, 0, std::move(read_callback));
    } else {
        uring.read(socket, read_buffer_view, 0, std::move(read_callback));
    }
}

void TestClient::start(int min_batch_size, std::chrono::nanoseconds wait_timeout, long max_size, bool register_ring,
                       int register_buffers) {
    auto thread_id = 0;
    for (auto& thread : threads) {
        *thread.stages_running = static_cast<int>(std::ssize(thread.stages));
        auto index_start = shared_ring ? thread_id++ * thread.stages.size() : 0;
        thread.thread = std::jthread{
            [&thread, min_batch_size, wait_timeout, register_ring, register_buffers, index_start,
             max_size_thread = max_size / static_cast<int>(threads.size())](std::stop_token stop_token) mutable {
                thread.start = std::chrono::high_resolution_clock::now();
                if (register_ring) {
                    thread.uring.register_ring();
                }

                if (register_buffers) {
                    thread.uring.register_sparse_buffers(register_buffers);
                }

                for (auto& stage : thread.stages) {
                    if (register_buffers) {
                        stage.buffers_indexes = {index_start++, index_start++};
                        thread.uring.assign_buffer(stage.buffers_indexes.first, stage.buffers.first->view());
                        thread.uring.assign_buffer(stage.buffers_indexes.second, stage.buffers.second->view());
                    }
                    stage.read_write(max_size_thread);
                }

                while (!stop_token.stop_requested() && *thread.stages_running > 0) [[likely]] {
                    thread.uring.queue_wait(min_batch_size, wait_timeout);
                }
                auto thread_time = std::chrono::duration_cast<std::chrono::duration<double>>(
                    std::chrono::high_resolution_clock::now() - thread.start);
                std::cout << "thread-time:" << to_engineering_string(thread_time.count(), 3, eng_prefixed, "s")
                          << std::endl;
                std::cout << to_engineering_string(
                                 static_cast<double>(thread.stages.back().bandwidth.get()) /
                                     std::chrono::duration_cast<std::chrono::duration<double>>(thread_time).count(),
                                 3, eng_prefixed, "B/s")
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
