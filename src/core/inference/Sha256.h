// SHA-256 of a file with Windows CNG (bcrypt.dll). Hash check only, not a signature check.
#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "Status.h"

namespace arx::core {

// Lower-case hex digest of the file contents.
std::expected<std::string, Status> sha256File(const std::filesystem::path& file);

}  // namespace arx::core
