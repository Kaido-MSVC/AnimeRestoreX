#include "ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "core/AppSettings.h"
#include "core/FolderScanner.h"
#include "core/ProcessJob.h"
#include "ui/AboutDialog.h"
#include "ui/AppIcon.h"
#include "ui/CompletionDialog.h"
#include "ui/ElidedLabel.h"
#include "ui/WindowPlacement.h"
#include "ui/JobWorker.h"
#include "ui/LanguageManager.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QPainterPath>
#include <QStyle>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProxyStyle>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleState = Qt::UserRole + 1;
constexpr int kRoleDetail = Qt::UserRole + 2;
constexpr int kRoleOutput = Qt::UserRole + 3;
constexpr int kRoleSmallerTile = Qt::UserRole + 4;  // Auto used a smaller tile (0 = no)

enum Column { ColFile = 0, ColStatus = 1, ColDetail = 2 };

// Accent frame drawn over the file list while files are dragged over the window.
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(28);
        p.fillRect(rect(), fill);
        const int w = std::max(2, fontMetrics().averageCharWidth() / 3);
        p.setPen(QPen(palette().color(QPalette::Highlight), w));
        p.drawRect(rect().adjusted(w / 2, w / 2, -(w + 1) / 2, -(w + 1) / 2));
    }
};

// The Windows 11 style leaves only a few pixels between the longest menu text
// and its shortcut. Items with a shortcut ("text\tCtrl+O") get a wider gap.
class MenuGapStyle : public QProxyStyle {
public:
    explicit MenuGapStyle(QObject *parent)
        : QProxyStyle(QStyleFactory::create(QApplication::style()->name()))
    {
        setParent(parent);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override
    {
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_MenuItem) {
            if (const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
                mi && mi->text.contains(QLatin1Char('\t')))
                s.rwidth() += mi->fontMetrics.averageCharWidth() * 4;
        }
        return s;
    }
};

// Rich-text tooltips wrap long lines instead of being cut off.
QString wrapTooltip(const QString &text)
{
    if (text.isEmpty())
        return {};
    return QStringLiteral("<p style='white-space:pre-wrap'>%1</p>").arg(text.toHtmlEscaped());
}

} // namespace

