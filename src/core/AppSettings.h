#pragma once

#include "core/Language.h"

#include <QString>

#include <memory>
#include <optional>

class QSettings;

namespace arx {

// User-scope persistent settings (QSettings, organisation "Kaido-MSVC",
// application "AnimeRestoreX"). Source and output folders use separate keys.
class AppSettings {
public:
    static constexpr int kDefaultNoise = 1;  // provisional until A9
    static constexpr int kDefaultTile = 0;   // auto, provisional until A9

    AppSettings();
    ~AppSettings();

    // Only a valid, explicitly chosen language is returned.
    std::optional<Language> manualLanguage() const;
    void setManualLanguage(Language lang);

    QString sourceDir() const;   // raw stored value (may be stale)
    void setSourceDir(const QString &dir);
    QString outputDir() const;
    void setOutputDir(const QString &dir);

    int noiseLevel() const;      // -1..3, invalid -> default
    void setNoiseLevel(int level);
    int tileSize() const;        // 0 (auto), 400, 200, 100; invalid -> default
    void setTileSize(int tile);

    // Flushes to storage; false if the settings could not be saved.
    bool sync();

    static bool isValidTile(int tile);

private:
    std::unique_ptr<QSettings> m_s;
};

// Returns `stored` if it is an existing folder, otherwise the fallback
// (Pictures, then Documents, then home). *fellBack tells which one was used.
QString resolveFolder(const QString &stored, bool *fellBack = nullptr);
QString defaultImageFolder();

} // namespace arx
