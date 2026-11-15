#include "ui/EulaDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

bool EulaDialog::required()
{
#ifdef ARX_EULA_SHA256
    return true;
#else
    return false;
#endif
}

QString EulaDialog::termsId()
{
#ifdef ARX_EULA_SHA256
    return QStringLiteral(ARX_EULA_SHA256);
#else
    return {};
#endif
}

QString EulaDialog::agreementText()
{
    QFile f(QStringLiteral(":/legal/WindowsML-license.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

bool EulaDialog::isAccepted(const QString &accepted)
{
    return !required() || (!accepted.isEmpty() && accepted == termsId());
}

QString EulaDialog::saveFailedMessage()
{
    return tr("Your acceptance of the license terms could not be saved. "
              "You will be asked again the next time AnimeRestoreX starts.");
}

EulaDialog::EulaDialog(Mode mode, QWidget *parent) : QDialog(parent), m_mode(mode)
{
    auto *root = new QVBoxLayout(this);

    m_intro = new QLabel(this);
    m_intro->setWordWrap(true);
    root->addWidget(m_intro);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setPlainText(agreementText());
    root->addWidget(m_text, 1);

    auto *buttons = new QDialogButtonBox(this);
    if (mode == Mode::Consent) {
        m_accept = new QCheckBox(this);
        root->addWidget(m_accept);
        m_agree = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
        m_decline = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
        m_agree->setEnabled(false);
        m_decline->setDefault(true);
        connect(m_accept, &QCheckBox::toggled, m_agree, &QPushButton::setEnabled);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    } else {
        m_close = buttons->addButton(QDialogButtonBox::Close);
        m_close->setDefault(true);
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // About 80 columns of the terms and 30 lines, but never larger than the screen.
    const QFontMetrics fm(m_text->font());
    QSize want(fm.horizontalAdvance(QLatin1Char('M')) * 84, fm.lineSpacing() * 30);
    if (const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen())
        want = want.boundedTo(screen->availableGeometry().size() * 0.85);
    resize(want);

    retranslate();
}

void EulaDialog::retranslate()
{
    setWindowTitle(tr("Microsoft Windows ML Runtime License Terms"));
    if (m_mode == Mode::Consent) {
        m_intro->setText(
            tr("AnimeRestoreX comes with files of the Microsoft Windows ML Runtime (onnxruntime.dll, "
               "DirectML.dll, Microsoft.Windows.AI.MachineLearning.dll). Before you use AnimeRestoreX, "
               "please read the Microsoft license terms for these files below. The terms are shown in "
               "their original English.\n"
               "If you do not agree, AnimeRestoreX will close."));
        m_accept->setText(tr("I have read and accept the Microsoft Windows ML Runtime license terms."));
        m_agree->setText(tr("I Agree"));
        m_decline->setText(tr("I Do Not Agree"));
    } else {
        m_intro->setText(tr("License terms for onnxruntime.dll, DirectML.dll and "
                            "Microsoft.Windows.AI.MachineLearning.dll (original English text)."));
    }
}

void EulaDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}
