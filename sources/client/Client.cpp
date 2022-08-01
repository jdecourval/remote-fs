#include "Client.h"

#include <linux/dccp.h>
#include <netdb.h>
#include <quill/Quill.h>

#include <memory>

#include "Config.h"
#include "FuseCmdlineOptsWrapper.h"

namespace remotefs {

Client::Client(int argc, char *argv[])
    : logger(quill::get_logger()) {
    auto args = fuse_args{argc, argv, 0};
    auto options = FuseCmdlineOptsWrapper(args);

    if (options.show_help) {
        printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
        fuse_cmdline_help();
        fuse_lowlevel_help();
        return;
    }

    if (options.show_version) {
        printf("FUSE library version %s\n", fuse_pkgversion());
        fuse_lowlevel_version();
        return;
    }

    if (options.mountpoint == nullptr) {
        printf("usage: %s [options] <mountpoint>\n", argv[0]);
        printf("       %s --help\n", argv[0]);
        throw std::logic_error("Failed to parse mount point");
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const struct fuse_lowlevel_ops fuse_ops = {
        .init =
            [](void *, struct fuse_conn_info *conn) {
                //                conn->want |= ((FUSE_CAP_SPLICE_MOVE | FUSE_CAP_SPLICE_WRITE) & conn->capable);
                conn->max_background = 100;
                conn->max_readahead = 1024 * 1024 * 1024;
                conn->max_read = messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>::MAX_PAYLOAD_SIZE;
                conn->max_write = messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE>::MAX_PAYLOAD_SIZE;
            },
        .lookup =
            [](fuse_req_t req, fuse_ino_t parent, const char *name) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending lookup for {}/{}, req={}", parent, name,
                             reinterpret_cast<uintptr_t>(req));
                // TODO: Error check
                auto message_size = sizeof(messages::requests::Lookup) + strnlen(name, PATH_MAX) + 1;
                auto message = std::unique_ptr<messages::requests::Lookup, void (*)(messages::requests::Lookup *)>{
                    static_cast<messages::requests::Lookup *>(operator new(message_size)),
                    [](auto *ptr) { operator delete(ptr); }};
                message->tag = 2;
                message->ino = parent;
                message->req = req;
                // TODO: Error check
                strcpy(message->path, name);
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), message_size};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int) mutable {});
            },
        .getattr =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending getattr, req={}", reinterpret_cast<uintptr_t>(req));
                auto message =
                    std::make_unique<messages::requests::GetAttr>(messages::requests::GetAttr{.req = req, .ino = ino});
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), sizeof(*message)};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int32_t) mutable {});
            },
        .open =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending open, req={}", reinterpret_cast<uintptr_t>(req));
                auto message = std::make_unique<messages::requests::Open>(
                    messages::requests::Open{.req = req, .ino = ino, .file_info = *fi});
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), sizeof(*message)};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int32_t) mutable {});
            },
        .read =
            [](fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending read for {} of size {}, req={}", ino, size,
                             reinterpret_cast<uintptr_t>(req));
                auto message = std::make_unique<messages::requests::Read>(
                    messages::requests::Read{.req = req, .ino = ino, .size = size, .offset = off});
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), sizeof(*message)};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int32_t) mutable {});
            },
        .release =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending release for {}", ino);
                auto message =
                    std::make_unique<messages::requests::Release>(messages::requests::Release{.req = req, .ino = ino});
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), sizeof(*message)};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int32_t) mutable {});
            },
        .readdir =
            [](fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *) {
                auto &client = *static_cast<Client *>(fuse_req_userdata(req));
                LOG_TRACE_L1(client.logger, "Sending readdir for {} with off {} and size {}, req=", ino, off, size,
                             reinterpret_cast<uint64_t>(req));
                auto message = std::make_unique<messages::requests::ReadDir>(
                    messages::requests::ReadDir{.req = req, .ino = ino, .size = size, .offset = off});
                auto message_view = std::span{reinterpret_cast<char *>(message.get()), sizeof(*message)};
                client.io_uring.write(client.socket, message_view, [message = std::move(message)](int32_t) mutable {});
            },
    };
