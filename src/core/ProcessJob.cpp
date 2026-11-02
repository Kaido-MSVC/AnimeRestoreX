#include "core/ProcessJob.h"

#include "core/ImageEncoder.h"
#include "core/ImageLimits.h"
#include "core/ImageLoader.h"
#include "core/OutputNaming.h"
#include "core/Waifu2xRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

namespace arx {

namespace {

ItemResult fail(const QString &msg)
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

QString ProcessJob::checkPreconditions(const JobSpec &spec)
{
    QString err = checkOutputDir(spec.outputDir);
    if (!err.isEmpty())
        return err;
    err = Waifu2xRunner::checkInstall(spec.waifu2x);
    if (!err.isEmpty())
        return err;
    const QByteArray fmt = ImageEncoder::formatName(spec.encode.format);
    if (!QImageWriter::supportedImageFormats().contains(fmt))
        return tr("The %1 image plugin is missing.").arg(QString::fromLatin1(fmt).toUpper());
    return {};
}

ItemPlan ProcessJob::progressPlan(const QString &sourcePath, const JobSpec &spec)
{
    // Default speeds in seconds per output megapixel. Rough values from the
    // A9 measurements (GTX 1060 6GB); they are replaced by measured speeds
    // after the first image, so they only shape the very first estimate.
    constexpr double kLoadRate = 0.10;        // decode + temporary PNG, per input MP
    constexpr double kUpscaleRate = 0.37;     // cunet 2x, per output MP
    constexpr double kDenoiseOnlyRate = 1.0;  // cunet 1x (noiseN_model), per output MP
    constexpr double kEngineStartSec = 1.0;   // process start, Vulkan init, model load

    const QSize in = QImageReader(sourcePath).size();  // header only; invalid if unreadable
    const double inMp = in.isValid() ? double(in.width()) * in.height() / 1e6 : 0.0;
    const int scale = spec.waifu2x.scale;

    ItemPlan plan;
    plan.weight = inMp * scale * scale;
    plan.stages << StagePlan{Stage::Load, 0, QStringLiteral("load"), inMp, kLoadRate, 0.0};

    const QList<Waifu2xParams> passes = Waifu2xRunner::passes(spec.waifu2x);
    double mp = inMp;
    for (int i = 0; i < passes.size(); ++i) {
        const Waifu2xParams &pass = passes.at(i);
        mp *= double(pass.scale) * pass.scale;
        plan.stages << StagePlan{Stage::Inference, i,
                                 QStringLiteral("waifu2x/") + Waifu2xRunner::modelBaseName(pass), mp,
                                 pass.scale == 1 ? kDenoiseOnlyRate : kUpscaleRate, kEngineStartSec};
    }

    // Read back, convert and encode, per output MP.
    QString key;
    double rate = 0;
    switch (spec.encode.format) {
    case OutputFormat::Png:  key = QStringLiteral("encode/png");  rate = 0.15; break;
    case OutputFormat::Jpeg: key = QStringLiteral("encode/jpeg"); rate = 0.05; break;
    case OutputFormat::WebP:
        key = spec.encode.webpLossless ? QStringLiteral("encode/webp-lossless")
                                       : QStringLiteral("encode/webp");
        rate = spec.encode.webpLossless ? 1.0 : 0.15;
        break;
    }
    plan.stages << StagePlan{Stage::Encode, 0, key, plan.weight, rate, 0.0};
    return plan;
}

ItemResult ProcessJob::run(const QString &sourcePath, const JobSpec &spec,
                           const std::atomic_bool &cancel, const ProgressSink &progress)
{
    const QList<Waifu2xParams> passes = Waifu2xRunner::passes(spec.waifu2x);
    auto report = [&](Stage stage, int pass) {
        if (progress.stageStarted)
            progress.stageStarted(StageEvent{stage, pass, int(passes.size())});
    };

    // 1. Load and validate.
    report(Stage::Load, 0);
    LoadResult loaded = ImageLoader::load(sourcePath);
    if (!loaded.ok)
        return fail(loaded.error);

    const int scale = spec.waifu2x.scale;
    const QSize inSize = loaded.image.size();
    const limits::SizeCheck sizeCheck = limits::check(inSize, scale, spec.encode.format);
    if (sizeCheck != limits::SizeCheck::Ok)
        return fail(limits::Messages::forCheck(sizeCheck, inSize, scale, spec.encode.format));
    if (cancel.load())
        return canceled();

    // 2. Write the normalised 8-bit image as a temporary PNG.
    QTemporaryDir work(QDir(QDir::tempPath()).filePath(QStringLiteral("AnimeRestoreX-XXXXXX")));
    if (!work.isValid())
        return fail(tr("Cannot create a temporary folder: %1").arg(work.errorString()));
    const QString inPng = work.filePath(QStringLiteral("in.png"));
    const QString outPng = work.filePath(QStringLiteral("out.png"));
    {
        QImageWriter w(inPng, "png");
        if (!w.write(loaded.image))
            return fail(tr("Cannot write the temporary file: %1").arg(w.errorString()));
    }
    const bool hasAlpha = loaded.hasAlpha;
    loaded.image = QImage();  // free memory before inference

    // 3. Inference: one pass for 1x (denoise only) and 2x, or for 4x a
    //    denoise+2x pass followed by an upscale-only 2x pass. waifu2x exits
    //    with 0 on decode errors, so each pass is verified by its output file
    //    and exact size. waifu2x-ncnn-vulkan has no progress output, so
    //    progress.stageFraction is not used here (an engine that reports real
    //    progress would forward it during the pass).
    QString passInput = inPng;
    QSize passSize = inSize;
    for (int i = 0; i < passes.size(); ++i) {
        const QString passOutput = (i == passes.size() - 1)
            ? outPng
            : work.filePath(QStringLiteral("pass%1.png").arg(i + 1));
        report(Stage::Inference, i);
        const RunResult run = Waifu2xRunner::run(passes.at(i), passInput, passOutput, cancel);
        if (!run.device.isEmpty() && progress.deviceDetected)
            progress.deviceDetected(run.device);
        if (run.canceled)
            return canceled();
        if (!run.ok) {
            ItemResult r = fail(run.error);
            r.fatal = run.fatal;
            return r;
        }
        if (!QFileInfo::exists(passOutput)) {
            QString msg = tr("The AI engine did not produce an output image.");
            if (!run.error.isEmpty())
                msg += QLatin1Char(' ') + run.error;
            return fail(msg);
        }
        const int passScale = passes.at(i).scale;
        const QSize expected(passSize.width() * passScale, passSize.height() * passScale);
        const QSize got = QImageReader(passOutput, "png").size();
        if (got != expected)
            return fail(tr("The AI output has an unexpected size (%1 x %2, expected %3 x %4).")
                            .arg(got.width()).arg(got.height())
                            .arg(expected.width()).arg(expected.height()));
        if (passInput != inPng)
            QFile::remove(passInput);  // intermediate 2x image is no longer needed
        passInput = passOutput;
        passSize = expected;
        if (cancel.load())
            return canceled();
    }

    // 4. Read back the final result.
    report(Stage::Encode, 0);
    QImageReader reader(outPng, "png");
    QImage result = reader.read();
    if (result.isNull())
        return fail(tr("Cannot read the AI output: %1").arg(reader.errorString()));
    const QSize expected(inSize.width() * scale, inSize.height() * scale);
    if (result.size() != expected)
        return fail(tr("The AI output has an unexpected size (%1 x %2, expected %3 x %4).")
                        .arg(result.width()).arg(result.height())
                        .arg(expected.width()).arg(expected.height()));
    result = result.convertToFormat(hasAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    if (result.isNull())
        return fail(tr("Out of memory while converting the AI output."));
    QFile::remove(outPng);
    if (cancel.load())
        return canceled();

    // 5. Encode into a temporary file inside the output folder.
    const QString stem = OutputNaming::stem(sourcePath, scale);
    const QString ext = ImageEncoder::extension(spec.encode.format);
    const QString tmpPath = OutputNaming::tempPath(spec.outputDir, stem);
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(tr("Cannot create a file in the output folder: %1").arg(tmp.errorString()));
    TempFileCleanup cleanup(tmp);

    QString encodeError;
    if (!ImageEncoder::encode(result, spec.encode, &tmp, &encodeError))
        return fail(encodeError);
    if (!tmp.flush() || tmp.error() != QFileDevice::NoError)
        return fail(tr("Failed to write the output file: %1").arg(tmp.errorString()));
    tmp.close();
    if (tmp.error() != QFileDevice::NoError || QFileInfo(tmpPath).size() <= 0)
        return fail(tr("Failed to write the output file: %1").arg(tmp.errorString()));

    if (cancel.load())
        return canceled();

    // 6. Commit under the final, non-overwriting name.
    QString commitError;
    const QString finalPath = OutputNaming::commit(tmpPath, spec.outputDir, stem, ext, &commitError);
    if (finalPath.isEmpty())
        return fail(commitError);
    cleanup.release();

    ItemResult ok;
    ok.ok = true;
    ok.outputPath = finalPath;
    return ok;
}

} // namespace arx
