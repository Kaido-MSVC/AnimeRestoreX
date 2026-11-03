// SHA-256 of a file with Windows CNG (bcrypt.dll). Hash check only, not a signature check.
#pragma once

#include <filesystem>
#include <string>

#include "Status.h"

namespace arx::core {

// Lower-case hex digest of the file contents.
Status sha256File(const std::filesystem::path& file, std::string& hexDigest);

}  // namespace arx::core
