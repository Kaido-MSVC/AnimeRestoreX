#include "ModelContract.h"

#include <fstream>
#include <sstream>

#include "Json.h"
#include "Sha256.h"

namespace arx::core {

namespace fs = std::filesystem;

const char* errorKindName(ErrorKind k) {
    switch (k) {
    case ErrorKind::None: return "None";
    case ErrorKind::InvalidArgument: return "InvalidArgument";
    case ErrorKind::InvalidTileSize: return "InvalidTileSize";
    case ErrorKind::SizeOverflow: return "SizeOverflow";
    case ErrorKind::ContractViolation: return "ContractViolation";
    case ErrorKind::HashMismatch: return "HashMismatch";
    case ErrorKind::ModelNotFound: return "ModelNotFound";
    case ErrorKind::Cancelled: return "Cancelled";
    case ErrorKind::OutOfMemory: return "OutOfMemory";
    case ErrorKind::DeviceLost: return "DeviceLost";
    case ErrorKind::EngineFailure: return "EngineFailure";
    }
    return "?";
}

namespace {

Status bad(const std::string& m) { return Status::fail(ErrorKind::ContractViolation, "manifest: " + m); }

bool getInt(const JsonValue& o, const char* key, int& out) {
    const JsonValue* v = o.get(key);
    if (!v || !v->isNumber()) return false;
    double d = v->asNumber();
    if (d != (double)(int)d) return false;
    out = (int)d;
    return true;
}

bool getStr(const JsonValue& o, const char* key, std::string& out) {
    const JsonValue* v = o.get(key);
    if (!v || !v->isString()) return false;
    out = v->asString();
    return true;
}

bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// Relative, '/'-separated, no empty / "." / ".." components, no drive or root.
bool isSafeRelative(const std::string& p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos)
        return false;
    std::stringstream ss(p);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (part.empty() || part == "." || part == "..") return false;
    return p.back() != '/';
}

}  // namespace

Status ModelContract::load(const fs::path& manifest, ModelContract& out) {
    out = ModelContract{};
    std::ifstream f(manifest, std::ios::binary);
    if (!f) return Status::fail(ErrorKind::ModelNotFound, "cannot open manifest " + manifest.string());
    std::stringstream buf;
    buf << f.rdbuf();
    JsonValue root;
    std::string err;
    if (!JsonValue::parse(buf.str(), root, err)) return bad("JSON parse error: " + err);
    if (!root.isObject()) return bad("root is not an object");

    if (const JsonValue* io = root.get("io_contract")) {
        const JsonValue* in = io->get("input");
        const JsonValue* outp = io->get("output");
        if (!in || !outp || !getStr(*in, "name", out.inputName_) || !getStr(*outp, "name", out.outputName_))
            return bad("io_contract.input/output.name missing");
        std::string dt;
        if (!getStr(*in, "dtype", dt) || dt != "float32") return bad("input dtype must be float32");
        if (!getStr(*in, "layout", dt) || dt != "NCHW") return bad("input layout must be NCHW");
        if (!getStr(*in, "color", dt) || dt != "RGB") return bad("input color must be RGB");
    } else {
        return bad("io_contract missing");
    }
    if (const JsonValue* a = root.get("alpha")) {
        getStr(*a, "scale2x_alpha_model", out.alpha2x_);
        getStr(*a, "scale4x_alpha_model", out.alpha4x_);
    }

    const JsonValue* models = root.get("models");
    if (!models || !models->isArray() || models->items().empty()) return bad("models[] missing");
    for (const JsonValue& m : models->items()) {
        ModelInfo mi;
        if (!getStr(m, "id", mi.id) || !getStr(m, "file", mi.file) || !getStr(m, "sha256", mi.sha256) ||
            !getInt(m, "scale", mi.scale) || !getInt(m, "offset", mi.offset) || !getInt(m, "blend_size", mi.blendSize))
            return bad("model entry missing id/file/sha256/scale/offset/blend_size");
        getInt(m, "noise_level", mi.noiseLevel);
        if (mi.scale != 1 && mi.scale != 2 && mi.scale != 4) return bad(mi.id + ": scale must be 1, 2 or 4");
        if (mi.offset < 0 || mi.offset > 256 || mi.blendSize < 0 || mi.blendSize > 256)
            return bad(mi.id + ": offset / blend_size out of range");
        if (!isHex64(mi.sha256)) return bad(mi.id + ": sha256 is not 64 lower-case hex digits");
        if (!isSafeRelative(mi.file)) return bad(mi.id + ": file is not a safe relative path");
        if (out.find(mi.id)) return bad("duplicate model id " + mi.id);
        out.models_.push_back(std::move(mi));
    }
    for (const std::string* a : {&out.alpha2x_, &out.alpha4x_})
        if (!a->empty() && !out.find(*a)) return bad("alpha model " + *a + " not in models[]");
    return Status::success();
}

const ModelInfo* ModelContract::find(std::string_view id) const {
    for (const ModelInfo& m : models_)
        if (m.id == id) return &m;
    return nullptr;
}

std::string ModelContract::alphaModelFor(int scale) const {
    if (scale == 2) return alpha2x_;
    if (scale == 4) return alpha4x_;
    return {};
}

Status ModelContract::resolveVerified(const ModelInfo& m, const fs::path& modelsRoot, fs::path& resolved) const {
    if (!isSafeRelative(m.file)) return Status::fail(ErrorKind::ContractViolation, m.id + ": unsafe path");
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(modelsRoot, ec);
    if (ec) return Status::fail(ErrorKind::ModelNotFound, "models root: " + ec.message());
    const fs::path p = fs::weakly_canonical(root / fs::path(m.file), ec);
    if (ec) return Status::fail(ErrorKind::ModelNotFound, m.id + ": " + ec.message());
    // The canonical path must stay below the root (symlinks / junctions included).
    auto rel = p.lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..")
        return Status::fail(ErrorKind::ContractViolation, m.id + ": resolved path leaves the models root");
    if (!fs::is_regular_file(p, ec)) return Status::fail(ErrorKind::ModelNotFound, m.id + ": file not found " + p.string());
    std::string digest;
    Status s = sha256File(p, digest);
    if (!s.ok()) return s;
    if (digest != m.sha256)
        return Status::fail(ErrorKind::HashMismatch, m.id + ": SHA-256 " + digest + " != manifest " + m.sha256);
    resolved = p;
    return Status::success();
}

}  // namespace arx::core
