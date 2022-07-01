#ifndef REMOTE_FS_MESSAGERECEIVER_H
#define REMOTE_FS_MESSAGERECEIVER_H

#include <zmqpp/message.hpp>

#include "remotefs/tools/FuseOp.h"

namespace remotefs {
/// Includes the route
class MessageReceiver : public zmqpp::message {
   public:
    using zmqpp::message::message;

    template <typename T>
    T& get_usr_data(size_t user_data_idx) {
        assert(sizeof(T) == size(user_data_idx + 3));
        return *const_cast<T*>(reinterpret_cast<const T*>(raw_data(user_data_idx + 3)));
    }

    std::string_view get_usr_data_string(size_t user_data_idx) {
        return {const_cast<char*>(reinterpret_cast<const char*>(raw_data(user_data_idx + 3))), size(user_data_idx + 3)};
    }

    FuseOp op() {
        return static_cast<FuseOp>(get<std::underlying_type_t<FuseOp>>(1));
    }

    template <typename... Args>
    MessageReceiver respond(Args&&... args) {
        auto msg = MessageReceiver();
        msg.template add_raw(raw_data(0), size(0));
        msg.template add_raw(raw_data(1), size(1));
        msg.template add_raw(raw_data(2), size(2));
        if constexpr (sizeof...(args)) {
            msg.template add(std::forward<Args>(args)...);
        }
        return msg;
    }
};

}  // namespace remotefs
#endif  // REMOTE_FS_MESSAGERECEIVER_H
