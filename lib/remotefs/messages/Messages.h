#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-const-or-ref-data-members"
#ifndef REMOTE_FS_MESSAGES_H
#define REMOTE_FS_MESSAGES_H

#include <fuse_lowlevel.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <span>

#include "remotefs/tools/Bytes.h"

namespace remotefs::messages {
namespace both {

template <auto MaxPaddingSize = 0, auto Alignment = 1>
class Ping {
    [[maybe_unused]] const std::byte tag = std::byte{7};  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    size_t runtime_size;
    // Increasing the alignment is only useful if the user wish to store a structure inside padding.
    [[maybe_unused]] alignas(Alignment) std::array<std::byte, MaxPaddingSize> padding;

   public:
    [[nodiscard]] size_t size() const {
        return runtime_size;
    }

    [[nodiscard]] std::span<const std::byte> view() const {
        return {reinterpret_cast<const std::byte*>(this), size()};
    }

    [[nodiscard]] std::span<std::byte> view() {
        return {reinterpret_cast<std::byte*>(this), size()};
    }

    explicit Ping(size_t runtime_size)
        : runtime_size{runtime_size} {
        assert(runtime_size <= padding.size());
    }
};
}  // namespace both

namespace requests {
struct Open {
    [[maybe_unused]] const std::byte tag = std::byte{1};
    fuse_req_t req;
    fuse_ino_t ino;
    fuse_file_info file_info;
};

struct Lookup {
    [[maybe_unused]] std::byte tag = std::byte{2};
    fuse_req_t req;
    fuse_ino_t ino;
    std::array<char, PATH_MAX + 1> path;

    std::span<std::byte> view() {
        auto end = std::ranges::find(path, '\0');
        assert(end != path.end());
        return std::span{reinterpret_cast<std::byte*>(this), reinterpret_cast<std::byte*>(std::next(end))};
    }
};

struct GetAttr {
    [[maybe_unused]] const std::byte tag = std::byte{3};
    fuse_req_t req;
    fuse_ino_t ino;
};

struct ReadDir {
    [[maybe_unused]] const std::byte tag = std::byte{4};
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Read {
    [[maybe_unused]] const std::byte tag = std::byte{5};
    fuse_req_t req;
    fuse_ino_t ino;
    size_t size;
    off_t offset;
};

struct Release {
    [[maybe_unused]] const std::byte tag = std::byte{6};
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

    [[maybe_unused]] const std::byte tag = std::byte{1};
    fuse_req_t req;
    fuse_entry_param attr;
};

struct FuseReplyAttr {
    FuseReplyAttr(auto r, auto a)
        : req{r},
          attr{a} {}

    [[maybe_unused]] const std::byte tag = std::byte{2};
    fuse_req_t req;
    struct stat attr;
};

struct FuseReplyOpen {
    FuseReplyOpen(auto r, auto f)
        : req{r},
          file_info(f) {}

    [[maybe_unused]] const std::byte tag = std::byte{3};
    fuse_req_t req;
    fuse_file_info file_info;
};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"

template <size_t max_size = 65017>
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

    [[nodiscard]] size_t transmit_size() const {
        return reinterpret_cast<const std::byte*>(&data) - reinterpret_cast<const std::byte*>(this) + data_size;
    }

    static_assert(MAX_PAYLOAD_SIZE > 0);
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

#pragma clang diagnostic pop
