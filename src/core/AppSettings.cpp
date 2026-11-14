#include "core/AppSettings.h"
#include "core/Progress.h"
#include "core/inference/TilePolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace arx {

namespace {
// Shared with v1.2 (same meaning, read and written by both versions).
const QString kLanguageKey = QStringLiteral("ui/language");
const QString kSourceKey = QStringLiteral("paths/sourceDir");
const QString kOutputKey = QStringLiteral("paths/outputDir");
const QString kGeometryKey = QStringLiteral("window/geometry");
const QString kSplitterKey = QStringLiteral("window/splitter");

// v1.2 keys that v2.0 only reads, once, when its own group is created.
const QString kV1NoiseKey = QStringLiteral("model/noiseLevel");

// v2.0 only (nunif ONNX on ONNX Runtime). v1.2 never sees these.
const QString kSchemaKey = QStringLiteral("v2/schema");
const QString kNoiseKey = QStringLiteral("v2/model/noiseLevel");
const QString kTileKey = QStringLiteral("v2/inference/tileSize");
const QString kDeviceKey = QStringLiteral("v2/inference/device");
const QString kTtaKey = QStringLiteral("v2/inference/tta");  // 2.1; missing in older files = off
const QString kGrayKey = QStringLiteral("v2/output/grayscale");  // 2.1; missing = off
const QString kProgressDeviceKey = QStringLiteral("v2/progress/lastDevice");
const QString kOldEulaKey = QStringLiteral("v2/eula/acceptedVersion");  // early 2.0 test builds; removed by schema 2

// "v2/progress/rates/<device>"; "/" and "\" separate QSettings groups, so they
// are replaced in the device name.
QString ratesGroup(QString device)
{
    device.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (device.trimmed().isEmpty())
        device = QStringLiteral("unknown");
    return QStringLiteral("v2/progress/rates/") + device;
}

bool isExistingDir(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QFileInfo fi(dir);
    return fi.exists() && fi.isDir();
}
} // namespace

AppSettings::AppSettings() : m_s(std::make_unique<QSettings>()) { migrate(); }
AppSettings::~AppSettings() = default;

void AppSettings::migrate()
{
    // Non-destructive: v1.2 keeps working with its own keys, so nothing outside "v2/"
    // is ever removed or written here. The v2 group starts from the v1.2 denoise level
    // (same meaning); the tile starts at Auto (v1.2 tiles are not valid swin_unet tiles)
    // and the v1.2 speeds are ignored (other engine).
    bool ok = false;
    const int schema = m_s->value(kSchemaKey, 0).toInt(&ok);
    if (ok && schema >= kSchema)
        return;
    if (!m_s->contains(kNoiseKey)) {
        const int v1 = m_s->value(kV1NoiseKey).toInt(&ok);
        if (ok && v1 >= -1 && v1 <= 3)
            m_s->setValue(kNoiseKey, v1);
    }
    // Schema 2: tile 400 is no longer offered, 352 is the nearest size below it. The first-run
    // agreement is gone, so its version is dropped. Both keys are v2-only.
    if (schema < 2) {
        if (m_s->value(kTileKey).toInt() == 400)
            m_s->setValue(kTileKey, 352);
        m_s->remove(kOldEulaKey);
    }
    m_s->setValue(kSchemaKey, kSchema);
}

std::optional<Language> AppSettings::storedManualLanguage()
{
    const QSettings s;  // reading never writes
    return languageFromCode(s.value(kLanguageKey).toString());
}

std::optional<Language> AppSettings::manualLanguage() const
{
    return languageFromCode(m_s->value(kLanguageKey).toString());
}

void AppSettings::setManualLanguage(Language lang)
{
    m_s->setValue(kLanguageKey, languageCode(lang));
}

QString AppSettings::sourceDir() const { return m_s->value(kSourceKey).toString(); }
void AppSettings::setSourceDir(const QString &dir) { m_s->setValue(kSourceKey, dir); }
QString AppSettings::outputDir() const { return m_s->value(kOutputKey).toString(); }
void AppSettings::setOutputDir(const QString &dir) { m_s->setValue(kOutputKey, dir); }

int AppSettings::noiseLevel() const
{
    bool ok = false;
    const int v = m_s->value(kNoiseKey, kDefaultNoise).toInt(&ok);
    return (ok && v >= -1 && v <= 3) ? v : kDefaultNoise;
}

