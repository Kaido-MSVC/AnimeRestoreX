#pragma once

#include <QString>

#include <optional>

namespace arx {

enum class Language { English, TraditionalChinese, Japanese };

// Settings value <-> enum ("en", "zh_TW", "ja"). Unknown values -> nullopt.
QString languageCode(Language lang);
std::optional<Language> languageFromCode(const QString &code);

// Native display name ("English", "繁體中文", "日本語"); never translated.
QString languageDisplayName(Language lang);

// Maps a BCP-47 UI language tag (e.g. "zh-TW", "zh-Hant-HK", "ja-JP") to the
// app language. Traditional Chinese: zh-TW / zh-HK / zh-MO or an explicit
// Hant script. Japanese: ja*. Everything else, including zh-CN, zh-SG,
// zh-Hans* and bare "zh", falls back to English.
Language languageFromUiTag(const QString &tag);

// First entry of GetUserPreferredUILanguages(MUI_LANGUAGE_NAME); empty on failure.
QString windowsPrimaryUiLanguage();

// Priority: valid manual setting > Windows UI language rule > English.
Language effectiveLanguage(const std::optional<Language> &manual, const QString &systemTag);

} // namespace arx