#pragma GCC diagnostic pop

    if ((fuse_session = fuse_session_new(&args, &fuse_ops, sizeof(fuse_ops), this)) == nullptr) {
        throw std::logic_error("Failed to create fuse session");
    }

    if (fuse_set_signal_handlers(fuse_session) != 0) {
        throw std::logic_error("Failed to set fuse signal handler");
    }

    if (fuse_session_mount(fuse_session, options.mountpoint) != 0) {
        throw std::logic_error("Failed to create fuse mount point");
    }

    foreground = options.foreground;
    fuse_opt_free_args(&args);
}

void Client::read_callback(int syscall_ret, std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>> &&buffer) {
    auto buffer_view = std::span{buffer->data(), buffer->size()};

    if (syscall_ret < 0) {
        LOG_ERROR(logger, "Read failed: {}", std::strerror(-syscall_ret));
        io_uring.read(socket, buffer_view, 0, [this, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
            read_callback(syscall_ret, std::move(buffer));
        });
        return;
    }

    if (syscall_ret == 0) {
        LOG_INFO(logger, "Read NULL message");
        io_uring.read(socket, buffer_view, 0, [this, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
            read_callback(syscall_ret, std::move(buffer));
        });
        return;
    }

    switch (buffer->at(0)) {
        case 1: {
            auto *msg = reinterpret_cast<const messages::responses::FuseReplyEntry *>(buffer->data());
            LOG_DEBUG(logger, "Received FuseReplyEntry, ino={}, req={}", msg->attr.ino,
                      reinterpret_cast<uint64_t>(msg->req));

            if (auto ret = fuse_reply_entry(msg->req, &msg->attr); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_entry failure");
            }
            break;
        }
        case 2: {
            auto &msg = *reinterpret_cast<const messages::responses::FuseReplyAttr *>(buffer->data());
            LOG_DEBUG(logger, "Received FuseReplyAttr, req={}", reinterpret_cast<uint64_t>(msg.req));
            if (auto ret = fuse_reply_attr(msg.req, &msg.attr, 1.0); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_attr failure");
            }
            break;
        }
        case 3: {
            auto &msg = *reinterpret_cast<const messages::responses::FuseReplyOpen *>(buffer->data());
            LOG_DEBUG(logger, "Received FuseReplyOpen, req={}", reinterpret_cast<uint64_t>(msg.req));
            if (auto ret = fuse_reply_open(msg.req, &msg.file_info); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_open failure");
            }
            break;
        }
        case 4: {
            auto &msg =
                *reinterpret_cast<messages::responses::FuseReplyBuf<settings::MAX_MESSAGE_SIZE> *>(buffer->data());
            //                            assert(msg.size() <= settings::MAX_MESSAGE_SIZE);
            LOG_DEBUG(logger, "Received FuseReplyBuf, req={}, size={}", reinterpret_cast<uint64_t>(msg.req),
                      msg.data_size);
            auto buf_vec = fuse_bufvec{.count = 1,
                                       .idx = 0,
                                       .off = 0,
                                       .buf = {{.size = static_cast<size_t>(msg.data_size),
                                                .flags = static_cast<fuse_buf_flags>(0),
                                                .mem = msg.data.data()}}};
            if (auto ret = fuse_reply_data(msg.req, &buf_vec, FUSE_BUF_SPLICE_MOVE); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_data failure");
            }
            break;
        }
        case 5: {
            auto &msg = *reinterpret_cast<const messages::responses::FuseReplyErr *>(buffer->data());
            LOG_WARNING(logger, "Received error for req {}: {}", reinterpret_cast<uint64_t>(msg.req),
                        std::strerror(msg.error_code));
            if (auto ret = fuse_reply_err(msg.req, msg.error_code); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_err failure");
            }
            break;
        }
        default:
            assert(false);
    }
    io_uring.read(socket, buffer_view, 0, [this, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
        read_callback(syscall_ret, std::move(buffer));
    });
}

