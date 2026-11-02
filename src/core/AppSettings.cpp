#include "core/AppSettings.h"
#include "core/Progress.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace arx {

namespace {
const QString kLanguageKey = QStringLiteral("ui/language");
const QString kSourceKey = QStringLiteral("paths/sourceDir");
const QString kOutputKey = QStringLiteral("paths/outputDir");
const QString kNoiseKey = QStringLiteral("model/noiseLevel");
const QString kTileKey = QStringLiteral("model/tileSize");
const QString kGeometryKey = QStringLiteral("window/geometry");
const QString kSplitterKey = QStringLiteral("window/splitter");
const QString kProgressDeviceKey = QStringLiteral("progress/lastDevice");

// "progress/rates/<device>"; "/" and "\" separate QSettings groups, so they
// are replaced in the device name.
QString ratesGroup(QString device)
{
    device.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (device.trimmed().isEmpty())
        device = QStringLiteral("unknown");
    return QStringLiteral("progress/rates/") + device;
}

bool isExistingDir(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QFileInfo fi(dir);
    return fi.exists() && fi.isDir();
}
} // namespace

AppSettings::AppSettings() : m_s(std::make_unique<QSettings>()) {}
AppSettings::~AppSettings() = default;

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
    return tile == 0 || tile == 400 || tile == 200 || tile == 100;
}

int AppSettings::tileSize() const
{
    bool ok = false;
    const int v = m_s->value(kTileKey, kDefaultTile).toInt(&ok);
    return (ok && isValidTile(v)) ? v : kDefaultTile;
}

void AppSettings::setTileSize(int tile) { m_s->setValue(kTileKey, tile); }

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
