#include "core/Language.h"

#include <QStringList>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace arx {

QString languageCode(Language lang)
{
    switch (lang) {
    case Language::English:            return QStringLiteral("en");
    case Language::TraditionalChinese: return QStringLiteral("zh_TW");
    case Language::Japanese:           return QStringLiteral("ja");
    }
    return QStringLiteral("en");
}

std::optional<Language> languageFromCode(const QString &code)
{
    if (code == QLatin1String("en"))
        return Language::English;
    if (code == QLatin1String("zh_TW"))
        return Language::TraditionalChinese;
    if (code == QLatin1String("ja"))
        return Language::Japanese;
    return std::nullopt;
}

QString languageDisplayName(Language lang)
{
    switch (lang) {
    case Language::English:            return QStringLiteral("English");
    case Language::TraditionalChinese: return QStringLiteral("繁體中文");
    case Language::Japanese:           return QStringLiteral("日本語");
    }
    return QStringLiteral("English");
}

Language languageFromUiTag(const QString &tag)
{
    QString t = tag.trimmed();
    t.replace(QLatin1Char('_'), QLatin1Char('-'));
    const QStringList parts = t.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return Language::English;

    const QString lang = parts.first().toLower();
    if (lang == QLatin1String("ja"))
        return Language::Japanese;
    if (lang != QLatin1String("zh"))
        return Language::English;

    // Chinese: an explicit script decides; otherwise only TW/HK/MO regions are Traditional.
    QString script;
    QString region;
    for (int i = 1; i < parts.size(); ++i) {
        const QString &p = parts.at(i);
        if (p.size() == 4 && script.isEmpty() && region.isEmpty())
            script = p.toLower();
        else if ((p.size() == 2 || p.size() == 3) && region.isEmpty())
            region = p.toUpper();
    }
    if (!script.isEmpty())
        return script == QLatin1String("hant") ? Language::TraditionalChinese : Language::English;
    if (region == QLatin1String("TW") || region == QLatin1String("HK") || region == QLatin1String("MO"))
        return Language::TraditionalChinese;
    return Language::English;
}

QString windowsPrimaryUiLanguage()
{
    ULONG count = 0;
    ULONG size = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) || size == 0)
        return {};
    std::vector<wchar_t> buffer(size);
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size))
        return {};
    // Double-null-terminated list; the first entry is the primary UI language.
    return QString::fromWCharArray(buffer.data());
}

Language effectiveLanguage(const std::optional<Language> &manual, const QString &systemTag)
{
    if (manual)
        return *manual;
    return languageFromUiTag(systemTag);
}

} // namespace arx
