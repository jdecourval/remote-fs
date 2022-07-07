#ifndef REMOTE_FS_SYSCALLS_H
#define REMOTE_FS_SYSCALLS_H

#include "remotefs/inodecache/InodeCache.h"
#include "remotefs/tools/MessageWrappers.h"

namespace quill {
class Logger;
}

namespace remotefs {

class Syscalls {
   public:
    Syscalls() noexcept;
    MessageReceiver open(MessageReceiver& message);
    MessageReceiver lookup(MessageReceiver& message);
    MessageReceiver getattr(MessageReceiver& message);
    MessageReceiver readdir(MessageReceiver& message);
    MessageReceiver read(MessageReceiver& message);

   private:
    quill::Logger* logger;
    InodeCache inode_cache;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SYSCALLS_H
