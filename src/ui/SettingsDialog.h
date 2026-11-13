#pragma once

#include "core/AppSettings.h"
#include "core/Language.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QGroupBox;
class QDialogButtonBox;

// "Settings" window: advanced AI model parameters (tile size, processing device, TTA) and
// language. The denoise level lives in the main window since v1.1.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int tile, arx::ComputeDevice device, bool tta, arx::Language language,
                   QWidget *parent = nullptr);

    int tileSize() const;
    arx::ComputeDevice computeDevice() const;
    bool tta() const;
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
    QGroupBox *m_langGroup = nullptr;
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
