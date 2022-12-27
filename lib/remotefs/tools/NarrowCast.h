#ifndef REMOTE_FS_NARROWCAST_H
#define REMOTE_FS_NARROWCAST_H
#include <cassert>
#include <limits>
#include <utility>

namespace remotefs {

template <class T, class U>
constexpr T narrow_cast(U&& u) noexcept {
    assert(u <= std::numeric_limits<T>::max());
    return static_cast<T>(std::forward<U>(u));
}

}  // namespace remotefs

#endif  // REMOTE_FS_NARROWCAST_H
