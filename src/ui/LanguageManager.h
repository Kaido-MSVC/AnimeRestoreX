#pragma once

#include "core/Language.h"

#include <QTranslator>

// Installs the app translation (embedded :/i18n/animerestorex_<code>.qm) and
// Qt's own qtbase translation. Installing triggers QEvent::LanguageChange,
// which every window handles with retranslateUi (no restart needed).
class LanguageManager {
public:
    static LanguageManager &instance();

    // Returns false if the translation could not be loaded; English is used then.
    bool apply(arx::Language lang);
    arx::Language current() const { return m_current; }

private:
    LanguageManager() = default;
    QTranslator m_app;
    QTranslator m_qt;
    bool m_appInstalled = false;
    bool m_qtInstalled = false;
    arx::Language m_current = arx::Language::English;
};
