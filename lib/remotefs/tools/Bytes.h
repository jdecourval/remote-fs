#ifndef REMOTE_FS_BYTES_H
#define REMOTE_FS_BYTES_H

#include <span>

template <class T>
std::span<T, 1> singular_span(T& t) {
    return std::span<T, 1>{std::addressof(t), 1};
}

template <class T>
auto singular_bytes(const T& t) {
    return std::as_bytes(singular_span(t));
}

template <class T>
auto singular_bytes(T& t) {
    return std::as_writable_bytes(singular_span(t));
}

template <std::unsigned_integral T>
inline constexpr auto mask_out(T value, unsigned mask) {
    return value & (std::numeric_limits<T>::max() ^ mask);
}

// From https://stackoverflow.com/a/72128846

#endif  // REMOTE_FS_BYTES_H