void Client::fuse_callback(int syscall_ret, std::unique_ptr<char[]> &&buffer, size_t bufsize) {
    if (syscall_ret <= 0 && syscall_ret != -EINTR) {
        throw std::system_error(-syscall_ret, std::generic_category(), "fuse reading failure");
    } else if (syscall_ret <= 0) {
        return;
    }
    auto fuse_buffer = fuse_buf{.size = static_cast<size_t>(syscall_ret), .mem = buffer.get()};
    fuse_session_process_buf(fuse_session, &fuse_buffer);
    auto buffer_view = std::span{buffer.get(), bufsize};
    io_uring.read(fuse_session_fd(fuse_session), buffer_view, 0,
                  [this, bufsize, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                      fuse_callback(syscall_ret, std::move(buffer), bufsize);
                  });
}

void Client::start(const std::string &address) {
    class GetAddrInfoErrorCategory : public std::error_category {
        [[nodiscard]] const char *name() const noexcept override {
            return "getaddrinfo";
        }
        [[nodiscard]] std::string message(int i) const override {
            return gai_strerror(i);
        }
    };

    LOG_INFO(logger, "Opening connection to {}", address);
    struct addrinfo hosthints {
        .ai_family = AF_INET, .ai_socktype = SOCK_DCCP, .ai_protocol = IPPROTO_DCCP
    };
    struct addrinfo *hostinfo;  // TODO: unique_ptr with freeaddrinfo
    if (auto ret = getaddrinfo(address.c_str(), "5001", &hosthints, &hostinfo); ret < 0) {
        throw std::system_error(ret, GetAddrInfoErrorCategory(), "Failed to resolve address");
    }

    socket = ::socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol);
    if (socket < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to configure socket");
    }

    if (connect(socket, hostinfo->ai_addr, hostinfo->ai_addrlen) < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to connect socket");
    }
    int mps;
    socklen_t mps_size = sizeof(mps);
    if (getsockopt(socket, SOL_DCCP, DCCP_SOCKOPT_GET_CUR_MPS, &mps, &mps_size) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to get current maximum packet size");
    }
    LOG_INFO(logger, "Maximum packet size: {}", mps);

    fuse_daemonize(foreground);

    for (auto i = 0; i < 2; i++) {
        const auto FUSE_BUFFER_HEADER_SIZE = 0x1000;
        const auto FUSE_MAX_MAX_PAGES = 256;
        auto page_size = sysconf(_SC_PAGESIZE);
        const auto bufsize = static_cast<size_t>(FUSE_MAX_MAX_PAGES * page_size + FUSE_BUFFER_HEADER_SIZE);
        auto buffer = std::unique_ptr<char[]>{new char[bufsize]};
        auto buffer_view = std::span{buffer.get(), bufsize};
        io_uring.read(fuse_session_fd(fuse_session), buffer_view, 0,
                      [this, bufsize, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
                          fuse_callback(syscall_ret, std::move(buffer), bufsize);
                      });
    }
    {
        auto buffer = std::unique_ptr<std::array<char, settings::MAX_MESSAGE_SIZE>>{
            new (std::align_val_t(16)) std::array<char, settings::MAX_MESSAGE_SIZE>()};
        assert(reinterpret_cast<uintptr_t>(buffer.get()) % 8 == 0);
        auto buffer_view = std::span{buffer->data(), buffer->size()};
        io_uring.read(socket, buffer_view, 0, [this, buffer = std::move(buffer)](int32_t syscall_ret) mutable {
            read_callback(syscall_ret, std::move(buffer));
        });
    }

    while (!fuse_session_exited(fuse_session)) {
        io_uring.queue_wait();
    }

    LOG_INFO(logger, "Done");
}

Client::~Client() {
    fuse_session_unmount(fuse_session);
    fuse_remove_signal_handlers(fuse_session);
    fuse_session_destroy(fuse_session);
}
}  // namespace remotefs
