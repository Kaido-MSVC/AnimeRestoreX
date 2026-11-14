#pragma once

#include "core/AppSettings.h"
#include "core/Language.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QGroupBox;
class QDialogButtonBox;

// Values the settings dialog edits (stored in AppSettings).
struct SettingsValues {
    int tile = 0;  // 0 = Auto
    arx::ComputeDevice device = arx::ComputeDevice::Gpu;
    bool tta = false;
    bool grayscale = false;  // save every output as grayscale
    bool keepExif = false;   // copy the source EXIF into the output
    bool grain = false;      // film grain
    double grainStrength = arx::kGrainStrengthDefault;  // nunif grain_strength
};

// "Settings" window: advanced AI model parameters (tile size, processing device, TTA), output
// options (grayscale, EXIF) and language. The denoise level lives in the main window since v1.1.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const SettingsValues &values, arx::Language language, QWidget *parent = nullptr);

    SettingsValues values() const;
    int tileSize() const;
    arx::ComputeDevice computeDevice() const;
    bool tta() const;
    bool grayscale() const;
    bool keepExif() const;
    bool grain() const;
    double grainStrength() const;
    arx::Language language() const;
    // True only if the user picked a different language than the one shown at open.
    bool languageChanged() const { return language() != m_initialLanguage; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();
    void updateDeviceHint();
    void fitToContents();

    QGroupBox *m_modelGroup = nullptr;
    QGroupBox *m_outputGroup = nullptr;
    QGroupBox *m_langGroup = nullptr;
    QCheckBox *m_gray = nullptr;
    QLabel *m_grayHint = nullptr;
    QCheckBox *m_exif = nullptr;
    QLabel *m_exifHint = nullptr;
    QCheckBox *m_grain = nullptr;
    QLabel *m_grainLabel = nullptr;
    QDoubleSpinBox *m_grainStrength = nullptr;
    QLabel *m_grainHint = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_modelName = nullptr;
    QLabel *m_tileLabel = nullptr;
    QLabel *m_tileHint = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_deviceHint = nullptr;
    QLabel *m_ttaHint = nullptr;
    QLabel *m_langLabel = nullptr;
    QCheckBox *m_tta = nullptr;
    QComboBox *m_tile = nullptr;
    QComboBox *m_device = nullptr;
    QComboBox *m_lang = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    arx::Language m_initialLanguage;
};
