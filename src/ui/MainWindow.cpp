#include "ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "core/AppSettings.h"
#include "core/FolderScanner.h"
#include "ui/AboutDialog.h"
#include "ui/AppIcon.h"
#include "ui/CompletionDialog.h"
#include "ui/ElidedLabel.h"
#include "ui/WindowPlacement.h"
#include "ui/JobWorker.h"
#include "ui/LanguageManager.h"
#include "ui/SettingsDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>

namespace {

constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleState = Qt::UserRole + 1;
constexpr int kRoleDetail = Qt::UserRole + 2;
constexpr int kRoleOutput = Qt::UserRole + 3;

enum Column { ColFile = 0, ColStatus = 1, ColDetail = 2 };

QString waifu2xDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("waifu2x"));
}

// Rich-text tooltips wrap long lines instead of being cut off.
QString wrapTooltip(const QString &text)
{
    if (text.isEmpty())
        return {};
    return QStringLiteral("<p style='white-space:pre-wrap'>%1</p>").arg(text.toHtmlEscaped());
}

} // namespace

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
    m_settingsAction = menuBar()->addAction(QString());
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(QString());
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::openAbout);
    populateCombos();

    ui->mainSplitter->setStretchFactor(0, 2);
    ui->mainSplitter->setStretchFactor(1, 3);
    // File names first: the name column takes the free space and is elided in the middle.
    ui->fileList->setTextElideMode(Qt::ElideMiddle);
    QHeaderView *header = ui->fileList->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    header->setSectionResizeMode(ColStatus, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColDetail, QHeaderView::Interactive);
    // The Details column follows the list width so the name column keeps most of it.
    ui->fileList->viewport()->installEventFilter(this);
    updateDetailColumnWidth();
    resetProgress();

    m_worker = new JobWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &JobWorker::itemStarted, this, &MainWindow::onItemStarted);
    connect(m_worker, &JobWorker::itemFinished, this, &MainWindow::onItemFinished);
    connect(m_worker, &JobWorker::jobFinished, this, &MainWindow::onJobFinished);
    m_thread.start();

    connect(ui->addImagesButton, &QPushButton::clicked, this, &MainWindow::browseImages);
    connect(ui->addFolderButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(ui->clearButton, &QPushButton::clicked, this, &MainWindow::clearList);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(ui->cancelButton, &QPushButton::clicked, this, [this] {
        m_worker->requestCancel();
        ui->cancelButton->setEnabled(false);
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
    connect(ui->scaleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onOutputSpecChanged);
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
    SettingsDialog dlg(m_settings->noiseLevel(), m_settings->tileSize(),
                       LanguageManager::instance().current(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    applySettings(dlg.noiseLevel(), dlg.tileSize(), dlg.languageChanged(), int(dlg.language()));
}

void MainWindow::openAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::resetProgress()
{
    // QProgressBar needs max > 0 to show an empty (not busy) bar; the text is fixed.
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->progressBar->setFormat(QStringLiteral("0 / 0"));
}

void MainWindow::applySettings(int noise, int tile, bool languageChanged, int language)
{
    const bool noiseChanged = noise != m_settings->noiseLevel();
    m_settings->setNoiseLevel(noise);
    m_settings->setTileSize(tile);
    if (languageChanged) {
        // Only an explicit choice is stored as a manual override.
        const auto lang = arx::Language(language);
        m_settings->setManualLanguage(lang);
        LanguageManager::instance().apply(lang);
    }
    saveSettings();
    if (noiseChanged)
        onOutputSpecChanged();  // denoise level changes the output, too
}

// ------------------------------------------------------------------ controls

void MainWindow::retranslate()
{
    m_settingsAction->setText(tr("&Settings"));
    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("&About AnimeRestoreX"));
    populateCombos();
    updateQualityControls();
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i)
        refreshItemTexts(ui->fileList->topLevelItem(i));
}

void MainWindow::populateCombos()
{
    const int fmt = ui->formatCombo->currentIndex();
    const int mode = ui->webpModeCombo->currentIndex();
    const int scale = ui->scaleCombo->currentIndex();

    const QSignalBlocker b1(ui->formatCombo), b2(ui->webpModeCombo), b3(ui->scaleCombo);
    ui->formatCombo->clear();
    ui->formatCombo->addItem(QStringLiteral("PNG"), int(arx::OutputFormat::Png));
    ui->formatCombo->addItem(QStringLiteral("JPEG"), int(arx::OutputFormat::Jpeg));
    ui->formatCombo->addItem(QStringLiteral("WebP"), int(arx::OutputFormat::WebP));
    ui->webpModeCombo->clear();
    ui->webpModeCombo->addItem(tr("Lossy"), false);
    ui->webpModeCombo->addItem(tr("Lossless"), true);
    ui->scaleCombo->clear();
    ui->scaleCombo->addItem(QStringLiteral("2×"), 2);
    ui->scaleCombo->addItem(QStringLiteral("4×"), 4);

    ui->formatCombo->setCurrentIndex(fmt < 0 ? 0 : fmt);
    ui->webpModeCombo->setCurrentIndex(mode < 0 ? 0 : mode);
    ui->scaleCombo->setCurrentIndex(scale < 0 ? 0 : scale);
}

void MainWindow::updateQualityControls()
{
    const auto fmt = arx::OutputFormat(ui->formatCombo->currentData().toInt());
    const bool webp = fmt == arx::OutputFormat::WebP;
    const bool lossless = webp && ui->webpModeCombo->currentData().toBool();

    ui->webpModeCombo->setVisible(webp);
    QString hint;
    switch (fmt) {
    case arx::OutputFormat::Png:
        ui->qualitySpin->setEnabled(false);
        hint = tr("PNG is always lossless.");
        break;
    case arx::OutputFormat::Jpeg:
        ui->qualitySpin->setEnabled(!m_running);
        ui->qualitySpin->setMaximum(100);
        hint = tr("0-100, lossy");
        break;
    case arx::OutputFormat::WebP:
        ui->qualitySpin->setEnabled(!lossless && !m_running);
        ui->qualitySpin->setMaximum(99);
        hint = lossless ? tr("Lossless: quality setting not used.") : tr("0-99, lossy");
        break;
    }
    // A tooltip instead of an inline label: the hint length differs per
    // format and language and used to widen the window and shift the row.
    ui->qualitySpin->setToolTip(hint);
    ui->qualityLabel->setToolTip(hint);
}

void MainWindow::updateButtons()
{
    const bool idle = !m_running && !m_scanning;
    const bool hasItems = ui->fileList->topLevelItemCount() > 0;
    ui->addImagesButton->setEnabled(idle);
    ui->addFolderButton->setEnabled(idle);
    ui->removeButton->setEnabled(idle && !ui->fileList->selectedItems().isEmpty());
    ui->clearButton->setEnabled(idle && hasItems);
    ui->startButton->setEnabled(idle && hasItems);
    ui->cancelButton->setEnabled(m_running);
    ui->browseOutputButton->setEnabled(!m_running);
    ui->formatCombo->setEnabled(!m_running);
    ui->webpModeCombo->setEnabled(!m_running);
    ui->scaleCombo->setEnabled(!m_running);
    m_settingsAction->setEnabled(!m_running);
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
    spec.waifu2x.exePath = QDir(waifu2xDir()).filePath(QStringLiteral("waifu2x-ncnn-vulkan.exe"));
    spec.waifu2x.modelDir = QDir(waifu2xDir()).filePath(QStringLiteral("models-cunet"));
    spec.waifu2x.noise = m_settings->noiseLevel();
    spec.waifu2x.tile = m_settings->tileSize();
    spec.waifu2x.scale = ui->scaleCombo->currentData().toInt();
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
        setItemState(item, ItemState::Pending);
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

    // Scan off the GUI thread (folders may be on slow drives).
    m_scanning = true;
    updateButtons();
    m_status->setText(tr("Scanning folder..."));
    m_scanPool.start([this, dir] {
        const QStringList files = arx::scanFolder(dir);
        QMetaObject::invokeMethod(this, [this, dir, files] {
            m_scanning = false;
            if (files.isEmpty()) {
                m_status->setText(tr("No supported images (PNG, JPEG, WebP) in %1.")
                                             .arg(QDir::toNativeSeparators(dir)));
                updateButtons();
                return;
            }
            addFiles(files);
        }, Qt::QueuedConnection);
    });
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
    item->setForeground(ColStatus, state == ItemState::Failed ? QBrush(Qt::red) : QBrush());
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
    if (m_running || ui->fileList->topLevelItemCount() == 0)
        return;

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
    ui->progressBar->setRange(0, int(files.size()));
    ui->progressBar->setValue(0);
    ui->progressBar->setFormat(QStringLiteral("%v / %m"));
    m_status->setText(tr("Starting..."));
    setRunning(true);

    JobWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, files, spec] { worker->process(files, spec); },
                              Qt::QueuedConnection);
}

