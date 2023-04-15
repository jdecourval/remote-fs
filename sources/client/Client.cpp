#include "Client.h"

#include <fuse3/fuse_kernel.h>
#include <netdb.h>
#include <quill/Quill.h>
#include <sys/ioctl.h>

#include <memory>

#include "Config.h"
#include "FuseCmdlineOptsWrapper.h"

namespace remotefs {
thread_local Client *Client::self;
std::atomic_flag Client::common_init_done;
fuse_session *Client::static_fuse_session = nullptr;

Client::Client(int argc, char *argv[])
    : logger(quill::get_logger()),
      fuse_fd{0} {
    assert(self == nullptr);
    self = this;
    static std::once_flag flag;
    std::call_once(flag, &Client::common_init, this, argc, argv);
    common_init_done.wait(false);
    fuse_session = static_fuse_session;
    if (fuse_fd == 0) {
        if (fuse_fd = open("/dev/fuse", O_RDWR | O_CLOEXEC); fuse_fd == -1) {
            throw std::system_error(errno, std::generic_category(), "Failed to open slave fuse device");
        }
        fcntl(fuse_fd, F_SETFD, FD_CLOEXEC);
        auto master_fd = fuse_session_fd(fuse_session);
        if (ioctl(fuse_fd, FUSE_DEV_IOC_CLONE, &master_fd) == -1) {
            throw std::system_error(errno, std::generic_category(), "Failed to clone fuse device");
        }
        LOG_INFO(logger, "Initialized with fd={}, master={}", fuse_fd, master_fd);
    }
    fuse_channel = {
        .ctr = std::numeric_limits<decltype(fuse_chan::ctr)>::max(),
        .fd = fuse_fd,
    };
    pthread_mutex_init(&fuse_channel.lock, nullptr);

    io_uring.register_ring();
    io_uring.register_sparse_files(64);
    //    io_uring.assign_file((fuse_uring_idx = 0), fuse_fd);

    assert(sysconf(_SC_PAGESIZE) == PAGE_SIZE);
}

template <auto BufferSize>
void Client::fuse_reply_data(
    std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, BufferSize>>> old_callback
) {
    auto &msg = *reinterpret_cast<FuseReplyBuf *>(old_callback->get_storage().data());

    LOG_DEBUG(logger, "Received FuseReplyBuf, req={}, size={}", static_cast<void *>(msg.req), msg.payload_size);
    [[maybe_unused]] auto ret = fuse_reply_buf(msg.req, msg.read_view().data(), msg.read_view().size());
    assert(ret == 0);
}

template <auto BufferSize>
void Client::read_callback(
    int syscall_ret, std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, BufferSize>>> old_callback
) {
    auto callable = [this](int32_t syscall_ret, auto callback) { read_callback(syscall_ret, std::move(callback)); };

    if (syscall_ret < 0) {
        LOG_ERROR(logger, "Read failed: {}", std::strerror(-syscall_ret));
        io_uring.read_fixed(socket, 0, std::move(callable));
        return;
    }

    if (syscall_ret == 0) {
        LOG_INFO(logger, "Read NULL message");
        io_uring.read_fixed(socket, 0, std::move(callable));
        return;
    }

    switch (old_callback->get_storage()[0]) {
        case std::byte{1}: {
            auto *msg =
                reinterpret_cast<const messages::responses::FuseReplyEntry *>(old_callback->get_storage().data());
            LOG_DEBUG(
                logger, "Received FuseReplyEntry, ino={}, req={}, fd={}, size={}", msg->attr.ino,
                static_cast<void *>(msg->req), msg->req->ch->fd, msg->attr.attr.st_size
            );

            if (auto ret = fuse_reply_entry(msg->req, &msg->attr); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_entry failure");
            }
            break;
        }
        case std::byte{2}: {
            auto &msg =
                *reinterpret_cast<const messages::responses::FuseReplyAttr *>(old_callback->get_storage().data());
            LOG_DEBUG(
                logger, "Received FuseReplyAttr, req={}, fd={}, fd={}", static_cast<void *>(msg.req), fuse_fd,
                msg.req->ch->fd
            );
            if (auto ret = fuse_reply_attr(msg.req, &msg.attr, 1.0); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_attr failure");
            }
            break;
        }
        case std::byte{3}: {
            auto &msg =
                *reinterpret_cast<const messages::responses::FuseReplyOpen *>(old_callback->get_storage().data());

            LOG_DEBUG(logger, "Received FuseReplyOpen, req={}", static_cast<void *>(msg.req));
            if (auto ret = fuse_reply_open(msg.req, &msg.file_info); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_open failure");
            }
            break;
        }
        case std::byte{4}: {
            fuse_reply_data(std::move(old_callback));
            break;
        }
        case std::byte{5}: {
            auto &msg =
                *reinterpret_cast<const messages::responses::FuseReplyErr *>(old_callback->get_storage().data());
            LOG_WARNING(
                logger, "Received error for req {}: {}", static_cast<void *>(msg.req), std::strerror(msg.error_code)
            );
            if (auto ret = fuse_reply_err(msg.req, msg.error_code); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "fuse_reply_err failure");
            }
            break;
        }
        default:
            assert(false);
    }

