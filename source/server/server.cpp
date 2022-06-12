#include "server.h"
#include "mylib/lib.h"

#include <zmqpp/context.hpp>
#include <zmqpp/message.hpp>
#include <zmqpp/socket.hpp>

#include <filesystem>
#include <fts.h>
#include <fuse3/fuse_lowlevel.h>
#include <optional>

namespace remotefs
{
class InodeFinder
{
  public:
    explicit InodeFinder() : file_system{fts_open(path, FTS_PHYSICAL | FTS_NOSTAT | FTS_XDEV, nullptr), fts_close}
    {
    }

    std::optional<struct stat> stats(ino64_t inode)
    {
        for (auto file = fts_read(file_system.get()); file != nullptr; file = fts_read(file_system.get()))
        {
            if (file->fts_ino == inode && file->fts_info & FTS_D)
            {
                struct stat result
                {
                };
                if (stat(file->fts_path, &result) < 0)
                {
                    throw std::runtime_error(std::strerror(errno));
                }
                std::cout << "Found file: " << file->fts_path << " for ino: " << inode << std::endl;
                return result;
            }
        }

        return {};
    }

  private:
    char *path[2] = {".", nullptr};
    std::unique_ptr<FTS, decltype(&fts_close)> file_system;
};

server::server() : socket(context, zmqpp::socket_type::xreply)
{
}

void server::start(const std::string &address)
{
#ifndef NDEBUG
    socket.set(zmqpp::socket_option::router_mandatory, true);
#endif
    std::cout << "Binding to " << address << "..." << std::endl;
    socket.bind(address);

    while (true)
    {
        zmqpp::message message;
        zmqpp::message sendmsg;
        socket.receive(message);
        auto op = static_cast<remotefs::FuseOp>(message.get<std::underlying_type_t<remotefs::FuseOp>>(1));
        std::cout << "Received " << +op << " with " << message.parts() << " parts\n";
        sendmsg.add(message.get(0), message.get(1), message.get(2));

        switch (op)
        {
        case LOOKUP: {
            auto result = fuse_entry_param{};
            if (stat(message.get(4).c_str(), &result.attr) >= 0)
            {
                result.ino = result.attr.st_ino;
                result.generation = 0;
                result.attr_timeout = 1;
                result.entry_timeout = 1;
                sendmsg.add_raw(&result, sizeof(result));
                std::cout << "stat succeeded for : " << message.get(4) << ": " << result.ino << std::endl;
            }
            else
            {
                std::cout << "stat failed for : " << message.get(4) << ": " << std::strerror(errno) << std::endl;
            }
            break;
        }
        case GETATTR: {
            auto ino = message.get<fuse_ino_t>(3);
            std::cout << "getattr: " << ino << std::endl;

            if (ino == 1u)
            {
                struct stat result
                {
                };
                if (stat(".", &result) < 0)
                {
                    throw std::runtime_error(std::strerror(errno));
                }
                sendmsg.add_raw(&result, sizeof(result));
            }
            else if (auto stats = InodeFinder().stats(ino); stats)
            {
                sendmsg.add_raw(&stats.value(), sizeof(stats.value()));
            }
            break;
        }
        case READDIR: {
            // Probably not important to return a valid inode number
            // https://fuse-devel.narkive.com/L338RZTz/lookup-readdir-and-inode-numbers
            auto ino = message.get<fuse_ino_t>(3);
            auto size = message.get<size_t>(4);
            auto off = message.get<off_t>(5);
            auto fi = reinterpret_cast<const struct fuse_file_info *>(message.raw_data(6));
            auto key = fi->fh;
            std::array<char, 1024> buffer;
            std::cout << "Received readdir with size " << size << " and offset " << off << std::endl;
            auto entry_number = 1;

            if (off > 0)
            {
                break;
            }

            {
                auto permissions = std::filesystem::status(".").permissions();
                struct stat stbuf
                {
                };
                stbuf.st_ino = 1; // This is of course wrong
                stbuf.st_mode = std::to_underlying(permissions);
                auto entry_size = fuse_add_direntry(nullptr, buffer.data(), buffer.size(), ".", &stbuf, entry_number++);
                std::cout << "Adding " << entry_size << " bytes" << std::endl;
                sendmsg.add_raw(buffer.data(), entry_size);
            }

            {
                auto permissions = std::filesystem::status("..").permissions();
                struct stat stbuf
                {
                };
                stbuf.st_ino = 1; // This is of course wrong
                stbuf.st_mode = std::to_underlying(permissions);
                auto entry_size =
                    fuse_add_direntry(nullptr, buffer.data(), buffer.size(), "..", &stbuf, entry_number++);
                std::cout << "Adding " << entry_size << " bytes" << std::endl;
                sendmsg.add_raw(buffer.data(), entry_size);
            }

            for (const auto &entry : std::filesystem::directory_iterator{std::filesystem::path{"."}})
            {
                auto filename = entry.path().filename();
                auto permissions = entry.status().permissions();
                struct stat stbuf
                {
                };
                stbuf.st_ino = 2; // This is of course wrong
                stbuf.st_mode = std::to_underlying(permissions);
                // fuse_req_t is ignored
                auto entry_size =
                    fuse_add_direntry(nullptr, buffer.data(), buffer.size(), filename.c_str(), &stbuf, entry_number++);
                if (entry_size > buffer.size())
                {
                    throw std::runtime_error("Buffer too small");
                }

                // This does a copy
                std::cout << "Adding " << entry_size << " bytes" << std::endl;
                sendmsg.add_raw(buffer.data(), entry_size);
            }

            break;
        }
        case OPEN: {
            assert(false);
            break;
        }
        case READ: {
            auto ino = message.get<fuse_ino_t>(3);
            auto size = message.get<size_t>(4);
            auto off = message.get<off_t>(5);
            auto fi = reinterpret_cast<const struct fuse_file_info *>(message.raw_data(6));
            auto key = fi->fh;
            //            std::ifstream ()
            assert(false);
            break;
        }
            //        case OPENDIR: {
            //            // Return some number, add the open directory_entry to a cache
            //        }
        }
        socket.send(sendmsg);
    }
}
} // namespace remotefs
