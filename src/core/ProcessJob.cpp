#include "core/ProcessJob.h"

#include "core/ImageEncoder.h"
#include "core/ImageLimits.h"
#include "core/ImageLoader.h"
#include "core/ImageMetadata.h"
#include "core/OutputNaming.h"
#include "core/inference/TileProcessor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>

namespace arx {

namespace {

ItemResult failure(const QString &msg)
{
    ItemResult r;
    r.error = msg;
    return r;
}

ItemResult canceled()
{
    ItemResult r;
    r.canceled = true;
    return r;
}

// Closes and removes a file this job created (only after it was opened with
// NewOnly, so it can never be someone else's file), unless released.
class TempFileCleanup {
public:
    explicit TempFileCleanup(QFile &file) : m_file(&file) {}
    ~TempFileCleanup()
    {
        if (m_file) {
            m_file->close();
            m_file->remove();
        }
    }
    void release() { m_file = nullptr; }

private:
    QFile *m_file;
};

// Gets the session from the backend on every Run: the session is created on first use,
// and with one session slot (not enough GPU memory for RGB + alpha together) the alpha
// session replaces the RGB session only after the RGB pass is complete.
class BackendEngine final : public core::ITileEngine {
public:
    BackendEngine(core::Backend &backend, const core::ModelInfo &model) : m_backend(backend), m_model(model) {}
    core::Status run(const float *input, int tile, int outSide, std::vector<float> &output) override
    {
        core::ITileEngine *engine = nullptr;
        core::Status s = m_backend.acquire(m_model, engine);
        if (!s.ok())
            return s;
        return engine->run(input, tile, outSide, output);
    }

private:
    core::Backend &m_backend;
    const core::ModelInfo &m_model;
};

QString hex(quint32 v)
{
    return QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

QString statusDiagnostics(const core::Status &s)
{
    QString d = QString::fromLatin1(core::errorKindName(s.kind));
    if (s.hresult)
        d += QLatin1Char(' ') + hex(s.hresult);
    if (!s.message.empty())
        d += QStringLiteral(": ") + QString::fromStdString(s.message);
    return d;
}

TileChoice tileChoice(const core::TileDecision &d, int attempts)
{
    if (attempts > 1)
        return TileChoice::OomRetry;
    switch (d.reason) {
    case core::TileReason::Manual: return TileChoice::Manual;
    case core::TileReason::AutoVram: return TileChoice::SmallerForVram;
    case core::TileReason::AutoRecovered: return TileChoice::AfterGpuError;
    case core::TileReason::Auto:
    case core::TileReason::Cpu: break;
    }
    return TileChoice::Default;
}

} // namespace

QString ProcessJob::checkOutputDir(const QString &dir)
{
    if (dir.isEmpty())
        return tr("No output folder is selected.");
    const QFileInfo fi(dir);
    if (!fi.exists() || !fi.isDir())
        return tr("The output folder does not exist: %1").arg(QDir::toNativeSeparators(dir));

    // QFileInfo::isWritable is unreliable for folders on NTFS; probe instead.
    QFile probe(QDir(dir).filePath(QStringLiteral(".arx-write-test-%1.tmp")
                                        .arg(QCoreApplication::applicationPid())));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return tr("The output folder is not writable: %1").arg(QDir::toNativeSeparators(dir));
    probe.close();
    probe.remove();
    return {};
}

QString ProcessJob::checkOptions(const InferenceOptions &o)
{
    if (o.scale != 1 && o.scale != 2 && o.scale != 4)
        return tr("Unsupported scale: %1x.").arg(o.scale);
    if (o.noise < -1 || o.noise > 3)
        return tr("Unsupported denoise level: %1.").arg(o.noise);
    if (o.scale == 1 && o.noise < 0)
        return tr("\"No upscaling (1x)\" only denoises: choose a denoise level.");
    // Defence in depth: the settings file may have been edited by hand, so the job itself
    // accepts only Auto / 160 / 256 / 352 / 448 (legal for every model; checked against the
    // model's own rule again once the model is known).
    if (!core::isOfferedTile(o.tile))
        return tr("Tile size %1 is not valid for this model.").arg(o.tile);
    return {};
}

QString ProcessJob::modelId(const InferenceOptions &o)
{
    if (!checkOptions(o).isEmpty())
        return {};
    // 1x / 2x: swin_unet_v3. 4x: the 20250502 swin_unet graph (v3 has no 4x weights).
    const QString base = o.scale == 4 ? QStringLiteral("swin_unet/art/") : QStringLiteral("swin_unet_v3/art/");
    if (o.scale == 1)
        return base + QStringLiteral("noise%1").arg(o.noise);
    const QString scale = QStringLiteral("scale%1x").arg(o.scale);
    return o.noise < 0 ? base + scale : base + QStringLiteral("noise%1_").arg(o.noise) + scale;
}

QString ProcessJob::checkPreconditions(const JobSpec &spec)
{
    QString err = checkOutputDir(spec.outputDir);
    if (!err.isEmpty())
        return err;
    err = checkOptions(spec.inference);
    if (!err.isEmpty())
        return err;
    const QByteArray fmt = ImageEncoder::formatName(spec.encode.format);
    if (!QImageWriter::supportedImageFormats().contains(fmt))
        return tr("The %1 image plugin is missing.").arg(QString::fromLatin1(fmt).toUpper());
    return {};
}

QString ProcessJob::runtimeErrorMessage(const core::Status &s)
{
    if (s.kind == core::ErrorKind::ModelNotFound)
        return tr("The AI runtime files (onnxruntime.dll, DirectML.dll) are missing from the program "
                  "folder. Please reinstall AnimeRestoreX.");
    return tr("The AI runtime in the program folder cannot be used (wrong version or damaged). "
              "Please reinstall AnimeRestoreX. Details: %1")
        .arg(QString::fromStdString(s.message));
}

QString ProcessJob::engineErrorMessage(const core::Status &s)
{
    switch (s.kind) {
    case core::ErrorKind::None:
        return {};
    case core::ErrorKind::OutOfMemory:
        return tr("Not enough GPU memory to process this image.");
    case core::ErrorKind::DeviceLost:
        return tr("The GPU stopped responding (device error %1). Processing was stopped and the GPU "
                  "engine was reset. Close other programs that use the GPU or update the graphics "
                  "driver, then try again.")
            .arg(hex(s.hresult));
    case core::ErrorKind::HashMismatch:
        return tr("A model file is damaged or was replaced (its checksum does not match "
                  "models/manifest.json). Please reinstall AnimeRestoreX.");
    case core::ErrorKind::ModelNotFound:
        return tr("The AI model files are missing from the \"models\" folder next to AnimeRestoreX.exe. "
                  "This copy of AnimeRestoreX may have been distributed without the models; "
                  "see README.txt for how to add them.");
    case core::ErrorKind::ContractViolation:
        return tr("The AI model does not match its description in models/manifest.json.");
    case core::ErrorKind::InvalidTileSize:
        return tr("The tile size is not valid for this model.");
    case core::ErrorKind::SizeOverflow:
        return tr("The image is too large to process with the available memory.");
    case core::ErrorKind::InvalidArgument:
        return tr("The image cannot be processed in this format.");
    case core::ErrorKind::Cancelled:
        return tr("Canceled.");
    case core::ErrorKind::EngineFailure:
        break;
    }
    return tr("The AI engine reported an error (%1).").arg(QString::fromStdString(s.message).left(300));
}

QString ProcessJob::prepare(const JobSpec &spec, core::Backend &backend, PreparedJob &job, QString *diagnostics)
{
    job = PreparedJob{};
    auto fail = [&](const QString &msg, const core::Status &s) {
        if (diagnostics)
            *diagnostics = statusDiagnostics(s);
        return msg;
    };
    core::Status s = backend.runtimeReady();
    if (!s.ok())
        return fail(runtimeErrorMessage(s), s);
    s = backend.ready();
    if (!s.ok())
        return fail(tr("The model list (models/manifest.json) is missing or invalid. Please reinstall "
                       "AnimeRestoreX."),
                    s);

    const InferenceOptions &o = spec.inference;
    const QString id = modelId(o);
    if (id.isEmpty())
        return checkOptions(o);
    const core::ModelContract &contract = backend.contract();
    job.rgbModel = contract.find(id.toStdString());
    if (!job.rgbModel)
        return fail(tr("The model %1 is not listed in models/manifest.json.").arg(id),
                    core::Status::fail(core::ErrorKind::ModelNotFound, id.toStdString()));
    if (o.scale > 1) {
        const std::string aid = contract.alphaModelFor(o.scale);
        job.alphaModel = contract.find(aid);
        if (!job.alphaModel)
            return fail(tr("The model %1 is not listed in models/manifest.json.").arg(QString::fromStdString(aid)),
                        core::Status::fail(core::ErrorKind::ModelNotFound, aid));
    }
    // Hash check before the first image: a damaged model stops the job, no other model is used.
    for (const core::ModelInfo *m : {job.rgbModel, job.alphaModel}) {
        if (!m)
            continue;
        s = backend.verify(*m);
        if (!s.ok())
            return fail(engineErrorMessage(s), s);
    }

    const bool cpu = o.device == ComputeDevice::Cpu;
    job.device = cpu ? core::ExecutionDevice::Cpu : core::ExecutionDevice::Dml;
    job.vramMiB = cpu ? 0 : backend.availableVramMiB();
    job.tile = core::chooseTile(o.tile, o.scale, cpu, job.vramMiB);
    for (const core::ModelInfo *m : {job.rgbModel, job.alphaModel}) {
        if (m && (!core::ModelContract::isValidTile(*m, job.tile.tile) ||
                  (job.tile.allowOomRetry && !core::ModelContract::isValidTile(*m, job.tile.fallbackTile))))
            return fail(tr("Tile size %1 is not valid for this model.").arg(job.tile.tile),
                        core::Status::fail(core::ErrorKind::InvalidTileSize, std::to_string(job.tile.tile)));
    }
    s = backend.beginJob(job.device, job.tile.alphaTogether ? 2 : 1);
    if (!s.ok())
        return fail(engineErrorMessage(s), s);
    job.deviceName = QString::fromStdString(backend.deviceName(job.device));
    return {};
}

PlanContext ProcessJob::planContext(const PreparedJob &job, int index)
{
    PlanContext c;
    if (job.rgbModel)
        c.modelHash = QString::fromStdString(job.rgbModel->sha256);
    c.tile = job.tile.tile;
    c.cold = index == 0;
    return c;
}

ItemPlan ProcessJob::progressPlan(const QString &sourcePath, const JobSpec &spec, const PlanContext *context)
{
    // Default speeds in seconds per output megapixel, from the P2 tile 256 measurements
    // on the GTX 1060 6GB (input MP/s: 1x 0.284, 2x 0.303, 4x 0.234). They only shape the
    // first estimate; measured speeds replace them. CPU: factor measured on one PC (P4).
    constexpr double kLoadRate = 0.05;  // decode + conversion, per input MP
    const QSize in = QImageReader(sourcePath).size();  // header only; invalid if unreadable
    const double inMp = in.isValid() ? double(in.width()) * in.height() / 1e6 : 0.0;
    const InferenceOptions &o = spec.inference;
    const int scale = o.scale;
    const bool cpu = o.device == ComputeDevice::Cpu;

    double rate = scale == 1 ? 3.52 : scale == 2 ? 0.825 : 0.267;
    if (cpu)
        rate *= 5.0;  // P4: CPU 13.9-15.8 s vs. GTX 1060 2.9 s for a 0.38-0.5 MP 2x image

    ItemPlan plan;
    plan.weight = inMp * scale * scale;
    plan.stages << StagePlan{Stage::Load, 0, QStringLiteral("load"), inMp, kLoadRate, 0.0, {}};
    const int tile = context && context->tile ? context->tile : o.tile;
    QString model = modelId(o);
    if (context && !context->modelHash.isEmpty())
        model += QLatin1Char('@') + context->modelHash.left(8);
    // TTA renders the RGB image 8 times: its own speed group ("/tta"), 8x the RGB work.
    const QString key = QStringLiteral("nunif/%1/%2/%3/x%4%5")
                            .arg(model, cpu ? QStringLiteral("cpu") : QStringLiteral("dml"),
                                 tile ? QStringLiteral("t%1").arg(tile) : QStringLiteral("auto"))
                            .arg(scale)
                            .arg(o.tta ? QStringLiteral("/tta") : QString());
    // Cold start: session creation (and the hash check) before the first tile; about 4 s
    // per DML session on the GTX 1060 (P2/P3 measurements), less on the CPU.
    const bool cold = context && context->cold;
    const double coldSec = cold ? (cpu ? 1.0 : 4.0) : 0.0;
    plan.stages << StagePlan{Stage::Inference, 0, key, plan.weight * (o.tta ? 8 : 1), rate, coldSec,
                             cold ? key + QStringLiteral("#cold") : QString()};

    QString encKey;
    double encRate = 0;
    switch (spec.encode.format) {
    case OutputFormat::Png:  encKey = QStringLiteral("encode/png");  encRate = 0.15; break;
    case OutputFormat::Jpeg: encKey = QStringLiteral("encode/jpeg"); encRate = 0.05; break;
    case OutputFormat::WebP:
        encKey = spec.encode.webpLossless ? QStringLiteral("encode/webp-lossless")
                                          : QStringLiteral("encode/webp");
        encRate = spec.encode.webpLossless ? 1.0 : 0.15;
        break;
    }
    plan.stages << StagePlan{Stage::Encode, 0, encKey, plan.weight, encRate, 0.0, {}};
    return plan;
}

ItemResult ProcessJob::run(const QString &sourcePath, const JobSpec &spec, core::Backend &backend,
                           const PreparedJob &job, const std::atomic_bool &cancel, const ProgressSink &progress)
{
    auto report = [&](Stage stage) {
        if (progress.stageStarted)
            progress.stageStarted(StageEvent{stage, 0, 1});
    };

    // Failures carry the step, model, device and tile as diagnostics (for tests and
    // troubleshooting; the UI shows only `error`).
    QString step;
    QString engineDetail;
    int tileUsed = 0;
    bool gpuRecovered = false;  // set once the redone inference succeeded
    auto fail = [&](const QString &msg) {
        ItemResult r = failure(msg);
        r.gpuRecovered = gpuRecovered;
        r.diagnostics = QStringLiteral("step: %1\nmodel: %2\ndevice: %3\ntile: %4\ntta: %5")
                            .arg(step,
                                 job.rgbModel ? QString::fromStdString(job.rgbModel->id) : QString(),
                                 job.deviceName)
                            .arg(tileUsed ? tileUsed : job.tile.tile)
                            .arg(spec.inference.tta ? QStringLiteral("on") : QStringLiteral("off"));
        if (!engineDetail.isEmpty())
            r.diagnostics += QStringLiteral("\nengine: ") + engineDetail;
        return r;
    };
    if (!job.rgbModel)
        return fail(tr("The job was not prepared."));

    // 1. Load and validate.
    step = QStringLiteral("load");
    report(Stage::Load);
    LoadResult loaded = ImageLoader::load(sourcePath);
    if (!loaded.ok)
        return fail(loaded.error);

    const int scale = spec.inference.scale;
    const QSize inSize = loaded.image.size();
    const limits::SizeCheck sizeCheck = limits::check(inSize, scale, spec.encode.format);
    if (sizeCheck != limits::SizeCheck::Ok)
        return fail(limits::Messages::forCheck(sizeCheck, inSize, scale, spec.encode.format));
    if (cancel.load())
        return canceled();

    // 2. Tiled inference on straight (non-premultiplied) 8-bit RGB / RGBA, byte order R,G,B[,A].
    step = QStringLiteral("inference");
    QImage input = loaded.hasAlpha ? loaded.image.convertToFormat(QImage::Format_RGBA8888)
                                   : loaded.image.convertToFormat(QImage::Format_RGB888);
    loaded.image = QImage();  // one input copy only
    if (input.isNull())
        return fail(tr("Out of memory while converting the image."));
    report(Stage::Inference);

    BackendEngine rgbEngine(backend, *job.rgbModel);
    BackendEngine alphaEngine(backend, job.alphaModel ? *job.alphaModel : *job.rgbModel);
    const core::TileParams rgbParams = core::ModelContract::tileParams(*job.rgbModel);
    const core::TileParams alphaParams =
        job.alphaModel ? core::ModelContract::tileParams(*job.alphaModel) : rgbParams;

    core::ProcessOptions opt;
    opt.tile = job.tile.tile;
    opt.tta = spec.inference.tta;
    opt.allowOomRetry = job.tile.allowOomRetry;
    opt.fallbackTile = job.tile.fallbackTile;
    opt.maxOutputPixels = quint64(limits::kMaxOutputPixels);
    opt.cancel = &cancel;
    opt.beforeRetry = [&] {
        if (progress.retrying)
            progress.retrying(job.tile.fallbackTile);
        // BackendEngine keeps no session pointer: the next Run creates new sessions and with
        // them a new GPU device, once the runtime holds no reference to the old one.
        backend.releaseAll();
    };
    opt.progress = [&](const core::Progress &p) {
        if (progress.stageFraction && p.totalPixels > 0)
            progress.stageFraction(double(p.donePixels) / double(p.totalPixels));
    };

    const core::ImageView view{input.constBits(), input.width(), input.height(), loaded.hasAlpha ? 4 : 3,
                               std::ptrdiff_t(input.bytesPerLine())};
    core::ProcessStats stats;
    auto upscaled = core::upscaleImage(view, rgbParams, rgbEngine, job.alphaModel ? &alphaParams : nullptr,
                                       job.alphaModel ? &alphaEngine : nullptr, opt, &stats);
    input = QImage();
    const bool retried = stats.firstAttemptError.kind != core::ErrorKind::None;
    tileUsed = stats.tileUsed ? stats.tileUsed : (retried ? job.tile.fallbackTile : job.tile.tile);
    if (!upscaled) {
        const core::Status &s = upscaled.error();
        // A CPU Run aborted by SetTerminate fails with an engine error: it is a cancel.
        if (s.kind == core::ErrorKind::Cancelled || cancel.load())
            return canceled();
        engineDetail = statusDiagnostics(s);
        if (retried)
            engineDetail += QStringLiteral(" (after ") + statusDiagnostics(stats.firstAttemptError) +
                            QStringLiteral(" at tile %1)").arg(job.tile.tile);
        ItemResult r;
        switch (s.kind) {
        case core::ErrorKind::OutOfMemory:
            r = fail(tr("Not enough GPU memory to process this image with tile size %1. Choose a "
                        "smaller tile size or Auto in the settings.")
                         .arg(tileUsed));
            r.resetEngine = true;  // release the sessions before the next image
            break;
        case core::ErrorKind::SizeOverflow:
        case core::ErrorKind::InvalidArgument:
            r = fail(engineErrorMessage(s));
            break;
        default:
            // Device lost, model / contract errors, unknown runtime errors: the engine is
            // not trusted any more; stop the batch and release everything.
            r = fail(engineErrorMessage(s));
            r.fatal = true;
            r.resetEngine = true;
            break;
        }
        // The batch's single recovery is used up: stop here, never start a retry loop.
        const bool gpuFailure = s.kind == core::ErrorKind::OutOfMemory || s.kind == core::ErrorKind::DeviceLost ||
                                s.kind == core::ErrorKind::EngineFailure;
        if (retried || (job.recovered && gpuFailure)) {
            if (gpuFailure)
                r = fail(tr("The GPU still failed after the AI engine was restarted, at tile size %1 "
                            "(error %2). Close other programs that use the GPU or switch to the CPU "
                            "in the settings, then try again.")
                             .arg(tileUsed)
                             .arg(s.hresult ? hex(s.hresult) : QString::fromLatin1(core::errorKindName(s.kind))));
            r.fatal = true;
            r.resetEngine = true;
        }
        r.tileUsed = 0;
        return r;
    }
    core::Image8 &out = *upscaled;
    gpuRecovered = retried;
    auto canceledAfterInference = [&] {
        ItemResult r = canceled();
        r.gpuRecovered = gpuRecovered;
        return r;
    };
    const QSize expected(inSize.width() * scale, inSize.height() * scale);
    if (QSize(out.width, out.height) != expected)
        return fail(tr("The AI output has an unexpected size (%1 x %2, expected %3 x %4).")
                        .arg(out.width).arg(out.height).arg(expected.width()).arg(expected.height()));
    if (cancel.load())
        return canceledAfterInference();

    // 3. Encode into a temporary file inside the output folder. The QImage wraps the
    //    result buffer (no copy); `out` outlives it.
    step = QStringLiteral("encode");
    report(Stage::Encode);

    // Grayscale (nunif pil_io.save_image, 0d3d92c lines 292-296): a grayscale source, or the
    // "grayscale output" option, turns the 8-bit result into Pillow convert("L") values.
    const OutputFormat format = spec.encode.format;
    const bool gray = spec.encode.forceGrayscale || loaded.meta.grayscale;
    if (gray)
        ImageEncoder::toGrayscale(out.pixels.data(), out.width, out.height, out.channels,
                                  qsizetype(out.width) * out.channels);
    // One gray channel in the file: PNG / JPEG without alpha (JPEG drops the alpha first).
    const bool grayPlane = gray && format != OutputFormat::WebP && !(format == OutputFormat::Png && out.channels == 4);
    ImageEncoder::Extras extras;
    extras.grayscale = gray;

    // ICC: the pixels were never converted, so the source profile goes back as it was, if it
    // still describes the output: an RGB profile on RGB(A) pixels, a GRAY profile on a gray plane.
    QString iccNote = QStringLiteral("none");
    if (!loaded.meta.icc.isEmpty()) {
        const QByteArray space = metadata::iccColorSpace(loaded.meta.icc);
        if (space == (grayPlane ? "GRAY" : "RGB ") && (space == "GRAY" || !gray)) {
            extras.icc = loaded.meta.icc;
            iccNote = QStringLiteral("kept (%1 bytes, %2)").arg(extras.icc.size()).arg(QString::fromLatin1(space.trimmed()));
        } else if (space == "RGB ") {
            iccNote = QStringLiteral("dropped: RGB profile on grayscale output");
        } else {
            iccNote = QStringLiteral("dropped: grayscale profile on an RGB(A) file");
        }
    }

    loaded.meta = {};

    const QImage result(out.pixels.data(), out.width, out.height, qsizetype(out.width) * out.channels,
                        out.channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    const QString stem = OutputNaming::stem(sourcePath, scale);
    const QString ext = ImageEncoder::extension(spec.encode.format);
    const QString tmpPath = OutputNaming::tempPath(spec.outputDir, stem);
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(tr("Cannot create a file in the output folder: %1").arg(tmp.errorString()));
    TempFileCleanup cleanup(tmp);

    QString encodeError, metadataError;
    if (!ImageEncoder::encode(result, spec.encode, extras, &tmp, &encodeError, &metadataError))
        return fail(encodeError);
    if (!metadataError.isEmpty()) {
        if (!extras.icc.isEmpty())
            iccNote = QStringLiteral("not written: ") + metadataError;
    }
    if (!tmp.flush() || tmp.error() != QFileDevice::NoError)
        return fail(tr("Failed to write the output file: %1").arg(tmp.errorString()));
    tmp.close();
    if (tmp.error() != QFileDevice::NoError || QFileInfo(tmpPath).size() <= 0)
        return fail(tr("Failed to write the output file: %1").arg(tmp.errorString()));

    if (cancel.load())
        return canceledAfterInference();

    // 4. Commit under the final, non-overwriting name. After this point the item is a
    //    success even if a cancel arrives (the commit is the completion boundary).
    step = QStringLiteral("commit output file");
    QString commitError;
    const QString finalPath = OutputNaming::commit(tmpPath, spec.outputDir, stem, ext, &commitError);
    if (finalPath.isEmpty())
        return fail(commitError);
    cleanup.release();

    ItemResult ok;
    ok.ok = true;
    ok.outputPath = finalPath;
    ok.tileUsed = tileUsed;
    ok.tileChoice = tileChoice(job.tile, stats.attempts);
    ok.gpuRecovered = gpuRecovered;
    ok.tta = stats.tta;
    ok.diagnostics = QStringLiteral("model: %1\ndevice: %2\ntile: %3 (attempts %4%5)\ntta: %6")
                         .arg(QString::fromStdString(job.rgbModel->id), job.deviceName)
                         .arg(tileUsed)
                         .arg(stats.attempts)
                         .arg(stats.alphaPassed ? QStringLiteral(", alpha model") : QString(),
                              stats.tta ? QStringLiteral("on (8 runs)") : QStringLiteral("off"));
    ok.diagnostics += QStringLiteral("\ngray: %1\nicc: %2")
                          .arg(spec.encode.forceGrayscale ? QStringLiteral("forced")
                               : gray                     ? QStringLiteral("source is grayscale")
                                                          : QStringLiteral("off"),
                               iccNote);
    ok.grayscale = gray;
    return ok;
}

} // namespace arx
