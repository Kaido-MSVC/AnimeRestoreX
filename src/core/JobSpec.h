#pragma once

#include <QString>
#include <QtGlobal>

namespace arx {

enum class OutputFormat { Png, Jpeg, WebP };

struct EncodeOptions {
    OutputFormat format = OutputFormat::Png;
    bool webpLossless = false;  // only meaningful for WebP
    int quality = 90;           // JPEG 0-100, WebP lossy 0-99; ignored for PNG / WebP lossless
    // 2.1: save every output as grayscale (nunif --grayscale). A grayscale source is saved
    // as grayscale either way (nunif keeps L / LA images grayscale).
    bool forceGrayscale = false;
    // 2.1: copy the source's EXIF block into the output (orientation, size and thumbnail fixed).
    bool keepExif = false;
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
    // Test-time augmentation (nunif --tta): 8 flipped / rotated runs averaged, about 8x the time.
    bool tta = false;
    // 2.1 film grain (nunif --grain / --grain-strength). grainStrength is nunif's grain_strength;
    // for still images nunif applies half of it (waifu2x/ui_utils.py process_image).
    bool grain = false;
    double grainStrength = 0.2;
};

// nunif grain_strength: CLI default 0.2 (ui_utils.py), GUI choices 0.1-0.5 with 0.2 selected and
// values up to 1.0 accepted (gui.py). 0 means "no grain" upstream, so the range starts above it.
inline constexpr double kGrainStrengthDefault = 0.2;
inline constexpr double kGrainStrengthMin = 0.05;
inline constexpr double kGrainStrengthMax = 1.0;

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
    SmallerForVram, // Auto at 160: the GPU memory budget was too small for 256
    OomRetry,       // Auto: GPU failure at 256, the sessions were rebuilt, the image was redone at 160
    AfterGpuError,  // Auto at 160: an earlier image of this batch recovered from a GPU failure
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
    bool tta = false;    // the result is the TTA mean of 8 runs (set when ok)
    bool grayscale = false;  // saved as grayscale (source was grayscale, or forced; set when ok)
    bool grain = false;      // film grain was added (set when ok)
    // The engine should release its sessions before the next item (after an
    // out-of-memory failure or a device error).
    bool resetEngine = false;
    // The GPU failed at the first tile, the sessions were rebuilt and the inference was
    // redone at the fallback tile successfully. Reported separately from `ok`: a later
    // step (output file, encoding, commit, cancel) may still fail, and the batch must
    // keep the rebuilt sessions and the fallback tile either way.
    bool gpuRecovered = false;
    // Not shown in the UI: the step that failed, error kind, HRESULT, model,
    // device and tile, for tests and troubleshooting. No image content.
    QString diagnostics;
};

} // namespace arx
