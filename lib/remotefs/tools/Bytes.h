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

template <typename T, typename U>
long long struct_size_after_this_member(const T* self, const U& member) {
    return sizeof(member) + (reinterpret_cast<const std::byte*>(&member) - reinterpret_cast<const std::byte*>(self));
}

// From https://stackoverflow.com/a/72128846

#endif  // REMOTE_FS_BYTES_H