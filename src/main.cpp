#include "core/AppSettings.h"
#include "core/BatchRunner.h"
#include "core/JobSpec.h"
#include "core/Language.h"
#include "core/ProcessJob.h"
#include "core/inference/EngineHost.h"
#include "ui/AppIcon.h"
#include "ui/LanguageManager.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QImageReader>
#include <QMessageBox>
#include <QMetaType>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kaido-MSVC"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimeRestoreX"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    // --settings-dir <folder>: keep all settings in an INI file below that folder instead
    // of the registry (manual checks and screenshots must not touch the real settings,
    // which v1.2 shares). Must run before the first QSettings object is created.
    {
        const QStringList args = QCoreApplication::arguments();
        const qsizetype i = args.indexOf(QStringLiteral("--settings-dir"));
        if (i >= 0 && i + 1 < args.size()) {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, args.at(i + 1));
        }
    }

    // 72 MP RGBA is ~288 MB; raise Qt's default reader limit (official example uses 1024 MB).
    QImageReader::setAllocationLimit(1024);

    QApplication::setWindowIcon(appIcon());
    // v1.2 theme; follows the Windows light / dark setting while running.
    theme::install(app);

    qRegisterMetaType<arx::JobSpec>();
    qRegisterMetaType<arx::ItemResult>();
    qRegisterMetaType<arx::BatchSummary>();
    qRegisterMetaType<QList<arx::ItemPlan>>();

    // Language: manual setting first, otherwise the Windows UI language rule.
    // The automatic result is never written back as a manual choice.
    {
        const arx::AppSettings settings;
        LanguageManager::instance().apply(
            arx::effectiveLanguage(settings.manualLanguage(), arx::windowsPrimaryUiLanguage()));
    }

    // AI runtime: onnxruntime.dll / DirectML.dll from the program folder only (never
    // System32 or the working directory), with the exact ORT version. A failure is shown
    // in words; the window still opens (settings, file list) but processing will not start.
    const arx::core::RuntimeInfo &runtime = arx::core::EngineHost::initRuntime(
        QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString(), ARX_ORT_VERSION);

    MainWindow w;
    w.show();
    if (!runtime.status.ok())
        QMessageBox::critical(&w, QCoreApplication::translate("main", "AnimeRestoreX"),
                              arx::ProcessJob::runtimeErrorMessage(runtime.status));
    return app.exec();
}
