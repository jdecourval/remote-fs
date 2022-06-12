#include "client.h"

#include <zmqpp/context.hpp>
#include <zmqpp/message.hpp>
#include <zmqpp/poller.hpp>
#include <zmqpp/reactor.hpp>
#include <zmqpp/socket.hpp>

#include "mylib/lib.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
struct fuse_cmdline_opts_cpp : public fuse_cmdline_opts
{
    fuse_cmdline_opts_cpp() : fuse_cmdline_opts{.mountpoint = nullptr}
    {
    }
    ~fuse_cmdline_opts_cpp()
    {
        free(mountpoint);
    }
};
#pragma GCC diagnostic pop

namespace remotefs
{
struct fuse_bufvec
{
    decltype(::fuse_bufvec::count) count;
    decltype(::fuse_bufvec::idx) idx;
    decltype(::fuse_bufvec::off) off;
    std::array<fuse_buf, 256 - sizeof(count) - sizeof(idx) - sizeof(off)> buf;
};
client::client(int argc, char *argv[]) : socket(context, zmqpp::socket_type::xrequest), args{argc, argv, 0}
{
    struct fuse_cmdline_opts_cpp opts; // NOLINT(cppcoreguidelines-pro-type-member-init)

    if (fuse_parse_cmdline(&args, &opts) != 0)
        throw std::logic_error("Failed to parse command line");

    if (opts.show_help)
    {
        printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
        fuse_cmdline_help();
        fuse_lowlevel_help();
        return;
    }
    else if (opts.show_version)
    {
        printf("FUSE library version %s\n", fuse_pkgversion());
        fuse_lowlevel_version();
        return;
    }

    if (opts.mountpoint == nullptr)
    {
        printf("usage: %s [options] <mountpoint>\n", argv[0]);
        printf("       %s --help\n", argv[0]);
        throw std::logic_error("Failed to parse mount point");
    }

    const struct fuse_lowlevel_ops fuse_ops = {
        .init = nullptr,
        .destroy = nullptr,
        .lookup = [](auto... ts) { remotefs::sendcall(FuseOp::LOOKUP, ts...); },
        .forget = nullptr,
        .getattr = [](auto... ts) { remotefs::sendcall(FuseOp::GETATTR, ts...); },
        .setattr = nullptr,
        .readlink = nullptr,
        .mknod = nullptr,
        .mkdir = nullptr,
        .unlink = nullptr,
        .rmdir = nullptr,
        .symlink = nullptr,
        .rename = nullptr,
        .link = nullptr,
        .open =
            [](fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
                std::cout << "Called open on " << ino << std::endl;
                if ((fi->flags & O_ACCMODE) != O_RDONLY)
                {
                    fuse_reply_err(req, EACCES);
                }
                else
                {
                    fuse_reply_open(req, fi);
                };
            },
        .read = [](auto... ts) { remotefs::sendcall(FuseOp::READ, ts...); },
        .write = nullptr,
        .flush = nullptr,
        .release = nullptr,
        .fsync = nullptr,
        .opendir = nullptr,
        .readdir = [](auto... ts) { remotefs::sendcall(FuseOp::READDIR, ts...); },
        .releasedir = nullptr,
        .fsyncdir = nullptr,
        .statfs = nullptr,
        .setxattr = nullptr,
        .getxattr = nullptr,
        .listxattr = nullptr,
        .removexattr = nullptr,
        .access = nullptr,
        .create = nullptr,
        .getlk = nullptr,
        .setlk = nullptr,
        .bmap = nullptr,
        .ioctl = nullptr,
        .poll = nullptr,
        .write_buf = nullptr,
        .retrieve_reply = nullptr,
        .forget_multi = nullptr,
        .flock = nullptr,
        .fallocate = nullptr,
        .readdirplus = nullptr,
        .copy_file_range = nullptr,
        .lseek = nullptr};

    fuse_session = fuse_session_new(&args, &fuse_ops, sizeof(fuse_ops), this);
    if (fuse_session == nullptr)
        throw std::logic_error("Failed to create fuse session");

    if (fuse_set_signal_handlers(fuse_session) != 0)
        throw std::logic_error("Failed to set fuse signal handler");

    if (fuse_session_mount(fuse_session, opts.mountpoint) != 0)
        throw std::logic_error("Failed to create fuse mount point");

    foreground = opts.foreground;
}

void client::start(const std::string &address)
{
    // open the connection
    std::cout << "Opening connection to " << address << "..." << std::endl;
    socket.connect(address);

    //        fuse_daemonize(foreground);

    //        fuse_session_loop(fuse_session);
    zmqpp::message message;
    auto poll = zmqpp::poller();
    poll.add(fuse_session_fd(fuse_session), socket);

    auto reactor = zmqpp::reactor();
    reactor.add(socket, [&] {
        socket.receive(message);
        std::cout << "Received message" << std::endl;
        auto op = static_cast<remotefs::FuseOp>(message.get<std::underlying_type_t<remotefs::FuseOp>>(0));
        auto req = reinterpret_cast<fuse_req_t>(message.get<uint64_t>(1));

        switch (op)
        {
        case LOOKUP:
            if (message.parts() == 3)
            {
                std::cout << "Received lookup success, ino="
                          << static_cast<const fuse_entry_param *>(message.raw_data(2))->ino << std::endl;
                if (fuse_reply_entry(req, static_cast<const fuse_entry_param *>(message.raw_data(2))) < 0)
                {
                    throw std::runtime_error("fuse_reply_entry failed");
                }
            }
            else
            {
                std::cout << "Received lookup failure" << std::endl;
                fuse_reply_err(req, ENOENT);
            }
            break;
        case GETATTR:
            if (message.parts() == 3)
            {
                std::cout << "Received getattr: " << message.size(2) << std::endl;
                if (fuse_reply_attr(req, reinterpret_cast<const struct stat *>(message.raw_data(2)), 1.0) < 0)
                {
                    throw std::runtime_error("fuse_reply_attr failed");
                }
            }
            else
            {
                std::cout << "Received getattr failure" << std::endl;
                fuse_reply_err(req, ENOENT);
            }
            break;
        case READ:
        case READDIR: {
            std::cout << "Received read or read dir (" << static_cast<int>(op) << ") in " << message.parts() - 2
                      << " parts" << std::endl;
            fuse_bufvec bufvec{};
            bufvec.count = message.parts() - 2;

            if (bufvec.count == 0)
            {
                fuse_reply_buf(req, nullptr, 0);
                break;
            }

            bufvec.idx = 0;
            bufvec.off = 0;
            assert(bufvec.count <= bufvec.buf.size());
            for (auto i = 0u; i < bufvec.count; i++)
            {
                bufvec.buf[i].mem = const_cast<void *>(message.raw_data(i + 2));
                bufvec.buf[i].size = message.size(i + 2);
            }

            if (fuse_reply_data(req, reinterpret_cast<::fuse_bufvec *>(&bufvec), FUSE_BUF_SPLICE_MOVE) < 0)
            {
                throw std::runtime_error("fuse_reply_data failed");
            }
            break;
        }
        case OPEN:
            assert(false);
        }
        std::cout << "done" << std::endl;
    });

    reactor.add(fuse_session_fd(fuse_session), [&] {
        struct fuse_buf fbuf = {
            .mem = nullptr,
        };

        auto res = fuse_session_receive_buf(fuse_session, &fbuf);

        if (res <= 0 && res != -EINTR)
            throw std::runtime_error("?");

        fuse_session_process_buf(fuse_session, &fbuf);
    });

    while (!fuse_session_exited(fuse_session))
    {
        reactor.poll();
    }
}

client::~client()
{
    fuse_session_unmount(fuse_session);
    fuse_remove_signal_handlers(fuse_session);
    fuse_session_destroy(fuse_session);
    // TODO: Can this be freed in the constructor?
    fuse_opt_free_args(&args);
}

template <typename... Ts> void sendcall(FuseOp op, fuse_req_t req, Ts... args)
{
    zmqpp::message message;
    // TODO: Write the op to the pointer's lower 4 bits?
    message.template add(std::uint8_t{op}, reinterpret_cast<uintptr_t>(req), args...);
    static_cast<remotefs::client *>(fuse_req_userdata(req))->socket.send(message);
    std::cout << "Sent" << std::endl;
}
} // namespace remotefs
