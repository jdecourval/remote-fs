#ifndef REMOTE_FS_SYSCALLS_H
#define REMOTE_FS_SYSCALLS_H

#include <memory>
#include <optional>

#include "Config.h"
#include "remotefs/inodecache/InodeCache.h"
#include "remotefs/messages/Messages.h"

namespace quill {
class Logger;
}

namespace remotefs {
class IoUring;

class Syscalls {
    using FuseReplyBuf = messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>;

   public:
    explicit Syscalls(IoUring& ring);
    void open(messages::requests::Open& message, int socket);
    void lookup(messages::requests::Lookup& message, int socket);
    void getattr(messages::requests::GetAttr& message, int socket);
    void readdir(messages::requests::ReadDir& message, int socket);
    void read(messages::requests::Read& message, int socket);
    void release(messages::requests::Release& message);
    void ping(std::unique_ptr<std::array<std::byte, settings::MAX_MESSAGE_SIZE>>&& buffer, int socket);

   private:
    quill::Logger* logger;
    IoUring& uring;
    InodeCache inode_cache;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SYSCALLS_H
