#include "core/Waifu2xRunner.h"
#include "core/ChildProcessGuard.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace arx {

QString Waifu2xRunner::checkInstall(const Waifu2xParams &p)
{
    if (!QFileInfo(p.exePath).isFile())
        return tr("The AI engine (waifu2x-ncnn-vulkan.exe) is missing: %1")
            .arg(QDir::toNativeSeparators(p.exePath));

    // waifu2x-ncnn-vulkan builds the model file path with swprintf into a
    // wchar_t[256] buffer (src/main.cpp, 20250915); longer paths fail to load.
    const QString longest = QDir::toNativeSeparators(
        QFileInfo(p.modelDir).absoluteFilePath() + QStringLiteral("/noise0_scale2.0x_model.param"));
    if (longest.size() >= 256)
        return tr("The program folder path is too long for the AI engine (%1 characters, the limit is 255). "
                  "Please move AnimeRestoreX to a shorter folder path.")
            .arg(longest.size());

    if (!QFileInfo(p.modelDir).isDir())
        return tr("The AI model folder is missing: %1").arg(QDir::toNativeSeparators(p.modelDir));

    // cunet uses "noiseN_scale2.0x_model" for 2x/4x, or "scale2.0x_model" for noise -1.
    const QString base = p.noise < 0 ? QStringLiteral("scale2.0x_model")
                                     : QStringLiteral("noise%1_scale2.0x_model").arg(p.noise);
    const QDir dir(p.modelDir);
    for (const QString &ext : {QStringLiteral(".param"), QStringLiteral(".bin")}) {
        if (!QFileInfo(dir.filePath(base + ext)).isFile())
            return tr("The AI model file is missing: %1")
                .arg(QDir::toNativeSeparators(dir.filePath(base + ext)));
    }
    return {};
}

QStringList Waifu2xRunner::arguments(const Waifu2xParams &p, const QString &inPng,
                                     const QString &outPng)
{
    return {
        QStringLiteral("-i"), QDir::toNativeSeparators(inPng),
        QStringLiteral("-o"), QDir::toNativeSeparators(outPng),
        QStringLiteral("-n"), QString::number(p.noise),
        QStringLiteral("-s"), QString::number(p.scale),
        QStringLiteral("-t"), QString::number(p.tile),
        QStringLiteral("-m"), QDir::toNativeSeparators(QFileInfo(p.modelDir).absoluteFilePath()),
        QStringLiteral("-f"), QStringLiteral("png"),
    };
}

QString Waifu2xRunner::summarizeStderr(const QByteArray &stderrData)
{
    QStringList kept;
    const QString text = QString::fromLocal8Bit(stderrData);
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('[')))
            continue;  // "[0 NVIDIA ...]" device info lines
        kept << line;
    }
    return kept.join(QLatin1Char(' ')).left(500);
}

RunResult Waifu2xRunner::run(const Waifu2xParams &p, const QString &inPng, const QString &outPng,
                             const std::atomic_bool &cancel)
{
    RunResult r;

    QProcess proc;
    proc.setProgram(p.exePath);
    proc.setArguments(arguments(p, inPng, outPng));
    proc.setWorkingDirectory(QFileInfo(p.exePath).absolutePath());
    proc.setStandardInputFile(QProcess::nullDevice());
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(QIODevice::ReadOnly);

    if (!proc.waitForStarted(15000)) {
        r.error = tr("Cannot start the AI engine: %1").arg(proc.errorString());
        r.fatal = true;
        return r;
    }
    ChildProcessGuard::instance().assign(proc.processId());

    QByteArray err;
    while (!proc.waitForFinished(200)) {
        err += proc.readAllStandardError();
        proc.readAllStandardOutput();
        if (proc.state() == QProcess::NotRunning)
            break;
        if (cancel.load()) {
            proc.kill();
            proc.waitForFinished(5000);
            r.canceled = true;
            return r;
        }
    }
    err += proc.readAllStandardError();

    const QString detail = summarizeStderr(err);
    if (proc.exitStatus() == QProcess::CrashExit) {
        r.error = tr("The AI engine crashed.");
        if (!detail.isEmpty())
            r.error += QLatin1Char(' ') + detail;
        return r;
    }
    if (proc.exitCode() != 0) {
        if (detail.contains(QLatin1String("gpu"), Qt::CaseInsensitive)
            || detail.contains(QLatin1String("vulkan"), Qt::CaseInsensitive)) {
            r.error = tr("No usable Vulkan GPU was found. Please check the graphics driver. (%1)")
                          .arg(detail);
            r.fatal = true;
        } else {
            r.error = tr("The AI engine failed (exit code %1). %2").arg(proc.exitCode()).arg(detail);
        }
        return r;
    }

    // Exit code 0: the caller verifies the output. Keep stderr for diagnostics.
    r.ok = true;
    r.error = detail;
    return r;
}

} // namespace arx
