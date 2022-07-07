#include "Client.h"

#include <quill/Quill.h>

#include <zmqpp/context.hpp>
#include <zmqpp/poller.hpp>
#include <zmqpp/reactor.hpp>
#include <zmqpp/socket.hpp>

#include "FuseBufVec.h"
#include "FuseCmdlineOptsWrapper.h"
#include "config.h"
#include "remotefs/tools/FuseOp.h"
#include "remotefs/tools/MessageWrappers.h"

namespace remotefs {

Client::Client(int argc, char *argv[])
    : logger(quill::get_logger()),
      socket(context, zmqpp::socket_type::xrequest) {
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
        .lookup = [](auto... ts) { remotefs::sendcall(FuseOp::LOOKUP, ts...); },
        .getattr = [](auto... ts) { remotefs::sendcall(FuseOp::GETATTR, ts...); },
        .open = [](auto... ts) { remotefs::sendcall(FuseOp::OPEN, ts...); },
        .read = [](auto... ts) { remotefs::sendcall(FuseOp::READ, ts...); },
        .readdir = [](auto... ts) { remotefs::sendcall(FuseOp::READDIR, ts...); },
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

void Client::start(const std::string &address) {
    // open the connection
    LOG_INFO(logger, "Opening connection to {}", address);
    socket.connect(address);

    fuse_daemonize(foreground);

    MessageTransmitter message;
    auto poll = zmqpp::poller();
    poll.add(fuse_session_fd(fuse_session), socket);

    auto reactor = zmqpp::reactor();
    reactor.add(socket, [&] {
        socket.receive(message);
        LOG_TRACE_L1(logger, "Received message {}", static_cast<int>(message.op()));

        switch (message.op()) {
            case LOOKUP:
                if (message.usr_data_parts() == 1) {
                    auto result = message.copy_usr_data<struct fuse_entry_param>(0);
                    if (fuse_reply_entry(message.req(), &result) < 0) {
                        throw std::runtime_error("fuse_reply_entry failed");
                    }
                } else {
                    LOG_TRACE_L1(logger, "Received lookup failure");
                    fuse_reply_err(message.req(), ENOENT);
                }
                break;
            case GETATTR:
                if (message.usr_data_parts() == 1) {
                    auto result = message.copy_usr_data<struct stat>(0);
                    if (fuse_reply_attr(message.req(), &result, 1.0) < 0) {
                        throw std::runtime_error("fuse_reply_attr failed");
                    }
                } else {
                    LOG_TRACE_L1(logger, "Received getattr failure");
                    fuse_reply_err(message.req(), ENOENT);
                }
                break;
            case READ:
            case READDIR: {
                LOG_TRACE_L1(logger, "Received read or readdir in {} parts", message.usr_data_parts());
                auto bufvec = FuseBufVec<settings::client::FUSE_READ_BUFFER_SIZE>{};
                bufvec.count = message.usr_data_parts();

                if (bufvec.count == 0) {
                    fuse_reply_buf(message.req(), nullptr, 0);
                    break;
                }

                bufvec.idx = 0;
                bufvec.off = 0;

                if (bufvec.count <= bufvec.buf.size()) {
                    // Use the stack for small buffers
                    for (auto i = 0u; i < bufvec.count; i++) {
                        bufvec.buf[i].mem = message.raw_usr_data(i);
                        bufvec.buf[i].size = message.usr_data_size(i);
                    }

                    if (fuse_reply_data(message.req(), reinterpret_cast<::fuse_bufvec *>(&bufvec),
                                        FUSE_BUF_SPLICE_MOVE) < 0) {
                        throw std::runtime_error("fuse_reply_data failed");
                    }
                } else {
                    LOG_TRACE_L2(logger, "Buffer is too small, allocating");
                    auto buffer = std::unique_ptr<fuse_bufvec, decltype(&free)>(
                        reinterpret_cast<fuse_bufvec *>(malloc(sizeof(fuse_bufvec) + bufvec.count * sizeof(fuse_buf))),
                        free);

                    buffer->count = bufvec.count;
                    buffer->idx = 0;
                    buffer->off = 0;

                    for (auto i = 0u; i < buffer->count; i++) {
                        buffer->buf[i].mem = &message.get_usr_data<char>(i);
                        buffer->buf[i].size = message.usr_data_size(i);
                    }

                    if (fuse_reply_data(message.req(), buffer.get(), FUSE_BUF_SPLICE_MOVE) < 0) {
                        throw std::runtime_error("fuse_reply_data failed");
                    }
                }

                break;
            }

            case OPEN:
                if (message.usr_data_parts() == 0) {
                    fuse_reply_err(message.req(), EINVAL);
                } else {
                    auto result = message.copy_usr_data<fuse_file_info>(0);
                    fuse_reply_open(message.req(), &result);
                }
        }
    });

    reactor.add(fuse_session_fd(fuse_session), [&] {
        auto fbuf = fuse_buf{};
        auto res = fuse_session_receive_buf(fuse_session, &fbuf);

        if (res <= 0 && res != -EINTR) {
            throw std::runtime_error("?");
        }

        fuse_session_process_buf(fuse_session, &fbuf);
    });

    while (!fuse_session_exited(fuse_session)) {
        reactor.poll();
    }

    LOG_INFO(logger, "Done");
}

Client::~Client() {
    fuse_session_unmount(fuse_session);
    fuse_remove_signal_handlers(fuse_session);
    fuse_session_destroy(fuse_session);
}

template <typename... Ts>
void sendcall(FuseOp op, fuse_req_t req, Ts... args) {
    auto message = MessageTransmitter(std::uint8_t{op}, reinterpret_cast<uintptr_t>(req), args...);
    static_cast<remotefs::Client *>(fuse_req_userdata(req))->socket.send(message);
    LOG_TRACE_L1(quill::get_logger(), "Sent {}", static_cast<int>(op));
}
}  // namespace remotefs
