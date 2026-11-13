#include "Sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <format>
#include <fstream>
#include <iterator>
#include <vector>

namespace arx::core {

namespace {
struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};
struct HashHandle {
    BCRYPT_HASH_HANDLE h = nullptr;
    ~HashHandle() { if (h) BCryptDestroyHash(h); }
};
}  // namespace

std::expected<std::string, Status> sha256File(const std::filesystem::path& file) {
    auto fail = [](ErrorKind k, std::string msg) { return std::unexpected(Status::fail(k, std::move(msg))); };
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(ErrorKind::ModelNotFound, "cannot open " + file.string());

    AlgHandle alg;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return fail(ErrorKind::EngineFailure, "BCryptOpenAlgorithmProvider(SHA256) failed");
    HashHandle hash;
    // pbHashObject = nullptr: the provider allocates the hash object (bcrypt.h, Windows 7+).
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg.h, &hash.h, nullptr, 0, nullptr, 0, 0)))
        return fail(ErrorKind::EngineFailure, "BCryptCreateHash failed");

    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        const auto n = (ULONG)in.gcount();
        if (n && !BCRYPT_SUCCESS(BCryptHashData(hash.h, reinterpret_cast<PUCHAR>(buf.data()), n, 0)))
            return fail(ErrorKind::EngineFailure, "BCryptHashData failed");
    }
    if (in.bad()) return fail(ErrorKind::ModelNotFound, "read error on " + file.string());

    unsigned char digest[32];
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.h, digest, sizeof digest, 0)))
        return fail(ErrorKind::EngineFailure, "BCryptFinishHash failed");
    std::string hex;
    hex.reserve(2 * sizeof digest);
    for (unsigned char b : digest) std::format_to(std::back_inserter(hex), "{:02x}", b);
    return hex;
}

}  // namespace arx::core
