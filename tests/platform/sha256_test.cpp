#include "platform/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using hr::platform::sha256_file;

TEST_CASE("sha256_file hashes a file's bytes", "[sha256]") {
    const auto path = std::filesystem::temp_directory_path() / "hr_sha256_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "abc";
    }
    CHECK(sha256_file(path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(path);
}

TEST_CASE("sha256_file of a missing file is nullopt", "[sha256]") {
    CHECK_FALSE(sha256_file(std::filesystem::temp_directory_path() / "hr_no_such_file.bin"));
}
