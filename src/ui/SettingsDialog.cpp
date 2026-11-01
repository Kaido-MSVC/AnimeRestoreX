#include "ui/SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(int tile, arx::Language language, QWidget *parent)
    : QDialog(parent), m_initialLanguage(language)
{
    auto *root = new QVBoxLayout(this);

    m_modelGroup = new QGroupBox(this);
    auto *model = new QFormLayout(m_modelGroup);
    m_modelLabel = new QLabel(this);
    m_modelName = new QLabel(QStringLiteral("waifu2x CUnet (models-cunet)"), this);
    model->addRow(m_modelLabel, m_modelName);
    m_tileLabel = new QLabel(this);
    m_tile = new QComboBox(this);
    for (int t : {0, 400, 200, 100})
        m_tile->addItem(QString(), t);
    model->addRow(m_tileLabel, m_tile);
    m_tileHint = new QLabel(this);
    m_tileHint->setWordWrap(true);
    model->addRow(QString(), m_tileHint);
    root->addWidget(m_modelGroup);

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

    m_tile->setCurrentIndex(std::max(0, m_tile->findData(tile)));
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(int(language))));
    retranslate();
}

void SettingsDialog::retranslate()
{
    setWindowTitle(tr("Settings"));
    m_modelGroup->setTitle(tr("AI Model"));
    m_modelLabel->setText(tr("Model:"));
    m_tileLabel->setText(tr("Tile size:"));
    m_tile->setItemText(0, tr("Auto"));
    m_tile->setItemText(1, QStringLiteral("400"));
    m_tile->setItemText(2, QStringLiteral("200"));
    m_tile->setItemText(3, QStringLiteral("100"));
    m_tileHint->setText(tr("Smaller tiles use less GPU memory but are slower. "
                           "Try a smaller size if processing fails with large images."));
    m_langGroup->setTitle(tr("Language"));
    m_langLabel->setText(tr("Interface language:"));
}

void SettingsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

int SettingsDialog::tileSize() const { return m_tile->currentData().toInt(); }
arx::Language SettingsDialog::language() const { return arx::Language(m_lang->currentData().toInt()); }
