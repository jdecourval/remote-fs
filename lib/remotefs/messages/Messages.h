#ifndef REMOTE_FS_MESSAGES_H
#define REMOTE_FS_MESSAGES_H

#include <fuse_lowlevel.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <span>

namespace remotefs::messages {
namespace both {
class Ping {
    [[maybe_unused]] const std::byte tag = std::byte{7};  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    int buffer_size;
    [[maybe_unused]] std::byte buffer[];  // NOLINT(cppcoreguidelines-avoid-c-arrays)

   public:
    static void* operator new(std::size_t, std::size_t total_size) {
        assert(total_size >= sizeof(Ping));
        return ::operator new (total_size, std::align_val_t{alignof(Ping)});
    }

    static void operator delete(void* ping) {
        ::operator delete (ping, std::align_val_t{alignof(Ping)});
    }

    [[nodiscard]] size_t size() const {
        return sizeof(*this) + buffer_size;
    }

    [[nodiscard]] std::span<const std::byte> view() const {
        return {reinterpret_cast<const std::byte*>(this), size()};
    }

    [[nodiscard]] std::span<std::byte> view() {
        return {reinterpret_cast<std::byte*>(this), size()};
    }

    explicit Ping(std::size_t total_size)
        : buffer_size{static_cast<int>(total_size - sizeof(Ping))} {};
};
}  // namespace both
namespace requests {
struct Open {
    const std::byte tag = std::byte{1};
    fuse_req_t req;
    fuse_ino_t ino;
    fuse_file_info file_info;
};

struct Lookup {
    std::byte tag = std::byte{2};
    fuse_req_t req;
    fuse_ino_t ino;
    char path[];
};

struct GetAttr {
    const std::byte tag = std::byte{3};
    fuse_req_t req;
    fuse_ino_t ino;
};

struct ReadDir {
    const std::byte tag = std::byte{4};
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Read {
    const std::byte tag = std::byte{5};
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Release {
    const std::byte tag = std::byte{6};
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

    const std::byte tag = std::byte{1};
    fuse_req_t req;
    fuse_entry_param attr;
};

struct FuseReplyAttr {
    FuseReplyAttr(auto r, auto a)
        : req{r},
          attr{a} {}

    const std::byte tag = std::byte{2};
    fuse_req_t req;
    struct stat attr;
};

struct FuseReplyOpen {
    FuseReplyOpen(auto r, auto f)
        : req{r},
          file_info(f) {}

    const std::byte tag = std::byte{3};
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

    const std::byte tag = std::byte{4};
    int data_size{};
    fuse_req_t req;
    // TODO: This is wrong, as it ignores padding
    // TODO: 8 for vtable: remove! 20=hack for missing paddings
    static constexpr int MAX_PAYLOAD_SIZE = max_size - sizeof(tag) - sizeof(data_size) - 8 - 20;
    static_assert(MAX_PAYLOAD_SIZE > 0);
    std::array<std::byte, MAX_PAYLOAD_SIZE> data;

    [[nodiscard]] int free_space() const {
        return MAX_PAYLOAD_SIZE - data_size;
    }

    [[nodiscard]] size_t size() const {
        return reinterpret_cast<const std::byte*>(&data) - reinterpret_cast<const std::byte*>(this) + data_size;
    }
};
#pragma clang diagnostic pop

struct FuseReplyErr {
    FuseReplyErr(fuse_req_t r, int e)
        : req(r),
          error_code(e) {}

    const std::byte tag = std::byte{5};
    fuse_req_t req;
    int error_code;
};
}  // namespace responses
}  // namespace remotefs::messages
#endif  // REMOTE_FS_MESSAGES_H
