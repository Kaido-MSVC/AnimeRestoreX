#include "ui/AboutDialog.h"
#include "ui/AppIcon.h"
#include "ui/LicensesDialog.h"

#include "core/inference/EngineHost.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

struct ModelSource {
    QString release;  // file name of the release archive
    QString commit;   // nunif commit the models were exported with (short)
    QString url;      // upstream project page
};

QList<ModelSource> modelSources(const QString &manifestPath)
{
    QFile f(manifestPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QList<ModelSource> out;
    const QJsonObject sources = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("sources")).toObject();
    for (const QJsonValue &v : sources) {
        const QJsonObject source = v.toObject();
        ModelSource s;
        s.release = QUrl(source.value(QStringLiteral("url")).toString()).fileName();
        s.commit = source.value(QStringLiteral("exported_with_commit")).toString().left(7);
        const QString project = source.value(QStringLiteral("project")).toString();
        if (!project.isEmpty())
            s.url = QStringLiteral("https://github.com/") + project;
        out << s;
    }
    return out;
}

// "1.15.4" from the version resource of a DLL ("" if it has none).
QString fileVersion(const QString &path)
{
    const std::wstring p = QDir::toNativeSeparators(path).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(p.c_str(), &handle);
    if (size == 0)
        return {};
    QByteArray data(int(size), '\0');
    if (!GetFileVersionInfoW(p.c_str(), 0, size, data.data()))
        return {};
    VS_FIXEDFILEINFO *info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.constData(), L"\\", reinterpret_cast<void **>(&info), &len) || !info)
        return {};
    return QStringLiteral("%1.%2.%3")
        .arg(HIWORD(info->dwProductVersionMS))
        .arg(LOWORD(info->dwProductVersionMS))
        .arg(HIWORD(info->dwProductVersionLS));
}

} // namespace

void AboutDialog::setDeviceName(const QString &device)
{
    m_device = device;
    retranslate();
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(appIcon().pixmap(QSize(64, 64), devicePixelRatioF()));  // sharp at 125%/150%
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
    m_body->setMinimumWidth(fontMetrics().averageCharWidth() * 64);  // follows font and scale
    root->addWidget(m_body);

    auto *buttons = new QDialogButtonBox(this);
    m_licenses = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    m_openLicenses = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    m_aboutQt = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    m_close = buttons->addButton(QDialogButtonBox::Close);
    m_close->setDefault(true);
    connect(m_openLicenses, &QPushButton::clicked, this,
            [this] { openLicenseFolder(this, QCoreApplication::applicationDirPath()); });
    connect(m_licenses, &QPushButton::clicked, this, [this] {
        LicensesDialog dlg(QCoreApplication::applicationDirPath(), this);
        dlg.exec();
    });
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
    html += QStringLiteral("<p>%1</p>").arg(tr("AI denoising (1x) and 2x / 4x upscaling for anime illustrations.").toHtmlEscaped());
    html += QStringLiteral("<p>%1</p>").arg(
        tr("The AnimeRestoreX source code is released under the MIT License. "
           "The MIT License covers only AnimeRestoreX itself; the third-party components "
           "below remain under their own licenses.").toHtmlEscaped());
    html += QStringLiteral("<p><b>%1</b></p><ul style='margin-top:0'>").arg(tr("Third-party components").toHtmlEscaped());
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dml = fileVersion(QDir(appDir).filePath(QStringLiteral("DirectML.dll")));
    html += item(QStringLiteral("ONNX Runtime %1 (Microsoft)").arg(QStringLiteral(ARX_ORT_VERSION)),
                 tr("AI inference runtime; runs the models on the GPU through DirectML, or on the CPU "
                    "(MIT License)."));
    html += item(QStringLiteral("DirectML %1 (Microsoft)").arg(dml.isEmpty() ? QStringLiteral("?") : dml),
                 tr("GPU acceleration for AI inference (Microsoft Software License Terms)."));
    html += item(QStringLiteral("waifu2x swin_unet_v3/art, swin_unet/art models (nagadomi/nunif)"),
                 tr("Trained models for anime-style illustrations, converted for DirectML (MIT License)."));
    for (const ModelSource &src : modelSources(QDir(appDir).filePath(QStringLiteral("models/manifest.json"))))
        if (!src.release.isEmpty())
            html += QStringLiteral("<li style='list-style:none'><small>%1</small></li>")
                        .arg(tr("Model files: %1, exported from nunif %2; source: %3")
                                 .arg(src.release, src.commit, src.url)
                                 .toHtmlEscaped());
    html += item(QStringLiteral("Qt 6.12 (The Qt Company)"),
                 tr("User interface and image reading and writing (GNU LGPL v3)."));
    html += QStringLiteral("</ul><p>%1</p>").arg(
        tr("All license terms and third-party notices are listed under Licenses and Notices.").toHtmlEscaped());
    // Runtime actually loaded in this process (checked at start-up) and the device used.
    QStringList state;
    if (const arx::core::RuntimeInfo *rt = arx::core::EngineHost::runtime())
        state << (rt->status.ok()
                      ? tr("AI runtime: %1").arg(QString::fromStdString(rt->details))
                      : tr("AI runtime not available: %1").arg(QString::fromStdString(rt->status.message)));
    state << (m_device.isEmpty() ? tr("Processing device: not used yet in this session")
                                 : tr("Processing device: %1").arg(m_device));
    for (const QString &line : std::as_const(state))
        html += QStringLiteral("<p style='margin:0'><small>%1</small></p>").arg(line.toHtmlEscaped());
    m_body->setText(html);

    m_licenses->setText(tr("Licenses and Notices"));
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
