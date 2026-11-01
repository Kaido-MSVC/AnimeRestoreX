#pragma once

#include "core/Language.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QGroupBox;
class QDialogButtonBox;

// "Settings" window: AI model parameters (denoise, tile) and language.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int noise, int tile, arx::Language language, QWidget *parent = nullptr);

    int noiseLevel() const;
    int tileSize() const;
    arx::Language language() const;
    // True only if the user picked a different language than the one shown at open.
    bool languageChanged() const { return language() != m_initialLanguage; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    QGroupBox *m_modelGroup = nullptr;
    QGroupBox *m_langGroup = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_modelName = nullptr;
    QLabel *m_noiseLabel = nullptr;
    QLabel *m_tileLabel = nullptr;
    QLabel *m_tileHint = nullptr;
    QLabel *m_langLabel = nullptr;
    QComboBox *m_noise = nullptr;
    QComboBox *m_tile = nullptr;
    QComboBox *m_lang = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    arx::Language m_initialLanguage;
};
