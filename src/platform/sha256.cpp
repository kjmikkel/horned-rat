#include "platform/sha256.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <fstream>
#include <vector>

namespace hr::platform {

std::optional<std::string> sha256_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::nullopt;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::string> result;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buffer(1 << 16);
        bool ok = true;
        while (ok && in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<ULONG>(in.gcount());
            if (n > 0) {
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), n, 0) == 0;
            }
        }
        std::array<UCHAR, 32> digest{};
        if (ok &&
            BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string hex;
            for (const UCHAR byte : digest) {
                hex += kHex[byte >> 4];
                hex += kHex[byte & 0xF];
            }
            result = hex;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

} // namespace hr::platform
