#include "ui/LanguageManager.h"

#include <QCoreApplication>
#include <QLibraryInfo>

LanguageManager &LanguageManager::instance()
{
    static LanguageManager mgr;
    return mgr;
}

bool LanguageManager::apply(arx::Language lang)
{
    if (m_appInstalled) {
        QCoreApplication::removeTranslator(&m_app);
        m_appInstalled = false;
    }
    if (m_qtInstalled) {
        QCoreApplication::removeTranslator(&m_qt);
        m_qtInstalled = false;
    }
    m_current = arx::Language::English;

    if (lang == arx::Language::English) {
        // Source strings are English; removing translators already sent LanguageChange.
        return true;
    }

    const QString code = arx::languageCode(lang);
    if (!m_app.load(QStringLiteral(":/i18n/animerestorex_%1.qm").arg(code))) {
        return false;  // translation missing -> stay English
    }

    // Qt's own strings (standard buttons etc.). Deployed builds ship them in
    // <app>/translations, which QLibraryInfo resolves without a qt.conf.
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (m_qt.load(QStringLiteral("qtbase_%1").arg(code), qtDir)
        || m_qt.load(QStringLiteral("qt_%1").arg(code), qtDir)) {
        m_qtInstalled = QCoreApplication::installTranslator(&m_qt);
    }
    m_appInstalled = QCoreApplication::installTranslator(&m_app);
    if (m_appInstalled)
        m_current = lang;
    return m_appInstalled;
}