    io_uring.read_fixed(socket, 0, std::move(callable));
}

void Client::fuse_callback(
    int syscall_ret, std::unique_ptr<CallbackWithStorageAbstract<std::array<std::byte, FUSE_REQUEST_SIZE>>> callback
) {
    LOG_TRACE_L2(logger, "Fuse callback: {}", syscall_ret);
    if (syscall_ret <= 0 && syscall_ret != -EINTR) {
        throw std::system_error(-syscall_ret, std::generic_category(), "fuse reading failure");
    } else if (syscall_ret <= 0) {
        return;
    }
    auto fuse_buffer = fuse_buf{.size = static_cast<size_t>(syscall_ret), .mem = callback->get_storage().data()};
    // fuse_session_process_buf_int could be called with fuse_channel, but this method is not exposed by libfuse.
    auto backup_fd = fuse_session->fd;
    fuse_session->fd = fuse_fd;
    fuse_session_process_buf(fuse_session, &fuse_buffer);
    fuse_session->fd = backup_fd;
    auto view = std::span{callback->get_storage()};
    //    io_uring.read_fixed(fuse_fd, view, std::move(callback));
    io_uring.read(fuse_fd, view, 0, std::move(callback));
}

void Client::start(const std::string &address) {
    socket = remotefs::Socket::connect(
        address, 6512,
        {.rx_buffer_size = 10 * settings::MAX_MESSAGE_SIZE,
         .tx_buffer_size = 10 * settings::MAX_MESSAGE_SIZE,
         .delivery_point = settings::MAX_MESSAGE_SIZE}
    );
    //    io_uring.assign_file((socket_uring_idx = 1), socket);

    for (auto i = 0; i < 1; i++) {
        auto callback =
            io_uring.get_callback<std::array<std::byte, FUSE_REQUEST_SIZE>>([this](int32_t syscall_ret, auto callback) {
                fuse_callback(syscall_ret, std::move(callback));
            });
        auto view = std::span{callback->get_storage()};
        io_uring.read(fuse_fd, view, 0, std::move(callback));
    }
    {
        io_uring.read_fixed(socket, 0, [this](int32_t syscall_ret, auto callback) {
            read_callback(syscall_ret, std::move(callback));
        });
    }

    while (!fuse_session_exited(fuse_session)) {
        io_uring.queue_wait();
    }

    LOG_INFO(logger, "Done");
}

Client::~Client() {
    static std::once_flag flag;
    std::call_once(
        flag,
        [](auto fuse_session) {
            fuse_session_unmount(fuse_session);
            fuse_remove_signal_handlers(fuse_session);
            fuse_session_destroy(fuse_session);
        },
        fuse_session
    );
}

