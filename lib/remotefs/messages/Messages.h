#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-c-arrays"
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-const-or-ref-data-members"
#ifndef REMOTE_FS_MESSAGES_H
#define REMOTE_FS_MESSAGES_H

#include <fuse_lowlevel.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <filesystem>
#include <span>

#include "remotefs/tools/Bytes.h"
#include "remotefs/tools/Casts.h"

namespace remotefs::messages {
namespace both {

template <auto PingSize>
class Ping {
    union {
        struct {
            [[maybe_unused]] const std::byte tag = std::byte{7};
            size_t runtime_size;
        };

        std::array<std::byte, PingSize> _padding;
    };

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
        static_assert(sizeof(Ping) == PingSize);
        assert(runtime_size <= _padding.size());
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

template <size_t FuseReplyBufSize>
struct FuseReplyBuf {
    explicit FuseReplyBuf(auto r, size_t size = max_payload_size())
        : free_space{narrow_cast<decltype(free_space)>(size)},
          req{r} {
        static_assert(sizeof(FuseReplyBuf) == FuseReplyBufSize);
        assert(free_space <= narrow_cast<long>(max_payload_size()));
    }

    union {
        struct {
            [[maybe_unused]] const std::byte tag = std::byte{4};
            int payload_size = 0;
            int free_space;  // TODO: Do not transfer over the network.
            fuse_req_t req;
            std::byte payload[];
        };

        std::array<std::byte, FuseReplyBufSize> _padding;
    };

    void set_size(int size) {
        payload_size = size;
        free_space = 0;
    }

    std::span<char> read_view() {
        return {reinterpret_cast<char*>(payload), narrow_cast<size_t>(payload_size)};
    }

    std::span<std::byte> write_view() {
        return {reinterpret_cast<std::byte*>(payload + payload_size), narrow_cast<size_t>(free_space)};
    }

    std::span<std::byte> outer_view() {
        return singular_bytes(*this).subspan(0, offsetof(FuseReplyBuf, payload) + payload_size);
    }

    bool add_directory_entry(const std::filesystem::path& path, const struct stat& stats, off_t offset) {
        auto view = write_view();

        // fuse_req_t is ignored (1st parameter)
        auto entry_size = fuse_add_direntry(
            nullptr, reinterpret_cast<char*>(view.data()), view.size(), path.filename().c_str(), &stats, offset
        );
        if (entry_size <= narrow_cast<size_t>(free_space)) {
            payload_size += entry_size;
            free_space -= entry_size;
            return true;
        }

        return false;
    }

    static constexpr size_t max_payload_size() {
        return sizeof(FuseReplyBuf) - offsetof(FuseReplyBuf, payload);
    }
};

struct FuseReplyErr {
    FuseReplyErr(fuse_req_t r, int e)
        : req(r),
          error_code(e) {}

    [[maybe_unused]] const std::byte tag = std::byte{5};
    fuse_req_t req;
    int error_code;
};
}  // namespace responses
}  // namespace remotefs::messages
#endif  // REMOTE_FS_MESSAGES_H

#pragma clang diagnostic pop
