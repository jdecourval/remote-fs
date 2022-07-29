#ifndef REMOTE_FS_MESSAGES_H
#define REMOTE_FS_MESSAGES_H

#include <fuse_lowlevel.h>

#include <array>
#include <chrono>

namespace remotefs::messages {
namespace both {
struct Ping {
    std::chrono::high_resolution_clock::time_point first;
    std::chrono::high_resolution_clock::time_point middle;
};
}  // namespace both
namespace requests {
struct Open {
    const uint8_t tag = 1;
    fuse_req_t req;
    fuse_ino_t ino;
    fuse_file_info file_info;
};

struct Lookup {
    uint8_t tag = 2;
    fuse_req_t req;
    fuse_ino_t ino;
    char path[];
};

struct GetAttr {
    const uint8_t tag = 3;
    fuse_req_t req;
    fuse_ino_t ino;
};

struct ReadDir {
    const uint8_t tag = 4;
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Read {
    const uint8_t tag = 5;
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Release {
    const uint8_t tag = 6;
    fuse_req_t req;
    fuse_ino_t ino;
};
}  // namespace requests

namespace responses {
struct FuseReplyEntry {
    explicit FuseReplyEntry(fuse_req_t r)
        : req{r} {}
    explicit FuseReplyEntry(fuse_req_t r, fuse_entry_param f)
        : req{r},
          attr{f} {}
    const uint8_t tag = 1;
    fuse_req_t req;
    fuse_entry_param attr;
};

struct FuseReplyAttr {
    FuseReplyAttr(auto r, auto a)
        : req{r},
          attr{a} {}
    const uint8_t tag = 2;
    fuse_req_t req;
    struct stat attr;
};

struct FuseReplyOpen {
    FuseReplyOpen(auto r, auto f)
        : req{r},
          file_info(f) {}
    const uint8_t tag = 3;
    fuse_req_t req;
    fuse_file_info file_info;
};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
template <size_t max_size>
struct FuseReplyBuf {
    explicit FuseReplyBuf(auto r)
        : req{r} {}
    FuseReplyBuf(auto r, auto d)
        : req{r},
          data_size{d} {}

    const uint8_t tag = 4;
    fuse_req_t req;
    int data_size{};
    // TODO: This is wrong, as it ignores padding
    // TODO: 8 for vtable: remove! 20=hack for missing paddings
    static constexpr int MAX_PAYLOAD_SIZE = max_size - sizeof(tag) - sizeof(data_size) - 8 - 20;
    static_assert(MAX_PAYLOAD_SIZE > 0);
    std::array<char, MAX_PAYLOAD_SIZE> data;

    [[nodiscard]] int free_space() const {
        return MAX_PAYLOAD_SIZE - data_size;
    }

    [[nodiscard]] size_t size() const {
        return reinterpret_cast<const char*>(&data) - reinterpret_cast<const char*>(this) + data_size;
    }
};
#pragma clang diagnostic pop

struct FuseReplyErr {
    FuseReplyErr(fuse_req_t r, int e)
        : req(r),
          error_code(e) {}
    const uint8_t tag = 5;
    fuse_req_t req;
    int error_code;
};
}  // namespace responses
}  // namespace remotefs::messages
#endif  // REMOTE_FS_MESSAGES_H
