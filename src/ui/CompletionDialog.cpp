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
                                   const QList<Item> &items, QWidget *parent)
    : QDialog(parent), m_outputDir(outputDir)
{
    setWindowTitle(s.canceled ? tr("Processing Canceled") : tr("Processing Finished"));

    auto *layout = new QVBoxLayout(this);

    QStringList lines;
    lines << tr("Succeeded: %1").arg(s.succeeded) << tr("Failed: %1").arg(s.failed);
    if (s.notProcessed > 0)
        lines << tr("Not processed: %1").arg(s.notProcessed);
    if (s.smallerTile > 0)
        lines << tr("Made with the smaller tile size %1: %2")
                     .arg(arx::core::kFallbackTile)
                     .arg(s.smallerTile);
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

    if (!items.isEmpty()) {
        // One section per kind, in the order of the list in the main window.
        QStringList text;
        auto section = [&](Item::Kind kind, const QString &title) {
            QStringList lines;
            for (const Item &it : items) {
                if (it.kind != kind)
                    continue;
                QString line = QStringLiteral("  ") + it.file;
                if (!it.detail.isEmpty())
                    line += kind == Item::Succeeded ? QStringLiteral(" → ") + it.detail
                                                    : QStringLiteral(": ") + it.detail;
                if (it.smallerTile > 0)
                    line += QLatin1Char(' ') + tr("[tile %1]").arg(it.smallerTile);
                lines << line;
            }
            if (lines.isEmpty())
                return;
            if (!text.isEmpty())
                text << QString();
            text << title.arg(lines.size());
            text << lines;
        };
        section(Item::Failed, tr("Failed (%1):"));
        section(Item::NotProcessed, tr("Not processed (%1):"));
        section(Item::Succeeded, tr("Succeeded (%1):"));
        m_items = new QPlainTextEdit(text.join(QLatin1Char('\n')), this);
        m_items->setObjectName(QStringLiteral("itemList"));
        m_items->setReadOnly(true);
        m_items->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_items->setMinimumSize(fontMetrics().averageCharWidth() * 60, fontMetrics().lineSpacing() * 8);
        layout->addWidget(m_items);
        if (s.smallerTile > 0) {
            auto *note = new QLabel(tr("[tile %1]").arg(arx::core::kFallbackTile) + QLatin1Char(' ') +
                                        tr("Auto had to use the smaller tile size %1 because of a GPU memory "
                                           "shortage or a GPU error. The tile size changes the result slightly.")
                                            .arg(arx::core::kFallbackTile),
                                    this);
            note->setWordWrap(true);
            layout->addWidget(note);
        }
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

QString CompletionDialog::itemsText() const
{
    return m_items ? m_items->toPlainText() : QString();
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