void MainWindow::onItemStarted(int index)
{
    QTreeWidgetItem *item = m_jobItems.value(index);
    if (!item)
        return;
    setItemState(item, ItemState::Processing);
    ui->fileList->scrollToItem(item);
    m_status->setText(tr("Processing %1 / %2: %3 (AI upscaling may take a while)")
                                 .arg(index + 1)
                                 .arg(m_jobItems.size())
                                 .arg(item->text(ColFile)));
    if (item == ui->fileList->currentItem())
        updatePreview();
}

void MainWindow::onItemFinished(int index, const arx::ItemResult &r)
{
    QTreeWidgetItem *item = m_jobItems.value(index);
    if (!item)
        return;
    if (r.ok) {
        item->setData(ColFile, kRoleOutput, r.outputPath);
        setItemState(item, ItemState::Done, QFileInfo(r.outputPath).fileName());
    } else if (r.canceled) {
        setItemState(item, ItemState::NotProcessed, tr("Canceled"));
    } else {
        setItemState(item, ItemState::Failed, r.error);
    }
    if (!r.canceled)
        ui->progressBar->setValue(++m_finishedCount);
    if (item == ui->fileList->currentItem())
        updatePreview();
}

void MainWindow::onJobFinished(const arx::BatchSummary &summary)
{
    setRunning(false);

    // Counts come from this job's items in the list, so the dialog matches it.
    arx::BatchSummary shown = summary;
    shown.succeeded = shown.failed = shown.notProcessed = 0;
    QStringList failures;
    for (QTreeWidgetItem *item : std::as_const(m_jobItems)) {
        switch (ItemState(item->data(ColFile, kRoleState).toInt())) {
        case ItemState::Done:
            ++shown.succeeded;
            break;
        case ItemState::Failed:
            ++shown.failed;
            failures << QStringLiteral("%1: %2").arg(item->text(ColFile),
                                                     item->data(ColFile, kRoleDetail).toString());
            break;
        case ItemState::Pending:
        case ItemState::Processing:
            setItemState(item, ItemState::NotProcessed);
            ++shown.notProcessed;
            break;
        case ItemState::NotProcessed:
            ++shown.notProcessed;
            break;
        }
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
    CompletionDialog dlg(shown, m_jobOutputDir, failures, this);
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
        // Kill the inference child and let the worker remove its temp files.
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
    if (watched == ui->fileList->viewport() && event->type() == QEvent::Resize)
        updateDetailColumnWidth();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateDetailColumnWidth()
{
    // In a narrow list the Details column is hidden; the reason stays
    // available in the File / Status tooltips.
    const int ch = ui->fileList->fontMetrics().averageCharWidth();
    const int view = ui->fileList->viewport()->width();
    const bool show = view >= ch * 40;
    if (ui->fileList->isColumnHidden(ColDetail) == show)
        ui->fileList->setColumnHidden(ColDetail, !show);
    if (show)
        ui->fileList->header()->resizeSection(ColDetail, std::clamp(view * 3 / 10, ch * 6, ch * 24));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
    QMainWindow::changeEvent(event);
}
