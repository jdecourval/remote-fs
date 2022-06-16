#ifndef REMOTE_FS_SYSCALLS_H
#define REMOTE_FS_SYSCALLS_H

#include "MessageReceiver.h"

namespace quill {
class Logger;
}
namespace remotefs {

class Syscalls {
   public:
    Syscalls() noexcept;
    MessageReceiver lookup(MessageReceiver& message);
    MessageReceiver getattr(MessageReceiver& message);
    MessageReceiver readdir(MessageReceiver& message);
    MessageReceiver read(MessageReceiver& message);

   private:
    quill::Logger* logger;
};

}  // namespace remotefs

#endif  // REMOTE_FS_SYSCALLS_H
