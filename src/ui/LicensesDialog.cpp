#include "ui/LicensesDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kRoleFile = Qt::UserRole;      // relative path of a text
constexpr int kRoleFolder = Qt::UserRole + 1;  // first path part below licenses/

const QString kLicense = QStringLiteral("LICENSE");
const QString kNotices = QStringLiteral("THIRD_PARTY_NOTICES.md");

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString text = QString::fromUtf8(f.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

} // namespace

QStringList LicensesDialog::licenseFiles(const QString &appDir)
{
    const QDir root(appDir);
    QStringList files;
    for (const QString &top : {kLicense, kNotices})
        if (QFileInfo(root.filePath(top)).isFile())
            files << top;
    QStringList below;
    QDirIterator it(root.filePath(QStringLiteral("licenses")), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString name = it.fileName();
        if (name.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) != 0 && name != QStringLiteral("index.json"))
            below << root.relativeFilePath(it.filePath());
    }
    std::sort(below.begin(), below.end(),
              [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return files + below;
}

LicensesDialog::LicensesDialog(const QString &appDir, QWidget *parent) : QDialog(parent), m_appDir(appDir)
{
    auto *root = new QVBoxLayout(this);

    m_intro = new QLabel(this);
    m_intro->setWordWrap(true);
    root->addWidget(m_intro);

    m_missing = new QLabel(this);
    m_missing->setObjectName(QStringLiteral("licensesMissingLabel"));
    m_missing->setWordWrap(true);
    m_missing->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_missing->hide();
    root->addWidget(m_missing);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    m_tree = new QTreeWidget(splitter);
    m_tree->setObjectName(QStringLiteral("licensesFileTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_text = new QPlainTextEdit(splitter);
    m_text->setObjectName(QStringLiteral("licensesText"));
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));  // notices align by column
    splitter->addWidget(m_tree);
    splitter->addWidget(m_text);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(this);
    m_close = buttons->addButton(QDialogButtonBox::Close);
    m_close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { showCurrent(); });

    buildTree();
    retranslate();

    // About 80 columns of text and 30 lines beside the list, but never larger than the screen.
    const QFontMetrics fm(m_text->font());
    const int listWidth = fontMetrics().averageCharWidth() * 34;
    QSize want(fm.horizontalAdvance(QLatin1Char('M')) * 84 + listWidth, fm.lineSpacing() * 30);
    if (const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen())
        want = want.boundedTo(screen->availableGeometry().size() * 0.85);
    resize(want);
    splitter->setSizes({listWidth, want.width() - listWidth});
    m_tree->setFocus();
}

void LicensesDialog::buildTree()
{
    m_tree->clear();
    QTreeWidgetItem *first = nullptr;
    QHash<QString, QTreeWidgetItem *> folders;
    for (const QString &file : licenseFiles(m_appDir)) {
        QStringList parts = file.split(QLatin1Char('/'));
        QTreeWidgetItem *parent = nullptr;
        QString path;
        if (parts.size() > 1 && parts.first() == QStringLiteral("licenses"))
            parts.removeFirst();
        for (int i = 0; i + 1 < parts.size(); ++i) {
            path += QLatin1Char('/') + parts[i];
            QTreeWidgetItem *&node = folders[path];
            if (!node) {
                node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
                node->setText(0, parts[i]);
                node->setFlags(Qt::ItemIsEnabled);   // a folder holds no text of its own
                if (!parent)
                    node->setData(0, kRoleFolder, parts[i]);
                else if (parts[i] == QStringLiteral("third_party") || i > 1)
                    node->setExpanded(false);
            }
            parent = node;
        }
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, parts.last());
        item->setData(0, kRoleFile, file);
        item->setToolTip(0, file);
        if (!first)
            first = item;
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        m_tree->topLevelItem(i)->setExpanded(true);
    if (first)
        m_tree->setCurrentItem(first);
    m_tree->setEnabled(first != nullptr);
    m_text->setEnabled(first != nullptr);
}

void LicensesDialog::showCurrent()
{
    const QString file = currentFile();
    if (file.isEmpty()) {
        m_text->clear();
        return;
    }
    m_text->setPlainText(readText(QDir(m_appDir).filePath(file)));
}

QString LicensesDialog::currentFile() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kRoleFile).toString() : QString();
}

bool LicensesDialog::selectFile(const QString &relativePath)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kRoleFile).toString() == relativePath) {
            m_tree->setCurrentItem(*it);
            return true;
        }
    return false;
}

QString LicensesDialog::componentName(const QString &folder) const
{
    if (folder == QStringLiteral("ONNXRuntime"))
        return QStringLiteral("ONNX Runtime");
    if (folder == QStringLiteral("nunif"))
        return tr("nunif (AI models)");
    return folder;
}

void LicensesDialog::retranslate()
{
    setWindowTitle(tr("Licenses and Notices"));
    m_intro->setText(tr("License terms and third-party notices of AnimeRestoreX and everything it includes. "
                        "Each text is shown in its original language."));
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const QString file = item->data(0, kRoleFile).toString();
        if (file == kLicense)
            item->setText(0, tr("AnimeRestoreX license"));
        else if (file == kNotices)
            item->setText(0, tr("Third-party notices"));
        else
            item->setText(0, componentName(item->data(0, kRoleFolder).toString()));
    }
    const bool empty = m_tree->topLevelItemCount() == 0;
    m_missing->setVisible(empty);
    if (empty)
        m_missing->setText(tr("The license files were not found in:\n%1\n"
                              "Extract the whole AnimeRestoreX folder again to restore them.")
                               .arg(QDir::toNativeSeparators(m_appDir)));
}

void LicensesDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}
