#ifndef REMOTE_FS_SYSCALLS_H
#define REMOTE_FS_SYSCALLS_H

#include <optional>

#include "IoUring.h"
#include "remotefs/inodecache/InodeCache.h"
#include "remotefs/tools/MessageWrappers.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Syscalls {
    using IoUringImpl = IoUring<MessageReceiver>;

   public:
    Syscalls();
    MessageReceiver open(MessageReceiver& message);
    std::optional<MessageReceiver> lookup(MessageReceiver& message, IoUringImpl& uring);
    MessageReceiver getattr(MessageReceiver& message);
    MessageReceiver readdir(MessageReceiver& message);
    void read(MessageReceiver& message, IoUringImpl& uring);
    MessageReceiver release(MessageReceiver& message);

   private:
    quill::Logger* logger;
    InodeCache inode_cache;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SYSCALLS_H
