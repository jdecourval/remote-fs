#include "Client.h"

#include <quill/Quill.h>

#include <zmqpp/context.hpp>
#include <zmqpp/message.hpp>
#include <zmqpp/poller.hpp>
#include <zmqpp/reactor.hpp>
#include <zmqpp/socket.hpp>

#include "FuseCmdlineOptsWrapper.h"
#include "config.h"
#include "remotefs/FuseOp.h"

namespace remotefs {
struct fuse_bufvec {
    decltype(::fuse_bufvec::count) count;
    decltype(::fuse_bufvec::idx) idx;
    decltype(::fuse_bufvec::off) off;
    std::array<fuse_buf, settings::client::FUSE_READ_BUFFER_SIZE - sizeof(count) - sizeof(idx) - sizeof(off)> buf;
};
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
        .open =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
                LOG_TRACE_L1(quill::get_logger(), "Called open on ino {}", ino);
                if ((fi->flags & O_ACCMODE) != O_RDONLY) {
                    fuse_reply_err(req, EACCES);
                } else {
                    fuse_reply_open(req, fi);
                };
            },
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

    zmqpp::message message;
    auto poll = zmqpp::poller();
    poll.add(fuse_session_fd(fuse_session), socket);

    auto reactor = zmqpp::reactor();
    reactor.add(socket, [&] {
        socket.receive(message);
        auto op = static_cast<remotefs::FuseOp>(message.get<std::underlying_type_t<remotefs::FuseOp>>(0));
        auto req = reinterpret_cast<fuse_req_t>(message.get<uint64_t>(1));
        LOG_TRACE_L1(logger, "Received message {}", static_cast<int>(op));

        switch (op) {
            case LOOKUP:
                if (message.parts() == 3) {
                    if (fuse_reply_entry(req, static_cast<const fuse_entry_param *>(message.raw_data(2))) < 0) {
                        throw std::runtime_error("fuse_reply_entry failed");
                    }
                } else {
                    LOG_TRACE_L1(logger, "Received lookup failure");
                    fuse_reply_err(req, ENOENT);
                }
                break;
            case GETATTR:
                if (message.parts() == 3) {
                    if (fuse_reply_attr(req, reinterpret_cast<const struct stat *>(message.raw_data(2)), 1.0) < 0) {
                        throw std::runtime_error("fuse_reply_attr failed");
                    }
                } else {
                    LOG_TRACE_L1(logger, "Received getattr failure");
                    fuse_reply_err(req, ENOENT);
                }
                break;
            case READ:
            case READDIR: {
                LOG_TRACE_L1(logger, "Received read or readdir in {} parts", message.parts() - 2);
                fuse_bufvec bufvec{};
                bufvec.count = message.parts() - 2;  // Two headers

                if (bufvec.count == 0) {
                    fuse_reply_buf(req, nullptr, 0);
                    break;
                }

                bufvec.idx = 0;
                bufvec.off = 0;
                assert(bufvec.count <= bufvec.buf.size());
                for (auto i = 0u; i < bufvec.count; i++) {
                    bufvec.buf[i].mem = const_cast<void *>(message.raw_data(i + 2));
                    bufvec.buf[i].size = message.size(i + 2);
                }

                if (fuse_reply_data(req, reinterpret_cast<::fuse_bufvec *>(&bufvec), FUSE_BUF_SPLICE_MOVE) < 0) {
                    throw std::runtime_error("fuse_reply_data failed");
                }
                break;
            }

            case OPEN:
                throw std::logic_error("Not implemented");
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
    auto message = zmqpp::message(std::uint8_t{op}, reinterpret_cast<uintptr_t>(req), args...);
    static_cast<remotefs::Client *>(fuse_req_userdata(req))->socket.send(message);
    LOG_TRACE_L1(quill::get_logger(), "Sent {}", static_cast<int>(op));
}
}  // namespace remotefs