void Client::common_init(int argc, char *argv[]) {
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
                conn->max_background = std::numeric_limits<decltype(conn->max_background)>::max();
                conn->max_readahead = std::numeric_limits<decltype(conn->max_readahead)>::max();
                conn->max_read = FUSE_MAX_MAX_PAGES * PAGE_SIZE;
                conn->max_write = FUSE_MAX_MAX_PAGES * PAGE_SIZE;
            },
        .lookup =
            [](fuse_req_t req, fuse_ino_t parent, const char *name) {
                auto &client = *Client::self;
                LOG_TRACE_L1(client.logger, "Sending lookup for {}/{}, req={}", parent, name, static_cast<void *>(req));
                auto callback = client.io_uring.get_callback<messages::requests::Lookup>([](int) {});
                callback->get_storage().tag = std::byte{2};
                callback->get_storage().ino = parent;
                callback->get_storage().req = req;
                callback->get_storage().req->ch = &client.fuse_channel;
                strcpy(callback->get_storage().path.data(), name);
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
        .getattr =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *) {
                auto &client = *Client::self;

                LOG_TRACE_L1(client.logger, "Sending getattr, req={}, fd={}", static_cast<void *>(req), client.fuse_fd);
                auto callback = client.io_uring.get_callback<messages::requests::GetAttr>([](int) {});
                callback->get_storage().req = req;
                callback->get_storage().ino = ino;
                callback->get_storage().req->ch = &client.fuse_channel;
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
        .open =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
                auto &client = *Client::self;
                LOG_TRACE_L1(client.logger, "Sending open, req={}", static_cast<void *>(req));
                auto callback = client.io_uring.get_callback<messages::requests::Open>([](int) {});
                callback->get_storage().req = req;
                callback->get_storage().ino = ino;
                callback->get_storage().file_info = *fi;
                callback->get_storage().req->ch = &client.fuse_channel;
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
        .read =
            [](fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *) {
                auto &client = *Client::self;
                LOG_TRACE_L1(
                    client.logger, "Sending read for {} of size {}, req={}", ino, size, static_cast<void *>(req)
                );
                auto callback = client.io_uring.get_callback<messages::requests::Read>([](int) {});
                callback->get_storage().req = req;
                callback->get_storage().ino = ino;
                callback->get_storage().size = size;
                callback->get_storage().offset = off;
                callback->get_storage().req->ch = &client.fuse_channel;
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
        .release =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *) {
                auto &client = *Client::self;
                LOG_TRACE_L1(client.logger, "Sending release for {}", ino);
                auto callback = client.io_uring.get_callback<messages::requests::Release>([](int) {});
                callback->get_storage().req = req;
                callback->get_storage().ino = ino;
                callback->get_storage().req->ch = &client.fuse_channel;
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
        .readdir =
            [](fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *) {
                auto &client = *Client::self;
                LOG_TRACE_L1(
                    client.logger, "Sending readdir for {} with off {} and size {}, req=", ino, off, size,
                    static_cast<void *>(req)
                );
                auto callback = client.io_uring.get_callback<messages::requests::ReadDir>([](int) {});
                callback->get_storage().req = req;
                callback->get_storage().ino = ino;
                callback->get_storage().size = size;
                callback->get_storage().offset = off;
                callback->get_storage().req->ch = &client.fuse_channel;
                client.io_uring.write_fixed(client.socket, std::move(callback));
            },
    };
#pragma GCC diagnostic pop

    if ((static_fuse_session = fuse_session_new(&args, &fuse_ops, sizeof(fuse_ops), nullptr)) == nullptr) {
        throw std::logic_error("Failed to create fuse session");
    }

    if (fuse_set_signal_handlers(static_fuse_session) != 0) {
        throw std::logic_error("Failed to set fuse signal handler");
    }

    if (fuse_session_mount(static_fuse_session, options.mountpoint) != 0) {
        throw std::logic_error("Failed to create fuse mount point");
    }

    fuse_opt_free_args(&args);

    fuse_fd = fuse_session_fd(static_fuse_session);
    LOG_INFO(logger, "Common init done");
    common_init_done.test_and_set();
    common_init_done.notify_all();
}
}  // namespace remotefs
