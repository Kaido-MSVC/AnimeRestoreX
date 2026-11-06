// AnimeRestoreX v2.0 core (P2 prototype, no Qt). Error type shared by the core modules.
// Designed to move to src/core/ later; kept free of Qt and of ORT headers.
#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace arx::core {

enum class ErrorKind {
    None,
    InvalidArgument,    // bad caller input (empty image, unsupported channel count, ...)
    InvalidTileSize,    // tile size violates the model's alignment rule; rejected before any Run
    SizeOverflow,       // a size / allocation computation would overflow or exceed a limit
    ContractViolation,  // manifest or model I/O does not match the contract (shape, names, ...)
    HashMismatch,       // model file SHA-256 differs from the manifest
    ModelNotFound,      // model id or file missing
    Cancelled,          // user cancel (checked between tiles; CPU Run may be terminated)
    OutOfMemory,        // explicit E_OUTOFMEMORY from the runtime: rebuild sessions, one retry
    DeviceLost,         // DXGI 887A0005/0006/0007/0020: rebuild sessions, one retry
    EngineFailure,      // any other runtime error
};

const char* errorKindName(ErrorKind k);

struct Status {
    ErrorKind kind = ErrorKind::None;
    std::string message;
    uint32_t hresult = 0;  // HRESULT found in the runtime error, 0 if none

    bool ok() const { return kind == ErrorKind::None; }
    static Status success() { return {}; }
    static Status fail(ErrorKind k, std::string msg, uint32_t hr = 0) { return {k, std::move(msg), hr}; }
};

}  // namespace arx::core
