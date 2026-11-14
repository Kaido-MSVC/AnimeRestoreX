#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const SettingsValues &values, arx::Language language, QWidget *parent)
    : QDialog(parent), m_initialLanguage(language)
{
    auto *root = new QVBoxLayout(this);

    m_modelGroup = new QGroupBox(this);
    auto *model = new QFormLayout(m_modelGroup);
    m_modelLabel = new QLabel(this);
    m_modelName = new QLabel(QStringLiteral("waifu2x swin_unet_v3 / swin_unet (nunif)"), this);
    model->addRow(m_modelLabel, m_modelName);
    m_tileLabel = new QLabel(this);
    m_tile = new QComboBox(this);
    // Only tiles validated on the target GPU; 640 and larger are not offered.
    for (int t : {0, 160, 256, 352, 448})
        m_tile->addItem(QString(), t);
    model->addRow(m_tileLabel, m_tile);
    m_tileHint = new QLabel(this);
    m_tileHint->setWordWrap(true);
    // A fixed text width (follows font and scale), so the dialog width does not depend on
    // the language and the wrapped height is computed for the width actually used.
    const int hintWidth = fontMetrics().horizontalAdvance(QLatin1Char('M')) * 26;
    m_tileHint->setMinimumWidth(hintWidth);
    model->addRow(QString(), m_tileHint);
    m_deviceLabel = new QLabel(this);
    m_device = new QComboBox(this);
    m_device->addItem(QString(), int(arx::ComputeDevice::Gpu));
    m_device->addItem(QString(), int(arx::ComputeDevice::Cpu));
    model->addRow(m_deviceLabel, m_device);
    m_deviceHint = new QLabel(this);
    m_deviceHint->setWordWrap(true);
    m_deviceHint->setMinimumWidth(hintWidth);
    model->addRow(QString(), m_deviceHint);
    // Off by default: about 8x the processing time.
    m_tta = new QCheckBox(this);
    model->addRow(QString(), m_tta);
    m_ttaHint = new QLabel(this);
    m_ttaHint->setWordWrap(true);
    m_ttaHint->setMinimumWidth(hintWidth);
    model->addRow(QString(), m_ttaHint);
    root->addWidget(m_modelGroup);

    // Output file options (2.1), both off by default. The ICC profile is always kept.
    m_outputGroup = new QGroupBox(this);
    auto *output = new QFormLayout(m_outputGroup);
    m_gray = new QCheckBox(this);
    output->addRow(m_gray);
    m_grayHint = new QLabel(this);
    m_grayHint->setWordWrap(true);
    m_grayHint->setMinimumWidth(hintWidth);
    output->addRow(m_grayHint);
    m_exif = new QCheckBox(this);
    output->addRow(m_exif);
    m_exifHint = new QLabel(this);
    m_exifHint->setWordWrap(true);
    m_exifHint->setMinimumWidth(hintWidth);
    output->addRow(m_exifHint);
    root->addWidget(m_outputGroup);

    m_langGroup = new QGroupBox(this);
    auto *lang = new QFormLayout(m_langGroup);
    m_langLabel = new QLabel(this);
    m_lang = new QComboBox(this);
    for (auto l : {arx::Language::TraditionalChinese, arx::Language::Japanese, arx::Language::English})
        m_lang->addItem(arx::languageDisplayName(l), int(l));  // native names, never translated
    lang->addRow(m_langLabel, m_lang);
    root->addWidget(m_langGroup);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttons);

    m_tile->setCurrentIndex(std::max(0, m_tile->findData(values.tile)));
    m_device->setCurrentIndex(std::max(0, m_device->findData(int(values.device))));
    m_tta->setChecked(values.tta);
    m_gray->setChecked(values.grayscale);
    m_exif->setChecked(values.keepExif);
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(int(language))));
    connect(m_device, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateDeviceHint);
    retranslate();
}

void SettingsDialog::updateDeviceHint()
{
    if (computeDevice() == arx::ComputeDevice::Cpu)
        m_deviceHint->setText(tr("The CPU is much slower than the GPU: on the test PC about 5 times "
                                 "(GTX 1060), on slower CPUs more. Use it only if the GPU cannot be used."));
    else
        m_deviceHint->setText(tr("If the GPU fails, processing stops; it does not switch to the CPU "
                                 "on its own."));
    fitToContents();
}

void SettingsDialog::fitToContents()
{
    // Wrapped labels changed their text: recompute the height for the current width
    // (adjustSize uses heightForWidth), so no line is cut off in any language.
    if (QLayout *l = layout())
        l->activate();
    adjustSize();
}

void SettingsDialog::retranslate()
{
    setWindowTitle(tr("Settings"));
    m_modelGroup->setTitle(tr("AI Model"));
    m_modelLabel->setText(tr("Model:"));
    m_tileLabel->setText(tr("Tile size:"));
    m_tile->setItemText(0, tr("Auto"));
    m_tile->setItemText(1, QStringLiteral("160"));
    m_tile->setItemText(2, QStringLiteral("256"));
    m_tile->setItemText(3, QStringLiteral("352"));
    m_tile->setItemText(4, QStringLiteral("448"));
    m_tileHint->setText(tr("Auto uses 256, or 160 when GPU memory is short or runs out. "
                           "Smaller tiles use less GPU memory but are slower; 448 needs about "
                           "3 GB of GPU memory. The tile size slightly changes the result."));
    m_deviceLabel->setText(tr("Processing device:"));
    m_device->setItemText(0, tr("GPU (DirectML)"));
    m_device->setItemText(1, tr("CPU (much slower)"));
    updateDeviceHint();
    m_tta->setText(tr("TTA (test-time augmentation)"));
    m_ttaHint->setText(tr("Each image is processed 8 times, flipped and rotated, and the results are "
                          "averaged. Processing takes about 8 times as long. The largest 4x images "
                          "with transparency use about 4.4 GB of memory; 16 GB of RAM is recommended."));
    m_outputGroup->setTitle(tr("Output"));
    m_gray->setText(tr("Save as grayscale"));
    m_grayHint->setText(tr("Grayscale source images are always saved as grayscale. PNG with "
                           "transparency and WebP keep three equal color channels."));
    m_exif->setText(tr("Keep EXIF information"));
    m_exifHint->setText(tr("Copies the camera, date and other EXIF data of the source image. EXIF may "
                           "contain personal information, such as the location where a photo was taken."));
    m_langGroup->setTitle(tr("Language"));
    m_langLabel->setText(tr("Interface language:"));
    fitToContents();
}

void SettingsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

int SettingsDialog::tileSize() const { return m_tile->currentData().toInt(); }
arx::ComputeDevice SettingsDialog::computeDevice() const
{
    return arx::ComputeDevice(m_device->currentData().toInt());
}
bool SettingsDialog::tta() const { return m_tta->isChecked(); }
bool SettingsDialog::grayscale() const { return m_gray->isChecked(); }
bool SettingsDialog::keepExif() const { return m_exif->isChecked(); }
SettingsValues SettingsDialog::values() const
{
    SettingsValues v;
    v.tile = tileSize();
    v.device = computeDevice();
    v.tta = tta();
    v.grayscale = grayscale();
    v.keepExif = keepExif();
    return v;
}
arx::Language SettingsDialog::language() const { return arx::Language(m_lang->currentData().toInt()); }
