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
// Schema 2 (v2.0, nunif ONNX on Windows ML): the old engine's tile value
// ("model/tileSize": 0/400/200/100) is dropped and the tile starts at Auto again;
// the measured speeds of the old engine ("progress/...") are removed. Language,
// folders, window state and the denoise level (-1..3, same meaning) are kept.
class AppSettings {
public:
    static constexpr int kSchema = 2;
    static constexpr int kDefaultNoise = 1;
    static constexpr int kDefaultTile = 0;   // auto: 256, 160 when VRAM is short or after OOM

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
