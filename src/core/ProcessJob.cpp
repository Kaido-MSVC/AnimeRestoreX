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

ItemResult ProcessJob::run(const QString &sourcePath, const JobSpec &spec,
                           const std::atomic_bool &cancel)
{
    // 1. Load and validate.
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

    // 3. Inference.
    const RunResult run = Waifu2xRunner::run(spec.waifu2x, inPng, outPng, cancel);
    if (run.canceled)
        return canceled();
    if (!run.ok) {
        ItemResult r = fail(run.error);
        r.fatal = run.fatal;
        return r;
    }

    // 4. Read back and verify. waifu2x exits with 0 on decode errors, so the
    //    output file and its exact size are the real success criteria.
    if (!QFileInfo::exists(outPng)) {
        QString msg = tr("The AI engine did not produce an output image.");
        if (!run.error.isEmpty())
            msg += QLatin1Char(' ') + run.error;
        return fail(msg);
    }
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
