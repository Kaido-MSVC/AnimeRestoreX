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

QString EulaDialog::currentVersion()
{
    return QStringLiteral(ARX_EULA_VERSION);
}

QString EulaDialog::agreementText()
{
    QFile f(QStringLiteral(":/legal/EULA.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

bool EulaDialog::isAccepted(const QString &accepted)
{
    return !accepted.isEmpty() && accepted == currentVersion();
}

QString EulaDialog::saveFailedMessage()
{
    return tr("Your acceptance of the license agreement could not be saved. "
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

    // About 80 columns of the agreement and 30 lines, but never larger than the screen.
    const QFontMetrics fm(m_text->font());
    QSize want(fm.horizontalAdvance(QLatin1Char('M')) * 84, fm.lineSpacing() * 30);
    if (const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen())
        want = want.boundedTo(screen->availableGeometry().size() * 0.85);
    resize(want);

    retranslate();
}

void EulaDialog::retranslate()
{
    setWindowTitle(tr("AnimeRestoreX License Agreement"));
    if (m_mode == Mode::Consent) {
        m_intro->setText(
            tr("Before you use AnimeRestoreX, please read the license agreement below. It includes the "
               "Microsoft license terms for the Windows ML Runtime files (onnxruntime.dll, DirectML.dll) "
               "that come with AnimeRestoreX. The agreement is shown in its original English.\n"
               "If you do not agree, AnimeRestoreX will close."));
        m_accept->setText(tr("I have read and accept the license agreement (version %1).").arg(currentVersion()));
        m_agree->setText(tr("I Agree"));
        m_decline->setText(tr("I Do Not Agree"));
    } else {
        m_intro->setText(tr("License agreement version %1 (original English text).")
                             .arg(currentVersion()));
    }
}

void EulaDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}
