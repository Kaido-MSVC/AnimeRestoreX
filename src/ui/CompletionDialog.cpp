#include "ui/CompletionDialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

CompletionDialog::CompletionDialog(const arx::BatchSummary &s, const QString &outputDir,
                                   const QStringList &failures, QWidget *parent)
    : QDialog(parent), m_outputDir(outputDir)
{
    setWindowTitle(s.canceled ? tr("Processing Canceled") : tr("Processing Finished"));

    auto *layout = new QVBoxLayout(this);

    QStringList lines;
    lines << tr("Succeeded: %1").arg(s.succeeded) << tr("Failed: %1").arg(s.failed);
    if (s.notProcessed > 0)
        lines << tr("Not processed: %1").arg(s.notProcessed);
    m_summary = new QLabel(lines.join(QLatin1Char('\n')), this);
    QFont f = m_summary->font();
    f.setPointSizeF(f.pointSizeF() * 1.2);
    m_summary->setFont(f);
    layout->addWidget(m_summary);

    if (!s.fatalError.isEmpty()) {
        auto *fatal = new QLabel(tr("Processing stopped: %1").arg(s.fatalError), this);
        fatal->setWordWrap(true);
        fatal->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(fatal);
    } else if (s.canceled) {
        layout->addWidget(new QLabel(tr("Processing was canceled."), this));
    }
    if (s.succeeded == 0)
        layout->addWidget(new QLabel(tr("No output files were created."), this));

    if (!failures.isEmpty()) {
        layout->addWidget(new QLabel(tr("Failed items:"), this));
        auto *list = new QPlainTextEdit(failures.join(QLatin1Char('\n')), this);
        list->setReadOnly(true);
        list->setLineWrapMode(QPlainTextEdit::NoWrap);
        list->setMinimumSize(420, 120);
        layout->addWidget(list);
    }

    auto *buttons = new QDialogButtonBox(this);
    m_openButton = buttons->addButton(tr("Open Output Folder"), QDialogButtonBox::ActionRole);
    QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
    close->setDefault(true);
    connect(m_openButton, &QPushButton::clicked, this, [this] { openFolder(this, m_outputDir); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString CompletionDialog::summaryText() const
{
    return m_summary->text();
}

bool CompletionDialog::openFolder(QWidget *parent, const QString &dir)
{
    const QFileInfo fi(dir);
    if (dir.isEmpty() || !fi.exists() || !fi.isDir()) {
        QMessageBox::warning(parent, tr("Open Output Folder"),
                             tr("The output folder no longer exists:\n%1")
                                 .arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()))) {
        QMessageBox::warning(parent, tr("Open Output Folder"),
                             tr("Cannot open the folder:\n%1").arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    return true;
}
