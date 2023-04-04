#ifndef REMOTE_FS_REGISTEREDBUFFERCACHE_H
#define REMOTE_FS_REGISTEREDBUFFERCACHE_H

#include <algorithm>
#include <cassert>
#include <memory_resource>
#include <ranges>

#include "IoUring.h"

namespace remotefs {
class IoUring;

template <auto BuffersDataSize>
class CachedRegisteredBuffersResource final : public std::pmr::memory_resource {
    static_assert(BuffersDataSize > 0);

   public:
    explicit CachedRegisteredBuffersResource(int buffer_count = 64) {
        assert(buffer_count > 0);
        for (auto i = 0; i < buffer_count; i++) {
            buffers_cache.emplace_back(i);
        }
        buffers_cache.shrink_to_fit();
        const auto available_bits = std::numeric_limits<decltype(active_registered_buffers)>::digits;
        assert(buffer_count <= available_bits);
        // If the value of the right operand (of the shift) is greater or equal to the number of bits in the
        // promoted left operand, the behavior is undefined
        active_registered_buffers =
            buffer_count == 0 ? 0 : ~decltype(active_registered_buffers){} >> (available_bits - buffer_count);
        assert(std::ranges::all_of(buffers_cache, [](const auto& buffer) {
            return &buffer == static_cast<const void*>(buffer.data.data());
        }));
    }

    short get_index(const void* ptr) const {
#ifndef NDEBUG
        assert(ptr >= &buffers_cache.front().data.front());
        assert(ptr < &buffers_cache.back().data.back());
#endif
        return static_cast<const Buffer*>(ptr)->index;
    }

    auto view() const {
        // Many ranges functions are broken on clang < 16... LLVM issue #44178.
        return buffers_cache | std::views::transform([](const Buffer& buffer) {
                   return std::pair{buffer.index, std::span{buffer.data}};
               });
    }

   private:
    void* do_allocate(size_t bytes, size_t alignment) final {
        auto index = std::countr_zero(active_registered_buffers);  // 0-indexed

        if (index >= std::numeric_limits<decltype(active_registered_buffers)>::digits) {
            throw std::bad_alloc();  // No more index available
        }

        assert(active_registered_buffers & (0b1ull << index));
        active_registered_buffers ^= 0b1ull << index;
        auto& buffer = buffers_cache[index].data;
        auto max_size = buffer.size();
        auto unaligned_ptr = static_cast<void*>(buffer.data());
        auto result = std::align(alignment, bytes, unaligned_ptr, max_size);
        if (result == nullptr) {
            throw std::bad_alloc();  // Pre-allocated buffers are too small to serve this request.
        }
        return result;
    }

    void do_deallocate(void* pointer, size_t, size_t) final {
        assert(!(active_registered_buffers & (0b1ull << reinterpret_cast<Buffer*>(pointer)->index)));
        active_registered_buffers ^= 0b1ull << reinterpret_cast<Buffer*>(pointer)->index;
    }

    [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept final {
        if (auto* other_res = dynamic_cast<decltype(this)>(&other); other_res != nullptr) {
            return buffers_cache.size() == other_res->buffers_cache.size() &&
                   buffers_cache.front().data.data() == other_res->buffers_cache.front().data.data();
        }

        return false;
    }

    struct Buffer {
        explicit Buffer(short index)
            : index{index} {}

        alignas(8) std::array<std::byte, BuffersDataSize> data;
        short index;
    };

    unsigned long long active_registered_buffers = 0;
    std::vector<Buffer> buffers_cache;
};

}  // namespace remotefs

#endif  // REMOTE_FS_REGISTEREDBUFFERCACHE_H
