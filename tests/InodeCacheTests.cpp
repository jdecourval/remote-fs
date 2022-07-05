#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "remotefs/inodecache/InodeCache.h"

thread_local auto random_64 =
    std::mt19937_64{static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count())};

class InSandbox {
   public:
    InSandbox() {
        try {
            previous_path = std::filesystem::current_path();
        } catch (const std::filesystem::filesystem_error&) {
            previous_path = std::filesystem::temp_directory_path();
        }

        temporary_path = std::filesystem::temp_directory_path() / std::to_string(random_64());
        std::filesystem::create_directory(temporary_path);
        std::filesystem::current_path(temporary_path);
    }

    ~InSandbox() {
        std::filesystem::current_path(previous_path);
        std::filesystem::remove_all(temporary_path);
    }

   private:
    std::filesystem::path previous_path;
    std::filesystem::path temporary_path;
};

std::filesystem::path create_file() {
    auto path = std::filesystem::path{std::to_string(random_64())};
    auto file = std::ofstream{path};
    file << "\n";
    file.close();
    return path;
}

TEST_CASE("InodeCache") {
    auto in_sandbox = InSandbox{};
    auto inode_cache = remotefs::InodeCache{};

    SUBCASE("lookup returns nullptr for missing paths") {
        REQUIRE(inode_cache.lookup("missing") == nullptr);
    }

    SUBCASE("lookup returns a valid inode for .") {
        auto inode = inode_cache.lookup(".");
        REQUIRE(inode != nullptr);
        REQUIRE(inode->first == ".");
        REQUIRE(inode->second.st_ino == 1);
    }

    SUBCASE("lookup creates a single inode per path") {
        auto inode_1 = *inode_cache.lookup(".");
        auto inode_2 = *inode_cache.lookup(".");
        REQUIRE(inode_1.second.st_ino == inode_2.second.st_ino);
    }

    SUBCASE("lookup returns a valid inode for a file") {
        auto file = create_file();
        auto inode = inode_cache.lookup(file.string());
        REQUIRE(inode != nullptr);
        REQUIRE(inode->first == file);
    }

    SUBCASE("lookup returns a valid inode for a directory") {
        std::filesystem::create_directory("directory");
        auto inode = inode_cache.lookup("directory");
        REQUIRE(inode != nullptr);
        REQUIRE(inode->first == "directory");
    }

    //    SUBCASE("inode_from_ino throws for missing ino") {
    // #pragma clang diagnostic push
    // #pragma clang diagnostic ignored "-Wunused-result"
    //        REQUIRE_THROWS_AS(inode_cache.inode_from_ino(0), std::out_of_range);
    //        REQUIRE_THROWS_AS(inode_cache.inode_from_ino(1), std::out_of_range);
    //        REQUIRE_THROWS_AS(inode_cache.inode_from_ino(10), std::out_of_range);
    // #pragma clang diagnostic pop
    //    }

    SUBCASE("lookup caches an inode that can be found by inode_from_ino") {
        auto inode_lookup = inode_cache.lookup(".");
        auto inode_from_ino = inode_cache.inode_from_ino(inode_lookup->second.st_ino);
        REQUIRE(inode_lookup != nullptr);
        REQUIRE(inode_lookup->first == inode_from_ino.first);
        REQUIRE(inode_lookup->second.st_ino == inode_from_ino.second.st_ino);
    }
}
