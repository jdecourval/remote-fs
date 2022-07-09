#ifndef REMOTE_FS_MESSAGEWRAPPERS_H
#define REMOTE_FS_MESSAGEWRAPPERS_H

#include <cstring>
#include <span>
#include <zmqpp/message.hpp>

#include "remotefs/tools/FuseOp.h"

class fuse_req;
using fuse_req_t = fuse_req*;

namespace remotefs {
/// Includes the route
template <int read_offset = 0>
class MessageWrappers : public zmqpp::message {
   public:
    using zmqpp::message::message;

    template <typename T, typename... Args>
    MessageWrappers(T const& part, Args&&... args)
        : message() {
        add(part, std::forward<Args>(args)...);
    }

    template <typename T>
    T& get_usr_data(size_t user_data_idx) {
        assert(sizeof(T) == size(user_data_idx + read_offset + 2));
        return *const_cast<T*>(reinterpret_cast<const T*>(raw_data(user_data_idx + read_offset + 2)));
    }

    template <typename T>
    T copy_usr_data(size_t user_data_idx) {
        assert(sizeof(T) == size(user_data_idx + read_offset + 2));
        T data;
        std::memcpy(&data, raw_data(user_data_idx + read_offset + 2), size(user_data_idx + read_offset + 2));
        return data;
    }

    void* raw_usr_data(size_t user_data_idx) {
        return const_cast<void*>(raw_data(user_data_idx + read_offset + 2));
    }

    std::string_view get_usr_data_string(size_t user_data_idx) {
        return {const_cast<char*>(reinterpret_cast<const char*>(raw_data(user_data_idx + read_offset + 2))),
                size(user_data_idx + read_offset + 2)};
    }

    FuseOp op() {
        return static_cast<FuseOp>(get<std::underlying_type_t<FuseOp>>(read_offset));
    }

    [[nodiscard]] fuse_req_t req() {
        return reinterpret_cast<fuse_req_t>(get<uint64_t>(read_offset + 1));
    }

    [[nodiscard]] size_t usr_data_parts() const {
        return parts() - read_offset - 2;
    }

    [[nodiscard]] bool empty() const {
        return parts() == 0;
    }

    [[nodiscard]] size_t usr_data_size(size_t part) const {
        return size(part + read_offset + 2);
    }

    template <typename... Args>
    MessageWrappers respond(Args&&... args);

    template <typename... Args>
    MessageWrappers* respond_new(Args&&... args, void* location = nullptr);

    void add() {}

    template <typename... Args>
    void add(const char* text, Args&&... args) {
        *this << text;
        add(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    void add(T* data, Args&&... args) {
        if (data != nullptr) add_raw(data, sizeof(*data));
        add(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    void add(T const& part, Args&&... args) {
        static_assert(!std::is_pointer_v<T>);
        *this << part;
        add(std::forward<Args>(args)...);
    }

   private:
    using zmqpp::message::add;
    using zmqpp::message::get;
    using zmqpp::message::parts;
    using zmqpp::message::raw_data;
    using zmqpp::message::size;
};

template <>
template <typename... Args>
MessageWrappers<1> MessageWrappers<1>::respond(Args&&... args) {
    auto msg = MessageWrappers();
    msg.template add_raw(raw_data(0), size(0));
    msg.template add_raw(raw_data(1), size(1));
    msg.template add_raw(raw_data(2), size(2));
    if constexpr (sizeof...(args)) {
        msg.template add(std::forward<Args>(args)...);
    }
    return msg;
}

template <>
template <typename... Args>
MessageWrappers<1>* MessageWrappers<1>::respond_new(Args&&... args, void* location) {
    auto msg = location == nullptr ? new MessageWrappers() : new (location) MessageWrappers();
    msg->template add_raw(raw_data(0), size(0));
    msg->template add_raw(raw_data(1), size(1));
    msg->template add_raw(raw_data(2), size(2));
    if constexpr (sizeof...(args)) {
        msg->template add(std::forward<Args>(args)...);
    }
    return msg;
}

using MessageTransmitter = MessageWrappers<0>;
using MessageReceiver = MessageWrappers<1>;  /// Includes the route

}  // namespace remotefs
#endif  // REMOTE_FS_MESSAGEWRAPPERS_H
