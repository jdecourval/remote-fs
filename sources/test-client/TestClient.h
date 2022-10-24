#ifndef REMOTE_FS_TESTCLIENT_H
#define REMOTE_FS_TESTCLIENT_H

#include <chrono>
#include <thread>
#include <vector>

#include "remotefs/metrics/Metrics.h"
#include "remotefs/sockets/Socket.h"
#include "remotefs/uring/IoUring.h"

namespace quill {
class Logger;
}

namespace remotefs::messages::both {
class Ping;
}

class TestClient {
    struct ClientThread {
       public:
        struct PipelineStage {
            void read_write(long max_size_thread) const;

            remotefs::Socket& socket;
            remotefs::IoUring& uring;
            std::pair<std::unique_ptr<remotefs::messages::both::Ping>, std::unique_ptr<remotefs::messages::both::Ping>>
                buffers;
            std::atomic<int>& stages_running;
            remotefs::MetricRegistry<>::Counter& bandwidth;
            remotefs::MetricRegistry<>::Timer& latency;
            std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
            bool measure_latency = false;
        };

        explicit ClientThread(remotefs::IoUring& ring);

       private:
        friend TestClient;
        std::vector<PipelineStage> stages;
        std::jthread thread;
        remotefs::IoUring& uring;
        quill::Logger* logger = nullptr;
        remotefs::MetricRegistry<> metrics;
        std::chrono::high_resolution_clock::time_point start;
        std::unique_ptr<std::atomic<int>> stages_running = std::make_unique<std::atomic<int>>();
    };

   public:
    TestClient(const std::string& address, int port, remotefs::Socket::Options socket_options, int threads_n,
               int sockets_n, int pipeline, int chunk_size, bool share_ring, int ring_depth);
    ~TestClient();
    [[nodiscard]] bool done() const;
    void start(int min_batch_size, std::chrono::nanoseconds wait_timeout, long max_size, bool register_ring);
    void register_buffers();
    void register_sockets();

   private:
    std::vector<remotefs::Socket> sockets;
    std::vector<remotefs::IoUring> urings;
    std::vector<ClientThread> threads;
};

#endif  // REMOTE_FS_TESTCLIENT_H
