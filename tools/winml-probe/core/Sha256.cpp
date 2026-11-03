#include "Sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <fstream>
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

Status sha256File(const std::filesystem::path& file, std::string& hexDigest) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return Status::fail(ErrorKind::ModelNotFound, "cannot open " + file.string());

    AlgHandle alg;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return Status::fail(ErrorKind::EngineFailure, "BCryptOpenAlgorithmProvider(SHA256) failed");
    HashHandle hash;
    // pbHashObject = nullptr: the provider allocates the hash object (bcrypt.h, Windows 7+).
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg.h, &hash.h, nullptr, 0, nullptr, 0, 0)))
        return Status::fail(ErrorKind::EngineFailure, "BCryptCreateHash failed");

    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        const auto n = (ULONG)in.gcount();
        if (n && !BCRYPT_SUCCESS(BCryptHashData(hash.h, reinterpret_cast<PUCHAR>(buf.data()), n, 0)))
            return Status::fail(ErrorKind::EngineFailure, "BCryptHashData failed");
    }
    if (in.bad()) return Status::fail(ErrorKind::ModelNotFound, "read error on " + file.string());

    unsigned char digest[32];
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.h, digest, sizeof digest, 0)))
        return Status::fail(ErrorKind::EngineFailure, "BCryptFinishHash failed");
    hexDigest.clear();
    char two[3];
    for (unsigned char b : digest) {
        std::snprintf(two, sizeof two, "%02x", b);
        hexDigest += two;
    }
    return Status::success();
}

}  // namespace arx::core
