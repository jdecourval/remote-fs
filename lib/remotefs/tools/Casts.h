#ifndef REMOTE_FS_CASTS_H
#define REMOTE_FS_CASTS_H
#include <cassert>
#include <limits>
#include <utility>

namespace remotefs {

template <class T, class U>
constexpr T narrow_cast(U&& u) noexcept {
    assert(u <= std::numeric_limits<T>::max());
    assert(u >= std::numeric_limits<T>::min());
    return static_cast<T>(std::forward<U>(u));
}

template <class T, class U>
inline T polymorphic_downcast(U* x) {
    assert(dynamic_cast<T>(x) == x);
    return static_cast<T>(x);
}

template <class T, class U>
inline T polymorphic_downcast(U&& x) {
    dynamic_cast<T>(x);
    return static_cast<T>(x);
}

}  // namespace remotefs

#endif  // REMOTE_FS_CASTS_H
