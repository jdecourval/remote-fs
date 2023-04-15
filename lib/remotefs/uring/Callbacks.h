#ifndef REMOTE_FS_CALLBACKS_H
#define REMOTE_FS_CALLBACKS_H

#include <iostream>
#include <memory>

#include "RegisteredBufferCache.h"

namespace remotefs {
class IoUring;

static constexpr auto buffers_alignment = 8;
static constexpr auto buffers_size = 2097152;

CachedRegisteredBuffersResource<buffers_size>& get_pool();

class CallbackErased {
   public:
    virtual ~CallbackErased() = default;
    CallbackErased(const CallbackErased& copyFrom) = delete;
    CallbackErased& operator=(const CallbackErased& copyFrom) = delete;
    CallbackErased(CallbackErased&&) = delete;
    CallbackErased& operator=(CallbackErased&&) = delete;

   protected:
    CallbackErased() = default;

   private:
    friend IoUring;
    virtual void operator()(int res) = 0;
    virtual void operator()(int res, std::unique_ptr<CallbackErased>) = 0;
};

template <typename Storage>
class CallbackWithStorageAbstract : public CallbackErased {
   public:
    virtual Storage& get_storage() = 0;
    virtual const Storage& get_storage() const = 0;
    [[nodiscard]] virtual short get_index() const = 0;
};

}  // namespace remotefs

// FIXME: This is only compatible with Clang
template <std::derived_from<remotefs::CallbackErased> Callback>
struct std::default_delete<Callback> {
    default_delete() = default;
    /// Random ID used to uniquely identify this struct for assertions.
    [[maybe_unused]] constexpr static const int is_proper_deleter = 893725892;

    template <class U>
    constexpr default_delete(default_delete<U>) noexcept {}

    void operator()(remotefs::CallbackErased* ptr) const noexcept {
        std::pmr::polymorphic_allocator<remotefs::CallbackErased>(&remotefs::get_pool()).delete_object(ptr);
    }
};

#endif  // REMOTE_FS_CALLBACKS_H
