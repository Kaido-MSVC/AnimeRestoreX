#include "core/ChildProcessGuard.h"
#include "core/JobSpec.h"
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

    qRegisterMetaType<arx::JobSpec>();
    qRegisterMetaType<arx::ItemResult>();

    // Create the kill-on-close job object up front.
    arx::ChildProcessGuard::instance();

    MainWindow w;
    w.show();
    return app.exec();
}
