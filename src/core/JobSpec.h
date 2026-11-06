#pragma once

#include <QString>
#include <QtGlobal>

namespace arx {

enum class OutputFormat { Png, Jpeg, WebP };

struct EncodeOptions {
    OutputFormat format = OutputFormat::Png;
    bool webpLossless = false;  // only meaningful for WebP
    int quality = 90;           // JPEG 0-100, WebP lossy 0-99; ignored for PNG / WebP lossless
};

// Where inference runs. CPU is only used when the user picks it; a GPU failure never
// switches a job to the CPU on its own.
enum class ComputeDevice { Gpu = 0, Cpu = 1 };

// nunif waifu2x model choice (models/manifest.json) and how to run it.
struct InferenceOptions {
    int noise = 1;     // -1..3 (-1 = no denoise)
    int scale = 2;     // 1 (denoise only, needs noise >= 0), 2 or 4
    int tile = 0;      // 0 = Auto (256, or 160 when GPU memory is short / after a GPU failure); 160, 256, 352, 448
    ComputeDevice device = ComputeDevice::Gpu;
};

// Snapshot of all parameters for one job; copied into the worker so that
// UI changes during processing do not affect the running job.
struct JobSpec {
    QString outputDir;
    InferenceOptions inference;
    EncodeOptions encode;
};

// How the tile size of an item was chosen (see core/inference/TilePolicy.h).
enum class TileChoice {
    Default,        // Auto at 256, or the CPU default
    Manual,         // chosen in the settings
    SmallerForVram, // Auto at 160: too little GPU memory for 256 (budget, or an earlier recovery)
    OomRetry,       // Auto: GPU failure at 256, the sessions were rebuilt, the image was redone at 160
};

struct ItemResult {
    bool ok = false;
    bool canceled = false;
    bool fatal = false;  // shared error (engine/GPU/model); the batch must stop
    QString outputPath;  // set when ok
    QString error;       // set when !ok && !canceled
    // Tile actually used for the result (0 if inference did not finish) and why.
    int tileUsed = 0;
    TileChoice tileChoice = TileChoice::Default;
    // The engine should release its sessions before the next item (after an
    // out-of-memory failure or a device error).
    bool resetEngine = false;
    // Not shown in the UI: the step that failed, error kind, HRESULT, model,
    // device and tile, for tests and troubleshooting. No image content.
    QString diagnostics;
};

} // namespace arx
