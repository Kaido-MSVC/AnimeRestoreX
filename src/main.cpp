#include "core/AppSettings.h"
#include "core/BatchRunner.h"
#include "core/ChildProcessGuard.h"
#include "core/JobSpec.h"
#include "core/Language.h"
#include "ui/AppIcon.h"
#include "ui/LanguageManager.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QImageReader>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kaido-MSVC"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimeRestoreX"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    // 72 MP RGBA is ~288 MB; raise Qt's default reader limit (official example uses 1024 MB).
    QImageReader::setAllocationLimit(1024);

    QApplication::setWindowIcon(appIcon());

    qRegisterMetaType<arx::JobSpec>();
    qRegisterMetaType<arx::ItemResult>();
    qRegisterMetaType<arx::BatchSummary>();

    // Create the kill-on-close job object up front.
    arx::ChildProcessGuard::instance();

    // Language: manual setting first, otherwise the Windows UI language rule.
    // The automatic result is never written back as a manual choice.
    {
        const arx::AppSettings settings;
        LanguageManager::instance().apply(
            arx::effectiveLanguage(settings.manualLanguage(), arx::windowsPrimaryUiLanguage()));
    }

    MainWindow w;
    w.show();
    return app.exec();
}