static QString stateText(MainWindow::ItemState s);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(std::make_unique<Ui::MainWindow>()),
      m_settings(std::make_unique<arx::AppSettings>())
{
    ui->setupUi(this);
    setWindowIcon(appIcon());
    // One-line status bar: long messages are elided instead of growing the window.
    m_status = new ElidedLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    statusBar()->addWidget(m_status, 1);
    // Windows menu layout: File / Tools / Help.
    m_fileMenu = menuBar()->addMenu(QString());
    // Shortcuts live on menu actions so the menus show them.
    m_addImagesAction = m_fileMenu->addAction(QString(), this, &MainWindow::browseImages);
    m_addImagesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    m_addFolderAction = m_fileMenu->addAction(QString(), this, &MainWindow::browseFolder);
    m_addFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    m_fileMenu->addSeparator();
    m_startAction = m_fileMenu->addAction(QString(), this, &MainWindow::start);
    // The first sequence is the one shown ("Ctrl+Enter"); both Enter keys work.
    m_startAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Enter), QKeySequence(Qt::CTRL | Qt::Key_Return)});
    m_fileMenu->addSeparator();
    m_exitAction = m_fileMenu->addAction(QString(), this, &QWidget::close);
    m_toolsMenu = menuBar()->addMenu(QString());
    m_settingsAction = m_toolsMenu->addAction(QString(), this, &MainWindow::openSettings);
    m_settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(QString(), this, &MainWindow::openAbout);
    auto *menuStyle = new MenuGapStyle(this);
    for (QMenu *menu : {m_fileMenu, m_toolsMenu, m_helpMenu})
        menu->setStyle(menuStyle);
    // Delete removes the selected items, only while the list has the focus.
    m_removeAction = new QAction(this);
    m_removeAction->setShortcut(QKeySequence::Delete);
    m_removeAction->setShortcutContext(Qt::WidgetShortcut);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    ui->fileList->addAction(m_removeAction);
    // No shortcut for Cancel on purpose: a stray key must not stop a long batch.
    setAcceptDrops(true);
    populateCombos();

    // Empty list: a centered hint instead of a blank white area. Two short
    // lines, not wrapped (wrapping would split CJK words); the list is kept
    // at least as wide as the hint (updateEmptyHintWidth).
    m_emptyHint = new QLabel(ui->fileList->viewport());
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(false);
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyHint->setForegroundRole(QPalette::PlaceholderText);
    m_dropOverlay = new DropOverlay(ui->fileList->viewport());

    // Equal halves by default: the list rows carry thumbnails since v1.2.
    // (A splitter position saved by the user is restored later and wins.)
    ui->mainSplitter->setStretchFactor(0, 1);
    ui->mainSplitter->setStretchFactor(1, 1);
    // File names first: the name column takes the free space and is elided in the middle.
    ui->fileList->setTextElideMode(Qt::ElideMiddle);
    QHeaderView *header = ui->fileList->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    // Status has a fixed width that fits every state text plus some air
    // (updateStatusColumnWidth), so the column titles do not run together.
    header->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    header->setSectionResizeMode(ColDetail, QHeaderView::Interactive);
    // The Details column follows the list width so the name column keeps most of it.
    ui->fileList->viewport()->installEventFilter(this);
    // Thumbnails: one row per image with a small preview next to the name.
    const int thumb = thumbnailSide();
    ui->fileList->setIconSize(QSize(thumb, thumb));
    ui->fileList->setAlternatingRowColors(false);
    m_thumbPool.setMaxThreadCount(2);
    updateDetailColumnWidth();

    // Progress: a text line above each thin bar ("AI processing 1/2 · est. 34%").
    m_itemProgressLabel = new ElidedLabel(this);
    m_itemProgressLabel->setObjectName(QStringLiteral("itemProgressLabel"));
    m_batchProgressLabel = new ElidedLabel(this);
    m_batchProgressLabel->setObjectName(QStringLiteral("batchProgressLabel"));
    ui->progressLayout->insertWidget(0, m_itemProgressLabel);
    ui->progressLayout->insertWidget(2, m_batchProgressLabel);
    ui->progressLayout->setSpacing(3);
    ui->itemProgressBar->setTextVisible(false);
    ui->progressBar->setTextVisible(false);
    setItemProgressVisible(false);
    resetProgress();

    m_worker = new JobWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &JobWorker::planned, this, [this](const QList<arx::ItemPlan> &plans) {
        m_estimator.startBatch(plans, m_clock.elapsed());
    });
    connect(m_worker, &JobWorker::itemStarted, this, &MainWindow::onItemStarted);
    connect(m_worker, &JobWorker::stageStarted, this, [this](int index, int stage, int pass, int passCount) {
        m_estimator.stageStarted(index, arx::StageEvent{arx::Stage(stage), pass, passCount},
                                 m_clock.elapsed());
        refreshProgress();
    });
    connect(m_worker, &JobWorker::stageFraction, this, [this](int index, double fraction) {
        m_estimator.stageFraction(index, fraction, m_clock.elapsed());
    });
    connect(m_worker, &JobWorker::prepared, this, [this](int tile, bool smallerForVram) {
        m_vramTile = smallerForVram ? tile : 0;
        if (m_vramTile)
            m_status->setText(tr("Not enough GPU memory for tile size 256: this job uses tile size %1.")
                                  .arg(m_vramTile));
    });
    connect(m_worker, &JobWorker::retrying, this, [this](int index, int tile) {
        m_retryTile = tile;
        m_estimator.stageRestarted(index, m_clock.elapsed());
        if (QTreeWidgetItem *item = m_jobItems.value(index))
            m_status->setText(tr("GPU error: processing %1 again with tile size %2...")
                                  .arg(item->text(ColFile))
                                  .arg(tile));
        refreshProgress();
    });
    connect(m_worker, &JobWorker::deviceDetected, this, &MainWindow::onDeviceDetected);
    connect(m_worker, &JobWorker::itemFinished, this, &MainWindow::onItemFinished);
    connect(m_worker, &JobWorker::jobFinished, this, &MainWindow::onJobFinished);
    m_thread.start();
    m_clock.start();
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(250);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::refreshProgress);

    connect(ui->addImagesButton, &QPushButton::clicked, this, &MainWindow::browseImages);
    connect(ui->addFolderButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(ui->clearButton, &QPushButton::clicked, this, &MainWindow::clearList);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(ui->cancelButton, &QPushButton::clicked, this, [this] {
        m_worker->requestCancel();
        ui->cancelButton->setEnabled(false);
        // The image encoders cannot be interrupted: a file being encoded is finished
        // first (and then discarded), which can take a few seconds for large outputs.
        const arx::ProgressSnapshot snap = m_estimator.snapshot(m_clock.elapsed());
        if (snap.index >= 0 && snap.stage == arx::Stage::Encode)
            m_status->setText(tr("Canceling: finishing the encoding of the current file..."));
        else
            m_status->setText(tr("Canceling..."));
    });
    connect(ui->fileList, &QTreeWidget::currentItemChanged, this, &MainWindow::updatePreview);
    connect(ui->fileList, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(ui->formatCombo, &QComboBox::currentIndexChanged, this, [this] {
        updateQualityControls();
        onOutputSpecChanged();
    });
    connect(ui->webpModeCombo, &QComboBox::currentIndexChanged, this, [this] {
        updateQualityControls();
        onOutputSpecChanged();
    });
    connect(ui->scaleCombo, &QComboBox::currentIndexChanged, this, [this] {
        updateNoiseChoices();
        onOutputSpecChanged();
        onProcessingChanged();
    });
    connect(ui->noiseCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_settings->setNoiseLevel(ui->noiseCombo->currentData().toInt());
        saveSettings();
        onOutputSpecChanged();  // the denoise level changes the output, too
        onProcessingChanged();
    });
    connect(ui->qualitySpin, &QSpinBox::valueChanged, this, &MainWindow::onOutputSpecChanged);

    retranslate();
    restoreFolders();
    updateQualityControls();
    updateButtons();

    // Window size: the .ui size is only the preferred size; it is fitted to
    // the screen (or restored from the last session) so the action row stays visible.
    ui->mainSplitter->restoreState(m_settings->splitterState());
    arx::placeWindow(this, m_settings->windowGeometry());
}

MainWindow::~MainWindow()
{
    m_scanPool.clear();
    m_scanPool.waitForDone();
    m_thumbPool.clear();
    m_thumbPool.waitForDone();
    if (m_thread.isRunning()) {
        m_worker->requestCancel();
        m_thread.quit();
        m_thread.wait();
    }
}

// ------------------------------------------------------------- settings

void MainWindow::restoreFolders()
{
    // Each folder falls back on its own; a stale value is not overwritten
    // until the user confirms a new folder.
    m_sourceDir = arx::resolveFolder(m_settings->sourceDir());

    bool outFellBack = false;
    const QString storedOut = m_settings->outputDir();
    const QString out = arx::resolveFolder(storedOut, &outFellBack);
    ui->outputDirEdit->setText(QDir::toNativeSeparators(out));
    if (outFellBack && !storedOut.isEmpty()) {
        m_status->setText(tr("The previous output folder is not available (%1). Using %2 instead.")
                                     .arg(QDir::toNativeSeparators(storedOut),
                                          QDir::toNativeSeparators(out)));
    }
}