void AppSettings::setNoiseLevel(int level) { m_s->setValue(kNoiseKey, level); }

bool AppSettings::isValidTile(int tile)
{
    // Auto, or a tile that is legal for every model and was validated on the GTX 1060 6GB
    // (docs/v2.0-P1重做報告.md §6). 640+ is not offered.
    // Same list as the engine-side check (ProcessJob::checkOptions).
    return core::isOfferedTile(tile);
}

int AppSettings::tileSize() const
{
    bool ok = false;
    const int v = m_s->value(kTileKey, kDefaultTile).toInt(&ok);
    return (ok && isValidTile(v)) ? v : kDefaultTile;
}

void AppSettings::setTileSize(int tile) { m_s->setValue(kTileKey, tile); }

ComputeDevice AppSettings::computeDevice() const
{
    return m_s->value(kDeviceKey).toString() == QLatin1String("cpu") ? ComputeDevice::Cpu
                                                                     : ComputeDevice::Gpu;
}

void AppSettings::setComputeDevice(ComputeDevice device)
{
    m_s->setValue(kDeviceKey, device == ComputeDevice::Cpu ? QStringLiteral("cpu")
                                                           : QStringLiteral("gpu"));
}

bool AppSettings::tta() const
{
    // Only an explicit "true" turns it on: a missing or garbled value keeps the 8x slower mode off.
    return m_s->value(kTtaKey).toString() == QLatin1String("true");
}

void AppSettings::setTta(bool on) { m_s->setValue(kTtaKey, on); }

// Same rule as TTA: only an explicit "true" turns an option on, so files from older versions
// (no key) and garbled values keep the old behaviour.
bool AppSettings::grayscaleOutput() const { return m_s->value(kGrayKey).toString() == QLatin1String("true"); }
void AppSettings::setGrayscaleOutput(bool on) { m_s->setValue(kGrayKey, on); }

QByteArray AppSettings::windowGeometry() const { return m_s->value(kGeometryKey).toByteArray(); }
void AppSettings::setWindowGeometry(const QByteArray &state) { m_s->setValue(kGeometryKey, state); }
QByteArray AppSettings::splitterState() const { return m_s->value(kSplitterKey).toByteArray(); }
void AppSettings::setSplitterState(const QByteArray &state) { m_s->setValue(kSplitterKey, state); }

QHash<QString, double> AppSettings::progressRates(const QString &device) const
{
    QHash<QString, double> rates;
    m_s->beginGroup(ratesGroup(device));
    for (const QString &key : m_s->allKeys()) {
        bool ok = false;
        const double v = m_s->value(key).toDouble(&ok);
        if (ok && ProgressEstimator::isValidRate(v))
            rates.insert(key, v);
    }
    m_s->endGroup();
    return rates;
}

void AppSettings::setProgressRates(const QString &device, const QHash<QString, double> &rates)
{
    m_s->beginGroup(ratesGroup(device));
    m_s->remove(QString());  // the whole group of this device
    for (auto it = rates.cbegin(); it != rates.cend(); ++it)
        if (ProgressEstimator::isValidRate(it.value()))
            m_s->setValue(it.key(), it.value());
    m_s->endGroup();
}

QString AppSettings::progressDevice() const { return m_s->value(kProgressDeviceKey).toString(); }
void AppSettings::setProgressDevice(const QString &device) { m_s->setValue(kProgressDeviceKey, device); }

bool AppSettings::sync()
{
    m_s->sync();
    return m_s->status() == QSettings::NoError;
}

QString defaultImageFolder()
{
    for (auto loc : {QStandardPaths::PicturesLocation, QStandardPaths::DocumentsLocation,
                     QStandardPaths::HomeLocation}) {
        const QString dir = QStandardPaths::writableLocation(loc);
        if (isExistingDir(dir))
            return QDir::cleanPath(dir);
    }
    return QDir::homePath();
}

QString resolveFolder(const QString &stored, bool *fellBack)
{
    const bool ok = isExistingDir(stored);
    if (fellBack)
        *fellBack = !ok;
    return ok ? QDir::cleanPath(stored) : defaultImageFolder();
}

} // namespace arx
