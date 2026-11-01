#include "ui/AboutDialog.h"
#include "ui/AppIcon.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(appIcon().pixmap(64, 64));
    head->addWidget(icon, 0, Qt::AlignTop);
    m_title = new QLabel(this);
    m_title->setTextFormat(Qt::RichText);
    head->addWidget(m_title, 1);
    root->addLayout(head);

    m_body = new QLabel(this);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_body->setOpenExternalLinks(true);
    m_body->setMinimumWidth(460);
    root->addWidget(m_body);

    auto *buttons = new QDialogButtonBox(this);
    m_openLicenses = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    m_aboutQt = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    m_close = buttons->addButton(QDialogButtonBox::Close);
    m_close->setDefault(true);
    connect(m_openLicenses, &QPushButton::clicked, this,
            [this] { openLicenseFolder(this, QCoreApplication::applicationDirPath()); });
    connect(m_aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    retranslate();
}

void AboutDialog::retranslate()
{
    setWindowTitle(tr("About AnimeRestoreX"));
    m_title->setText(QStringLiteral("<h2 style='margin:0'>AnimeRestoreX</h2><p>%1<br>%2</p>")
                         .arg(tr("Version %1").arg(QStringLiteral(ARX_VERSION)).toHtmlEscaped(),
                              QStringLiteral("Copyright (c) 2026 Kaido-MSVC")));

    auto item = [](const QString &name, const QString &what) {
        return QStringLiteral("<li><b>%1</b> — %2</li>").arg(name.toHtmlEscaped(), what.toHtmlEscaped());
    };
    QString html;
    html += QStringLiteral("<p>%1</p>").arg(tr("AI denoising and 2x / 4x upscaling for anime illustrations.").toHtmlEscaped());
    html += QStringLiteral("<p>%1</p>").arg(
        tr("The AnimeRestoreX source code is released under the MIT License. "
           "The MIT License covers only AnimeRestoreX itself; the third-party components "
           "below remain under their own licenses.").toHtmlEscaped());
    html += QStringLiteral("<p><b>%1</b></p><ul style='margin-top:0'>").arg(tr("Third-party components").toHtmlEscaped());
    html += item(QStringLiteral("waifu2x-ncnn-vulkan (nihui)"),
                 tr("AI inference engine that performs the denoising and upscaling (MIT License)."));
    html += item(QStringLiteral("ncnn (Tencent)"),
                 tr("Neural network inference framework with Vulkan GPU acceleration, built into waifu2x-ncnn-vulkan (BSD 3-Clause License)."));
    html += item(QStringLiteral("waifu2x CUnet models (nagadomi/waifu2x)"),
                 tr("Trained models for anime-style illustrations (MIT License)."));
    html += item(QStringLiteral("Qt 6.12 (The Qt Company)"),
                 tr("User interface and image reading and writing (GNU LGPL v3)."));
    html += QStringLiteral("</ul><p>%1</p>").arg(
        tr("See THIRD_PARTY_NOTICES.md and the licenses folder for the complete list and license texts.").toHtmlEscaped());
    m_body->setText(html);

    m_openLicenses->setText(tr("Open License Files"));
    m_aboutQt->setText(tr("About Qt"));
}

void AboutDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

QString AboutDialog::bodyText() const
{
    return m_title->text() + m_body->text();
}

bool AboutDialog::licenseFilesPresent(const QString &dir)
{
    const QDir d(dir);
    return QFileInfo(d.filePath(QStringLiteral("LICENSE"))).isFile()
        && QFileInfo(d.filePath(QStringLiteral("THIRD_PARTY_NOTICES.md"))).isFile();
}

bool AboutDialog::openLicenseFolder(QWidget *parent, const QString &dir)
{
    if (!licenseFilesPresent(dir)) {
        QMessageBox::warning(parent, tr("Open License Files"),
                             tr("The license files (LICENSE, THIRD_PARTY_NOTICES.md) were not found in:\n%1")
                                 .arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dir).absoluteFilePath()))) {
        QMessageBox::warning(parent, tr("Open License Files"),
                             tr("Cannot open the folder:\n%1").arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    return true;
}