void MainWindow::saveSettings()
{
    if (!m_settings->sync())
        m_status->setText(tr("Settings could not be saved. They will apply until the program is closed."));
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_settings->tileSize(), m_settings->computeDevice(),
                       LanguageManager::instance().current(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    applySettings(dlg.tileSize(), int(dlg.computeDevice()), dlg.languageChanged(), int(dlg.language()));
}

void MainWindow::openAbout()
{
    AboutDialog dlg(this);
    dlg.setDeviceName(m_device);
    dlg.exec();
}

void MainWindow::resetProgress()
{
    // QProgressBar needs max > 0 to show an empty (not busy) bar; the text is fixed.
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    setBatchProgressText(QStringLiteral("0 / 0"));
}

void MainWindow::setBatchProgressText(const QString &text)
{
    // The bar keeps the text as its format (accessibility, tests); the label shows it.
    ui->progressBar->setFormat(text);
    m_batchProgressLabel->setText(text);
}

void MainWindow::setItemProgressText(const QString &text)
{
    ui->itemProgressBar->setFormat(text);
    m_itemProgressLabel->setText(text);
}

void MainWindow::setItemProgressVisible(bool visible)
{
    ui->itemProgressBar->setVisible(visible);
    m_itemProgressLabel->setVisible(visible);
}

int MainWindow::thumbnailSide() const
{
    // About two and a half text lines: large enough to recognise an image.
    return std::max(32, int(std::lround(fontMetrics().height() * 2.3)));
}

void MainWindow::requestThumbnail(const QString &path)
{
    const int side = thumbnailSide();
    const qreal dpr = devicePixelRatioF();
    const QColor frame = theme::color("border");
    m_thumbPool.start([this, path, side, dpr, frame] {
        // Header first, then decode at (about) the thumbnail size.
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // The scaled size applies before the orientation is applied (Qt 6.12), so it is
        // computed from the stored size; the square box keeps the result within px either way.
        const QSize stored = reader.size();
        const int px = int(std::ceil(side * dpr));
        if (stored.isValid())
            reader.setScaledSize(stored.scaled(px, px, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        QImage tile(px, px, QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::transparent);
        if (!img.isNull()) {
            QPainter p(&tile);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const QImage fitted = img.size() == QSize(px, px) || (img.width() <= px && img.height() <= px)
                                      ? img
                                      : img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QRectF r((px - fitted.width()) / 2.0, (px - fitted.height()) / 2.0, fitted.width(),
                           fitted.height());
            QPainterPath clip;
            const qreal radius = 6 * dpr;
            clip.addRoundedRect(r, radius, radius);
            p.setClipPath(clip);
            p.drawImage(r, fitted);
            p.setClipping(false);
            p.setPen(QPen(frame, dpr));
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        }
        tile.setDevicePixelRatio(dpr);
        QMetaObject::invokeMethod(this, [this, path, tile] {
            // The item may have been removed while the thumbnail was decoded.
            for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = ui->fileList->topLevelItem(i);
                if (item->data(ColFile, kRolePath).toString() == path) {
                    item->setIcon(ColFile, QIcon(QPixmap::fromImage(tile)));
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::applySettings(int tile, int device, bool languageChanged, int language)
{
    m_settings->setTileSize(tile);
    m_settings->setComputeDevice(arx::ComputeDevice(device));
    if (languageChanged) {
        // Only an explicit choice is stored as a manual override.
        const auto lang = arx::Language(language);
        m_settings->setManualLanguage(lang);
        LanguageManager::instance().apply(lang);
    }
    saveSettings();
}

QString MainWindow::processingProblem() const
{
    arx::InferenceOptions o;
    o.scale = ui->scaleCombo->currentData().toInt();
    o.noise = ui->noiseCombo->currentData().toInt();
    return arx::ProcessJob::checkOptions(o);
}

void MainWindow::onProcessingChanged()
{
    const QString problem = processingProblem();
    if (!problem.isEmpty())
        m_status->setText(problem);
    updateButtons();
}

// ------------------------------------------------------------------ controls

void MainWindow::retranslate()
{
    m_fileMenu->setTitle(tr("&File"));
    m_addImagesAction->setText(tr("Add &Images..."));
    // Same text and access key (O) as the button: Alt+F stays with the File menu.
    m_addFolderAction->setText(tr("Add F&older..."));
    m_startAction->setText(tr("&Start Processing"));
    m_exitAction->setText(tr("E&xit"));
    m_toolsMenu->setTitle(tr("&Tools"));
    m_settingsAction->setText(tr("&Settings..."));
    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("&About AnimeRestoreX"));
    m_emptyHint->setText(tr("No images yet") + QLatin1Char('\n') + tr("Drag images or folders here"));
    updateEmptyHintWidth();
    populateCombos();
    updateQualityControls();
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i)
        refreshItemTexts(ui->fileList->topLevelItem(i));
    updateDetailColumnWidth();  // also sizes the Status column for the new texts
    updateButtons();
}

void MainWindow::updateEmptyHintWidth()
{
    // A small window shrinks the preview first; the list keeps room for
    // names and for the unwrapped empty-list hint.
    const int ch = fontMetrics().averageCharWidth();
    const int hint = m_emptyHint->fontMetrics()
                         .boundingRect(QRect(), Qt::AlignCenter, m_emptyHint->text())
                         .width();
    // Frame plus the style sheet's list padding (4 px each side).
    const int frame = 2 * ui->fileList->frameWidth() + 2 * 4;
    ui->fileList->setMinimumWidth(std::max(ch * 24, hint + 2 * ch + frame));
}

int MainWindow::statusColumnWidth() const
{
    // Widest of the column title and every state text (Failed has an icon),
    // plus padding so "Status" and "Details" do not touch.
    const QFontMetrics itemFm = ui->fileList->fontMetrics();
    const QFontMetrics headFm = ui->fileList->header()->fontMetrics();
    const int icon = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, ui->fileList);
    int w = headFm.horizontalAdvance(ui->fileList->headerItem()->text(ColStatus));
    for (auto s : {ItemState::Pending, ItemState::Processing, ItemState::Done, ItemState::Failed,
                   ItemState::NotProcessed}) {
        const int extra = s == ItemState::Failed ? icon + itemFm.averageCharWidth() : 0;
        w = std::max(w, itemFm.horizontalAdvance(stateText(s)) + extra);
    }
    // Room for the style sheet's item padding (4 px each side) as well.
    return w + itemFm.averageCharWidth() * 2 + 8;
}

void MainWindow::updateNoiseChoices()
{
    // 1x is denoise only, so "None" is disabled there and comes back with
    // 2x / 4x. If "None" was selected, a denoise level is picked for this
    // session; the saved choice is left alone.
    const bool oneX = ui->scaleCombo->currentData().toInt() == 1;
    const int noneIndex = ui->noiseCombo->findData(-1);
    if (auto *model = qobject_cast<QStandardItemModel *>(ui->noiseCombo->model());
        model && noneIndex >= 0)
        model->item(noneIndex)->setEnabled(!oneX);
    if (oneX && ui->noiseCombo->currentData().toInt() < 0) {
        const int saved = m_settings->noiseLevel();
        const int level = saved >= 0 ? saved : arx::AppSettings::kDefaultNoise;
        {
            const QSignalBlocker block(ui->noiseCombo);
            ui->noiseCombo->setCurrentIndex(ui->noiseCombo->findData(level));
        }
        m_status->setText(tr("\"No upscaling (1×)\" always denoises: denoise level set to %1.")
                              .arg(ui->noiseCombo->currentText()));
    }
}

void MainWindow::populateCombos()
{
    // Selections are kept by value, so the item order may change between versions.
    const QVariant fmt = ui->formatCombo->currentData();
    const QVariant mode = ui->webpModeCombo->currentData();
    const QVariant scale = ui->scaleCombo->currentData();
    const QVariant noise = ui->noiseCombo->currentData();

    const QSignalBlocker b1(ui->formatCombo), b2(ui->webpModeCombo), b3(ui->scaleCombo),
        b4(ui->noiseCombo);
    ui->formatCombo->clear();
    ui->formatCombo->addItem(QStringLiteral("PNG"), int(arx::OutputFormat::Png));
    ui->formatCombo->addItem(QStringLiteral("JPEG"), int(arx::OutputFormat::Jpeg));
    ui->formatCombo->addItem(QStringLiteral("WebP"), int(arx::OutputFormat::WebP));
    ui->webpModeCombo->clear();
    ui->webpModeCombo->addItem(tr("Lossy"), false);
    ui->webpModeCombo->addItem(tr("Lossless"), true);
    ui->scaleCombo->clear();
    ui->scaleCombo->addItem(tr("No upscaling (1×)"), 1);
    ui->scaleCombo->addItem(QStringLiteral("2×"), 2);
    ui->scaleCombo->addItem(QStringLiteral("4×"), 4);
    ui->noiseCombo->clear();
    ui->noiseCombo->addItem(tr("None"), -1);
    ui->noiseCombo->addItem(tr("0 (weakest)"), 0);
    ui->noiseCombo->addItem(QStringLiteral("1"), 1);
    ui->noiseCombo->addItem(QStringLiteral("2"), 2);
    ui->noiseCombo->addItem(tr("3 (strongest)"), 3);

    auto select = [](QComboBox *c, const QVariant &value, const QVariant &fallback) {
        c->setCurrentIndex(std::max(0, c->findData(value.isValid() ? value : fallback)));
    };
    select(ui->formatCombo, fmt, int(arx::OutputFormat::Png));
    select(ui->webpModeCombo, mode, false);
    select(ui->scaleCombo, scale, 2);
    select(ui->noiseCombo, noise, m_settings->noiseLevel());
    updateNoiseChoices();  // the items were rebuilt
}

void MainWindow::updateQualityControls()
{
    const auto fmt = arx::OutputFormat(ui->formatCombo->currentData().toInt());
    const bool webp = fmt == arx::OutputFormat::WebP;
    const bool lossless = webp && ui->webpModeCombo->currentData().toBool();

    // Everything after the format combo depends on the format and sits at the
    // end of the row, so showing / hiding it never moves the other controls.
    ui->webpModeCombo->setVisible(webp);
    QString qualityHint;
    QString formatHint;
    switch (fmt) {
    case arx::OutputFormat::Png:
        formatHint = tr("PNG is always lossless.");
        break;
    case arx::OutputFormat::Jpeg:
        ui->qualitySpin->setMaximum(100);
        qualityHint = tr("0-100, lossy");
        break;
    case arx::OutputFormat::WebP:
        ui->qualitySpin->setMaximum(99);
        if (lossless)
            formatHint = tr("Lossless: quality setting not used.");
        else
            qualityHint = tr("0-99, lossy");
        break;
    }
    const bool hasQuality = !qualityHint.isEmpty();
    ui->qualityLabel->setVisible(hasQuality);
    ui->qualitySpin->setVisible(hasQuality);
    ui->qualitySpin->setEnabled(hasQuality && !m_running);
    ui->qualitySpin->setToolTip(qualityHint);
    ui->qualityLabel->setToolTip(qualityHint);
    ui->formatCombo->setToolTip(formatHint);
    ui->webpModeCombo->setToolTip(formatHint);
}

int MainWindow::pendingCount() const
{
    int n = 0;
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i)
        if (ItemState(ui->fileList->topLevelItem(i)->data(ColFile, kRoleState).toInt()) != ItemState::Done)
            ++n;
    return n;
}

void MainWindow::updateButtons()
{
    const bool idle = !m_running && !m_scanning;
    const bool hasItems = ui->fileList->topLevelItemCount() > 0;
    const QString problem = processingProblem();
    ui->addImagesButton->setEnabled(idle);
    ui->addFolderButton->setEnabled(idle);
    m_addImagesAction->setEnabled(idle);
    m_addFolderAction->setEnabled(idle);
    const bool canRemove = idle && !ui->fileList->selectedItems().isEmpty();
    ui->removeButton->setEnabled(canRemove);
    m_removeAction->setEnabled(canRemove);
    ui->clearButton->setEnabled(idle && hasItems);
    const bool canStart = idle && hasItems && problem.isEmpty();
    ui->startButton->setEnabled(canStart);
    m_startAction->setEnabled(canStart);
    ui->startButton->setToolTip(problem.isEmpty()
                                    ? m_startAction->shortcut().toString(QKeySequence::NativeText)
                                    : problem);
    const int pending = pendingCount();
    ui->startButton->setText(pending > 0 && !m_running ? tr("Start (%n)", nullptr, pending) : tr("Start"));
    ui->cancelButton->setEnabled(m_running);
    ui->browseOutputButton->setEnabled(!m_running);
    ui->formatCombo->setEnabled(!m_running);
    ui->webpModeCombo->setEnabled(!m_running);
    ui->scaleCombo->setEnabled(!m_running);
    ui->noiseCombo->setEnabled(!m_running);
    m_settingsAction->setEnabled(!m_running);
    m_emptyHint->setVisible(!hasItems);
    ui->previewPane->setHasItems(hasItems);
}

void MainWindow::setRunning(bool running)
{
    m_running = running;
    updateButtons();
    updateQualityControls();
}

arx::JobSpec MainWindow::currentSpec() const
{
    arx::JobSpec spec;
    spec.outputDir = QDir::fromNativeSeparators(ui->outputDirEdit->text());
    spec.inference.noise = ui->noiseCombo->currentData().toInt();
    spec.inference.tile = m_settings->tileSize();
    spec.inference.device = m_settings->computeDevice();
    spec.inference.scale = ui->scaleCombo->currentData().toInt();
    spec.encode.format = arx::OutputFormat(ui->formatCombo->currentData().toInt());
    spec.encode.webpLossless = ui->webpModeCombo->currentData().toBool();
    spec.encode.quality = ui->qualitySpin->value();
    return spec;
}

void MainWindow::onOutputSpecChanged()
{
    if (m_running)
        return;
    // Results made with other settings no longer match: process them again.
    int reset = 0;
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = ui->fileList->topLevelItem(i);
        if (ItemState(item->data(ColFile, kRoleState).toInt()) == ItemState::Done) {
            item->setData(ColFile, kRoleOutput, QString());
            setItemState(item, ItemState::Pending);
            ++reset;
        }
    }
    if (reset > 0) {
        m_status->setText(tr("Output settings changed: %1 finished item(s) will be processed again.")
                                     .arg(reset));
        updatePreview();
        updateButtons();  // pending count on the Start button
    }
}

// --------------------------------------------------------------------- list

QString MainWindow::pathKey(const QString &path)
{
    // Windows paths are case-insensitive.
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

int MainWindow::addFiles(const QStringList &files)
{
    int added = 0;
    int duplicates = 0;
    int unsupported = 0;
    QPixmap blank(ui->fileList->iconSize() * devicePixelRatioF());
    blank.setDevicePixelRatio(devicePixelRatioF());
    blank.fill(Qt::transparent);
    const QIcon blankIcon(blank);
    for (const QString &f : files) {
        if (!arx::isSupportedImageFile(f)) {
            ++unsupported;
            continue;
        }
        const QString key = pathKey(f);
        if (m_keys.contains(key)) {
            ++duplicates;
            continue;
        }
        m_keys.insert(key);
        const QString abs = QFileInfo(f).absoluteFilePath();
        auto *item = new QTreeWidgetItem(ui->fileList);
        item->setText(ColFile, QFileInfo(abs).fileName());
        item->setData(ColFile, kRolePath, abs);
        item->setIcon(ColFile, blankIcon);  // keeps names aligned until the thumbnail is ready
        setItemState(item, ItemState::Pending);
        requestThumbnail(abs);
        ++added;
    }

    QStringList msg;
    msg << tr("Images added: %1.").arg(added);
    if (duplicates > 0)
        msg << tr("Duplicates skipped: %1.").arg(duplicates);
    if (unsupported > 0)
        msg << tr("Unsupported files skipped: %1.").arg(unsupported);
    m_status->setText(msg.join(QLatin1Char(' ')));

    if (!ui->fileList->currentItem() && ui->fileList->topLevelItemCount() > 0)
        ui->fileList->setCurrentItem(ui->fileList->topLevelItem(0));
    updateButtons();
    return added;
}

void MainWindow::browseImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Images"), m_sourceDir, tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (files.isEmpty())
        return;  // canceled: keep the previous source folder
    m_sourceDir = QFileInfo(files.first()).absolutePath();
    m_settings->setSourceDir(m_sourceDir);
    saveSettings();
    addFiles(files);
}

void MainWindow::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"), m_sourceDir);
    if (dir.isEmpty())
        return;
    m_sourceDir = dir;
    m_settings->setSourceDir(dir);
    saveSettings();
    scanAndAdd({dir}, {});
}

void MainWindow::scanAndAdd(const QStringList &dirs, const QStringList &files)
{
    // Scan off the GUI thread (folders may be on slow drives). Folders are
    // scanned one after another; everything is added in one go at the end.
    m_scanning = true;
    updateButtons();
    m_status->setText(tr("Scanning folder..."));
    m_scanPool.start([this, dirs, files] {
        QStringList all = files;
        for (const QString &dir : dirs)
            all += arx::scanFolder(dir);
        QMetaObject::invokeMethod(this, [this, dirs, files, all] {
            m_scanning = false;
            if (all.isEmpty()) {
                QStringList shown;
                for (const QString &d : dirs)
                    shown << QDir::toNativeSeparators(d);
                m_status->setText(tr("No supported images (PNG, JPEG, WebP) in %1.")
                                      .arg(shown.join(QStringLiteral(", "))));
                updateButtons();
                return;
            }
            addFiles(all);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- drag & drop

namespace {

// Local files / folders in a drag, or nothing if it carries none.
QStringList localPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl &url : mime->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    return paths;
}

} // namespace

void MainWindow::setDropHighlight(bool on)
{
    m_dropOverlay->setGeometry(ui->fileList->viewport()->rect());
    m_dropOverlay->setVisible(on);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Not while processing or scanning: the list must not change then.
    if (m_running || m_scanning || localPaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setDropHighlight(true);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropHighlight(false);
    QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    setDropHighlight(false);
    if (m_running || m_scanning) {
        event->ignore();
        return;
    }
    QStringList files, dirs;
    for (const QString &path : localPaths(event->mimeData())) {
        if (QFileInfo(path).isDir())
            dirs << path;
        else
            files << path;
    }
    if (files.isEmpty() && dirs.isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    // A drop does not change the remembered "Add" dialog folder.
    if (dirs.isEmpty())
        addFiles(files);
    else
        scanAndAdd(dirs, files);
}

void MainWindow::browseOutput()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Folder"), QDir::fromNativeSeparators(ui->outputDirEdit->text()));
    if (dir.isEmpty())
        return;  // canceled: keep the previous output folder
    ui->outputDirEdit->setText(QDir::toNativeSeparators(dir));
    m_settings->setOutputDir(dir);
    saveSettings();
}

void MainWindow::removeSelected()
{
    const QList<QTreeWidgetItem *> items = ui->fileList->selectedItems();
    for (QTreeWidgetItem *item : items) {
        m_keys.remove(pathKey(item->data(ColFile, kRolePath).toString()));
        delete item;
    }
    if (ui->fileList->topLevelItemCount() == 0) {
        ui->previewPane->clear();
        resetProgress();
    }
    updateButtons();
}

void MainWindow::clearList()
{
    ui->fileList->clear();
    m_keys.clear();
    ui->previewPane->clear();
    resetProgress();
    m_status->clear();
    updateButtons();
}

static QString stateText(MainWindow::ItemState s)
{
    switch (s) {
    case MainWindow::ItemState::Pending:      return MainWindow::tr("Pending");
    case MainWindow::ItemState::Processing:   return MainWindow::tr("Processing...");
    case MainWindow::ItemState::Done:         return MainWindow::tr("Done");
    case MainWindow::ItemState::Failed:       return MainWindow::tr("Failed");
    case MainWindow::ItemState::NotProcessed: return MainWindow::tr("Not processed");
    }
    return {};
}

void MainWindow::setItemState(QTreeWidgetItem *item, ItemState state, const QString &detail)
{
    item->setData(ColFile, kRoleState, int(state));
    item->setData(ColFile, kRoleDetail, detail);
    refreshItemTexts(item);
}

void MainWindow::refreshItemTexts(QTreeWidgetItem *item)
{
    const auto state = ItemState(item->data(ColFile, kRoleState).toInt());
    const QString detail = item->data(ColFile, kRoleDetail).toString();
    const QString path = QDir::toNativeSeparators(item->data(ColFile, kRolePath).toString());
    item->setText(ColStatus, stateText(state));
    item->setText(ColDetail, detail);
    // Full path and full reason are always available as tooltips.
    const QString full = detail.isEmpty() ? path : path + QLatin1Char('\n') + detail;
    item->setToolTip(ColFile, wrapTooltip(full));
    item->setToolTip(ColStatus, wrapTooltip(detail.isEmpty() ? stateText(state) : detail));
    item->setToolTip(ColDetail, wrapTooltip(detail));
    // Failure is shown by an icon as well, not by the red color alone.
    const bool failed = state == ItemState::Failed;
    item->setForeground(ColStatus, failed ? QBrush(theme::danger()) : QBrush());
    item->setIcon(ColStatus, failed ? style()->standardIcon(QStyle::SP_MessageBoxCritical) : QIcon());
}

void MainWindow::updatePreview()
{
    QTreeWidgetItem *item = ui->fileList->currentItem();
    if (!item) {
        ui->previewPane->clear();
        return;
    }
    const auto state = ItemState(item->data(ColFile, kRoleState).toInt());
    using Hint = PreviewPane::ResultHint;
    QString output;
    Hint hint = Hint::NotYet;
    switch (state) {
    case ItemState::Done:         output = item->data(ColFile, kRoleOutput).toString(); break;
    case ItemState::Failed:       hint = Hint::Failed; break;
    case ItemState::Processing:   hint = Hint::Processing; break;
    case ItemState::NotProcessed: hint = Hint::NotProcessed; break;
    case ItemState::Pending:      break;
    }
    ui->previewPane->showItem(item->data(ColFile, kRolePath).toString(), output, hint);
}

// -------------------------------------------------------------------- batch

void MainWindow::start()
{
    if (m_running || m_scanning || ui->fileList->topLevelItemCount() == 0)
        return;
    if (const QString problem = processingProblem(); !problem.isEmpty()) {
        m_status->setText(problem);
        return;
    }

    // Only items that are not "Done" are processed; finished items stay as
    // they are until the output settings change.
    QStringList files;
    m_jobItems.clear();
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = ui->fileList->topLevelItem(i);
        if (ItemState(item->data(ColFile, kRoleState).toInt()) == ItemState::Done)
            continue;
        files << item->data(ColFile, kRolePath).toString();
        m_jobItems << item;
        setItemState(item, ItemState::Pending);
    }
    if (files.isEmpty()) {
        m_status->setText(tr("All items are already done. Change the output settings or add images to process again."));
        return;
    }
    updatePreview();

    const arx::JobSpec spec = currentSpec();
    m_jobOutputDir = spec.outputDir;
    m_finishedCount = 0;
    m_vramTile = 0;
    m_retryTile = 0;
    // While running, both bars show the estimate in 0.1% steps; the batch
    // bar is weighted by output pixels (refreshProgress).
    // Measured speeds belong to one GPU. Before the engine has named the GPU
    // in this session, the last one is assumed but not trusted for the
    // remaining time (onDeviceDetected confirms or replaces it).
    m_ratesDevice = m_device.isEmpty() ? m_settings->progressDevice() : m_device;
    m_estimator.setLearnedRates(m_settings->progressRates(m_ratesDevice));
    m_estimator.setDeviceConfirmed(!m_device.isEmpty());
    m_estimator.startBatch({}, m_clock.elapsed());  // the plans follow from the worker
    ui->progressBar->setRange(0, 1000);
    ui->progressBar->setValue(0);
    ui->itemProgressBar->setValue(0);
    setItemProgressText(QString());
    setItemProgressVisible(true);
    m_status->setText(tr("Starting..."));
    setRunning(true);
    refreshProgress();
    m_progressTimer->start();

    JobWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, files, spec] { worker->process(files, spec); },
                              Qt::QueuedConnection);
}

void MainWindow::onItemStarted(int index)
{
    QTreeWidgetItem *item = m_jobItems.value(index);
    if (!item)
        return;
    m_estimator.itemStarted(index, m_clock.elapsed());
    m_retryTile = 0;
    setItemState(item, ItemState::Processing);
    ui->fileList->scrollToItem(item);
    m_status->setText(tr("Processing %1 / %2: %3")
                                 .arg(index + 1)
                                 .arg(m_jobItems.size())
                                 .arg(item->text(ColFile)));
    refreshProgress();
    if (item == ui->fileList->currentItem())
        updatePreview();
}

void MainWindow::onItemFinished(int index, const arx::ItemResult &r)
{
    QTreeWidgetItem *item = m_jobItems.value(index);
    if (!item)
        return;
    const bool smaller = r.ok && (r.tileChoice == arx::TileChoice::SmallerForVram ||
                                  r.tileChoice == arx::TileChoice::OomRetry ||
                                  r.tileChoice == arx::TileChoice::AfterGpuError);
    item->setData(ColFile, kRoleSmallerTile, smaller ? r.tileUsed : 0);
    if (r.ok) {
        item->setData(ColFile, kRoleOutput, r.outputPath);
        QString detail = QFileInfo(r.outputPath).fileName();
        // The tile size changes the result slightly: say when Auto had to use a smaller one.
        if (r.tileChoice == arx::TileChoice::SmallerForVram)
            detail += tr(" (tile %1: not enough GPU memory for 256)").arg(r.tileUsed);
        else if (r.tileChoice == arx::TileChoice::OomRetry)
            detail += tr(" (tile %1: redone after a GPU error)").arg(r.tileUsed);
        else if (r.tileChoice == arx::TileChoice::AfterGpuError)
            detail += tr(" (tile %1: switched after a GPU error)").arg(r.tileUsed);
        setItemState(item, ItemState::Done, detail);
    } else if (r.canceled) {
        setItemState(item, ItemState::NotProcessed, tr("Canceled"));
    } else {
        setItemState(item, ItemState::Failed, r.error);
    }
    m_estimator.itemFinished(index, r.ok, m_clock.elapsed());
    if (!r.canceled)
        ++m_finishedCount;
    refreshProgress();
    if (item == ui->fileList->currentItem())
        updatePreview();
}

void MainWindow::onDeviceDetected(const QString &device)
{
    m_device = device;
    if (device != m_ratesDevice) {
        // Another GPU than last time: its own measurements, or none yet
        // ("estimating time left" until this GPU has been measured).
        m_ratesDevice = device;
        m_estimator.setLearnedRates(m_settings->progressRates(device));
    }
    m_estimator.setDeviceConfirmed(true);
}

QString MainWindow::stageText(const arx::ProgressSnapshot &snap) const
{
    switch (snap.stage) {
    case arx::Stage::Load:
        return tr("Reading");
    case arx::Stage::Inference:
        if (m_retryTile)
            return tr("AI processing again with tile size %1 (after a GPU error)").arg(m_retryTile);
        if (m_vramTile)
            return tr("AI processing (tile size %1: GPU memory is short)").arg(m_vramTile);
        if (snap.passCount > 1)
            return tr("AI processing %1/%2").arg(snap.pass + 1).arg(snap.passCount);
        return tr("AI processing");
    case arx::Stage::Encode:
        return tr("Encoding and saving");
    }
    return {};
}

QString MainWindow::remainingText(double seconds) const
{
    if (seconds < 0)
        return tr("estimating time left");
    if (seconds < 3)
        return tr("almost done");
    if (seconds < 60)
        return tr("about %1 s left").arg(std::max(5, int(std::lround(seconds / 5.0)) * 5));
    if (seconds < 3600) {
        const int s = int(std::lround(seconds / 10.0)) * 10;
        return tr("about %1 min %2 s left").arg(s / 60).arg(s % 60);
    }
    const int m = int(std::lround(seconds / 60.0));
    return tr("about %1 h %2 min left").arg(m / 60).arg(m % 60);
}

void MainWindow::refreshProgress()
{
    if (!m_running)
        return;
    const arx::ProgressSnapshot snap = m_estimator.snapshot(m_clock.elapsed());
    const int total = int(m_jobItems.size());
    const auto permille = [](double f) { return int(std::lround(std::clamp(f, 0.0, 1.0) * 1000)); };
    const auto percent = [](double f) { return int(std::floor(std::clamp(f, 0.0, 1.0) * 100)); };

    // QProgressBar only replaces %v / %m / %p, so the literal "%" is safe here.
    // The tooltip repeats the text, which a narrow window may cut off.
    const QString hint = tr("Estimated from the image size and the measured speed of earlier images.");
    const QString batchText = tr("%1 / %2 · est. %3% · %4")
                                  .arg(m_finishedCount)
                                  .arg(total)
                                  .arg(percent(snap.batchFraction))
                                  .arg(remainingText(snap.remainingSec));
    ui->progressBar->setValue(permille(snap.batchFraction));
    setBatchProgressText(batchText);
    ui->progressBar->setToolTip(batchText + QLatin1Char('\n') + hint);
    if (snap.index >= 0) {
        const QString itemText = tr("%1 · est. %2%").arg(stageText(snap)).arg(percent(snap.itemFraction));
        ui->itemProgressBar->setValue(permille(snap.itemFraction));
        setItemProgressText(itemText);
        ui->itemProgressBar->setToolTip(itemText + QLatin1Char('\n') + hint);
    }
}

void MainWindow::onJobFinished(const arx::BatchSummary &summary)
{
    m_progressTimer->stop();
    setRunning(false);
    // Back to the plain count of this job; the estimate is no longer shown.
    setItemProgressVisible(false);
    ui->progressBar->setRange(0, std::max(1, int(m_jobItems.size())));
    ui->progressBar->setValue(m_finishedCount);
    setBatchProgressText(QStringLiteral("%1 / %2").arg(m_finishedCount).arg(m_jobItems.size()));
    ui->progressBar->setToolTip(QString());
    // Measured speeds make the next estimate (also in later sessions) better;
    // they are stored only under a GPU the engine actually reported.
    if (m_estimator.deviceConfirmed() && !m_ratesDevice.isEmpty()) {
        m_settings->setProgressRates(m_ratesDevice, m_estimator.learnedRates());
        m_settings->setProgressDevice(m_ratesDevice);
        saveSettings();
    }

    // Counts come from this job's items in the list, so the dialog matches it.
    arx::BatchSummary shown = summary;
    shown.succeeded = shown.failed = shown.notProcessed = 0;
    QList<CompletionDialog::Item> items;
    shown.smallerTile = 0;
    for (QTreeWidgetItem *item : std::as_const(m_jobItems)) {
        CompletionDialog::Item entry;
        entry.file = item->text(ColFile);
        switch (ItemState(item->data(ColFile, kRoleState).toInt())) {
        case ItemState::Done:
            ++shown.succeeded;
            entry.kind = CompletionDialog::Item::Succeeded;
            entry.detail = QFileInfo(item->data(ColFile, kRoleOutput).toString()).fileName();
            entry.smallerTile = item->data(ColFile, kRoleSmallerTile).toInt();
            shown.smallerTile += entry.smallerTile > 0 ? 1 : 0;
            break;
        case ItemState::Failed:
            ++shown.failed;
            entry.kind = CompletionDialog::Item::Failed;
            entry.detail = item->data(ColFile, kRoleDetail).toString();
            break;
        case ItemState::Pending:
        case ItemState::Processing:
            setItemState(item, ItemState::NotProcessed);
            ++shown.notProcessed;
            entry.kind = CompletionDialog::Item::NotProcessed;
            break;
        case ItemState::NotProcessed:
            ++shown.notProcessed;
            entry.kind = CompletionDialog::Item::NotProcessed;
            break;
        }
        items << entry;
    }
    m_jobItems.clear();

    if (!summary.fatalError.isEmpty())
        m_status->setText(tr("Stopped: %1").arg(summary.fatalError));
    else if (summary.canceled)
        m_status->setText(tr("Canceled."));
    else
        m_status->setText(tr("Finished: %1 succeeded, %2 failed.")
                                     .arg(shown.succeeded).arg(shown.failed));

    if (m_closing)
        return;
    CompletionDialog dlg(shown, m_jobOutputDir, items, this);
    dlg.exec();
}

// ------------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_running) {
        const auto answer = QMessageBox::question(
            this, tr("Processing in Progress"),
            tr("Images are still being processed. Cancel processing and quit?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        // Ask the worker to stop (before the next tile, or at once on the CPU) and wait
        // until it has removed its temporary file; the thread is never terminated.
        m_closing = true;
        m_worker->requestCancel();
        m_thread.quit();
        m_thread.wait();
    }
    m_settings->setWindowGeometry(saveGeometry());
    m_settings->setSplitterState(ui->mainSplitter->saveState());
    saveSettings();
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->fileList->viewport() && event->type() == QEvent::Resize) {
        updateDetailColumnWidth();
        const int margin = fontMetrics().averageCharWidth();
        m_emptyHint->setGeometry(ui->fileList->viewport()->rect().adjusted(margin, 0, -margin, 0));
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateDetailColumnWidth()
{
    // In a narrow list the Details column is hidden; the reason stays
    // available in the File / Status tooltips.
    const int ch = ui->fileList->fontMetrics().averageCharWidth();
    const int view = ui->fileList->viewport()->width();
    // The name column always keeps at least 40% of the list for the text,
    // plus the thumbnail; in a very narrow list the Status text is elided
    // (full text in the tooltip).
    const int nameMin = std::min(view, view * 2 / 5 + ui->fileList->iconSize().width() + 8);
    const int status = std::min(statusColumnWidth(), view - nameMin);
    ui->fileList->header()->resizeSection(ColStatus, status);
    const int room = view - nameMin - status;
    const bool show = room >= ch * 10;
    if (ui->fileList->isColumnHidden(ColDetail) == show)
        ui->fileList->setColumnHidden(ColDetail, !show);
    if (show)
        ui->fileList->header()->resizeSection(ColDetail, std::min(room, std::clamp(view * 35 / 100, ch * 10, ch * 30)));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    } else if (event->type() == QEvent::PaletteChange) {
        // Light / dark switch: the Failed color comes from the theme.
        for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i)
            refreshItemTexts(ui->fileList->topLevelItem(i));
    }
    QMainWindow::changeEvent(event);
}
