#ifndef REMOTE_FS_GETADDRINFOERRORCATEGORY_H
#define REMOTE_FS_GETADDRINFOERRORCATEGORY_H

#include <netdb.h>

#include <string>
#include <system_error>

class GetAddrInfoErrorCategory : public std::error_category {
    [[nodiscard]] const char* name() const noexcept override {
        return "getaddrinfo";
    }
    [[nodiscard]] std::string message(int i) const override {
        return gai_strerror(i);
    }
};

#endif  // REMOTE_FS_GETADDRINFOERRORCATEGORY_H
