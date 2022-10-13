#ifndef REMOTE_FS_TESTCLIENT_H
#define REMOTE_FS_TESTCLIENT_H

#include <chrono>
#include <thread>
#include <vector>

#include "remotefs/metrics/Metrics.h"
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
            void read_write() const;

            int socket;  // Not owner
            remotefs::IoUring& uring;
            std::pair<std::unique_ptr<remotefs::messages::both::Ping>, std::unique_ptr<remotefs::messages::both::Ping>>
                buffers;
            remotefs::MetricRegistry<>::Counter& bandwidth;
            remotefs::MetricRegistry<>::Timer& latency;
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
    };

   public:
    TestClient(int (*socket_constructor)(), int threads_n, int sockets_n, int pipeline, int chunk_size,
               bool share_ring);
    ~TestClient();
    void start();
    void register_buffers();
    void register_sockets();

   private:
    std::vector<int> sockets;  // Owner
    std::vector<remotefs::IoUring> urings;
    std::vector<ClientThread> threads;
};

#endif  // REMOTE_FS_TESTCLIENT_H
