#pragma once

#include <QString>

namespace arx {

enum class OutputFormat { Png, Jpeg, WebP };

struct EncodeOptions {
    OutputFormat format = OutputFormat::Png;
    bool webpLossless = false;  // only meaningful for WebP
    int quality = 90;           // JPEG 0-100, WebP lossy 0-99; ignored for PNG / WebP lossless
};

struct Waifu2xParams {
    QString exePath;   // absolute path to waifu2x-ncnn-vulkan.exe
    QString modelDir;  // absolute path to models-cunet
    int noise = 1;     // -1..3
    int scale = 2;     // 1 (denoise only, needs noise >= 0), 2 or 4
    int tile = 0;      // 0 = auto
};

// Snapshot of all parameters for one job; copied into the worker so that
// UI changes during processing do not affect the running job.
struct JobSpec {
    QString outputDir;
    Waifu2xParams waifu2x;
    EncodeOptions encode;
};

struct ItemResult {
    bool ok = false;
    bool canceled = false;
    bool fatal = false;  // shared error (engine/GPU); the batch must stop
    QString outputPath;  // set when ok
    QString error;       // set when !ok && !canceled
    // Not shown in the UI: the step that failed and the engine stderr of the
    // last pass, for tests and troubleshooting.
    QString diagnostics;
};

} // namespace arx
