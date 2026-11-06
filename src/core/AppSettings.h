#pragma once

#include "core/JobSpec.h"
#include "core/Language.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>

class QSettings;

namespace arx {

// User-scope persistent settings (QSettings, organisation "Kaido-MSVC",
// application "AnimeRestoreX"). Source and output folders use separate keys.
//
// v1.2 and v2.0 share one settings store, without breaking each other:
//  - shared (same meaning in both): ui/language, paths/*, window/*;
//  - v2.0 only, under "v2/": denoise level, tile, device, measured speeds, schema;
//  - v1.2 keys (model/*, progress/*) are only read, never removed or overwritten.
// The first v2.0 start copies the v1.2 denoise level into v2/; the tile starts at Auto.
class AppSettings {
public:
    static constexpr int kSchema = 2;  // "v2/schema"
    static constexpr int kDefaultNoise = 1;
    static constexpr int kDefaultTile = 0;   // auto: 256, 160 when VRAM is short or after OOM

    AppSettings();
    ~AppSettings();

    // Read-only access at start-up: does not create an AppSettings object, so nothing (not
    // even the schema migration) is written before the language is applied.
    static std::optional<Language> storedManualLanguage();

    // Only a valid, explicitly chosen language is returned.
    std::optional<Language> manualLanguage() const;
    void setManualLanguage(Language lang);

    QString sourceDir() const;   // raw stored value (may be stale)
    void setSourceDir(const QString &dir);
    QString outputDir() const;
    void setOutputDir(const QString &dir);

    int noiseLevel() const;      // -1..3, invalid -> default
    void setNoiseLevel(int level);
    int tileSize() const;        // 0 (auto), 160, 256, 400; invalid -> default
    void setTileSize(int tile);
    ComputeDevice computeDevice() const;  // invalid -> Gpu
    void setComputeDevice(ComputeDevice device);

    // Opaque blobs from QWidget::saveGeometry() / QSplitter::saveState(); empty if unset.
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &state);
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);

    // Measured processing speeds for the progress estimate, kept per device
    // (GPU name as the engine reports it): rate key -> seconds per megapixel.
    // Invalid values are dropped.
    QHash<QString, double> progressRates(const QString &device) const;
    void setProgressRates(const QString &device, const QHash<QString, double> &rates);
    // Device of the most recent measurement (a guess until the engine reports it).
    QString progressDevice() const;
    void setProgressDevice(const QString &device);

    // Flushes to storage; false if the settings could not be saved.
    bool sync();

    static bool isValidTile(int tile);

    // Applies the schema migration once (called by the constructor; idempotent).
    void migrate();

private:
    std::unique_ptr<QSettings> m_s;
};

// Returns `stored` if it is an existing folder, otherwise the fallback
// (Pictures, then Documents, then home). *fellBack tells which one was used.
QString resolveFolder(const QString &stored, bool *fellBack = nullptr);
QString defaultImageFolder();

} // namespace arx
