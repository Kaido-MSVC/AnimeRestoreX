#include "ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "core/FolderScanner.h"
#include "ui/CompletionDialog.h"
#include "ui/JobWorker.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>

namespace {

// Stage 1/2 provisional defaults; final values are decided by the A9 tests
// and become configurable in the settings dialog (stage 3).
constexpr int kDefaultNoise = 1;
constexpr int kDefaultTile = 0;  // auto

constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleState = Qt::UserRole + 1;
constexpr int kRoleDetail = Qt::UserRole + 2;
constexpr int kRoleOutput = Qt::UserRole + 3;

enum Column { ColFile = 0, ColStatus = 1, ColDetail = 2 };

QString waifu2xDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("waifu2x"));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);
    populateCombos();

    ui->mainSplitter->setStretchFactor(0, 2);
    ui->mainSplitter->setStretchFactor(1, 3);
    ui->fileList->header()->setSectionResizeMode(ColFile, QHeaderView::Interactive);
    ui->fileList->header()->resizeSection(ColFile, 200);
    ui->fileList->header()->resizeSection(ColStatus, 90);
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->progressBar->setFormat(QStringLiteral("%v / %m"));

    m_lastSourceDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    ui->outputDirEdit->setText(QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));

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
        ui->statusLabel->setText(tr("Canceling..."));
    });
    connect(ui->fileList, &QTreeWidget::currentItemChanged, this, &MainWindow::updatePreview);
    connect(ui->fileList, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(ui->formatCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateQualityControls);
    connect(ui->webpModeCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateQualityControls);
    updateQualityControls();
    updateButtons();
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

// ------------------------------------------------------------------ controls

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
    switch (fmt) {
    case arx::OutputFormat::Png:
        ui->qualitySpin->setEnabled(false);
        ui->qualityHint->setText(tr("PNG is always lossless."));
        break;
    case arx::OutputFormat::Jpeg:
        ui->qualitySpin->setEnabled(!m_running);
        ui->qualitySpin->setMaximum(100);
        ui->qualityHint->setText(tr("0-100, lossy"));
        break;
    case arx::OutputFormat::WebP:
        ui->qualitySpin->setEnabled(!lossless && !m_running);
        ui->qualitySpin->setMaximum(99);
        ui->qualityHint->setText(lossless ? tr("Lossless: quality setting not used.")
                                          : tr("0-99, lossy"));
        break;
    }
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
    spec.waifu2x.noise = kDefaultNoise;
    spec.waifu2x.tile = kDefaultTile;
    spec.waifu2x.scale = ui->scaleCombo->currentData().toInt();
    spec.encode.format = arx::OutputFormat(ui->formatCombo->currentData().toInt());
    spec.encode.webpLossless = ui->webpModeCombo->currentData().toBool();
    spec.encode.quality = ui->qualitySpin->value();
    return spec;
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
        item->setToolTip(ColFile, QDir::toNativeSeparators(abs));
        item->setData(ColFile, kRolePath, abs);
        setItemState(item, ItemState::Pending);
        ++added;
    }

    QStringList msg;
    msg << tr("Added %n image(s).", nullptr, added);
    if (duplicates > 0)
        msg << tr("Skipped %n duplicate(s).", nullptr, duplicates);
    if (unsupported > 0)
        msg << tr("Skipped %n unsupported file(s).", nullptr, unsupported);
    ui->statusLabel->setText(msg.join(QLatin1Char(' ')));

    if (!ui->fileList->currentItem() && ui->fileList->topLevelItemCount() > 0)
        ui->fileList->setCurrentItem(ui->fileList->topLevelItem(0));
    updateButtons();
    return added;
}

void MainWindow::browseImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Images"), m_lastSourceDir, tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (files.isEmpty())
        return;  // canceled: keep the previous source folder
    m_lastSourceDir = QFileInfo(files.first()).absolutePath();
    addFiles(files);
}

void MainWindow::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"), m_lastSourceDir);
    if (dir.isEmpty())
        return;
    m_lastSourceDir = dir;

    // Scan off the GUI thread (folders may be on slow drives).
    m_scanning = true;
    updateButtons();
    ui->statusLabel->setText(tr("Scanning folder..."));
    m_scanPool.start([this, dir] {
        const QStringList files = arx::scanFolder(dir);
        QMetaObject::invokeMethod(this, [this, dir, files] {
            m_scanning = false;
            if (files.isEmpty()) {
                ui->statusLabel->setText(tr("No supported images (PNG, JPEG, WebP) in %1.")
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
        return;
    ui->outputDirEdit->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::removeSelected()
{
    const QList<QTreeWidgetItem *> items = ui->fileList->selectedItems();
    for (QTreeWidgetItem *item : items) {
        m_keys.remove(pathKey(item->data(ColFile, kRolePath).toString()));
        delete item;
    }
    if (ui->fileList->topLevelItemCount() == 0)
        ui->previewPane->clear();
    updateButtons();
}

void MainWindow::clearList()
{
    ui->fileList->clear();
    m_keys.clear();
    ui->previewPane->clear();
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->statusLabel->clear();
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
    item->setText(ColStatus, stateText(state));
    item->setText(ColDetail, detail);
    item->setToolTip(ColDetail, detail);
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
    QString output;
    QString placeholder;
    switch (state) {
    case ItemState::Done:
        output = item->data(ColFile, kRoleOutput).toString();
        break;
    case ItemState::Failed:
        placeholder = tr("Processing failed. See the Details column.");
        break;
    case ItemState::Processing:
        placeholder = tr("Processing...");
        break;
    case ItemState::NotProcessed:
        placeholder = tr("Not processed.");
        break;
    case ItemState::Pending:
        break;
    }
    ui->previewPane->showItem(item->data(ColFile, kRolePath).toString(), output, placeholder);
}

// -------------------------------------------------------------------- batch

void MainWindow::start()
{
    if (m_running || ui->fileList->topLevelItemCount() == 0)
        return;

    // Every start processes the whole list; existing outputs are never
    // overwritten (new files get " (n)" suffixes).
    QStringList files;
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = ui->fileList->topLevelItem(i);
        files << item->data(ColFile, kRolePath).toString();
        item->setData(ColFile, kRoleOutput, QString());
        setItemState(item, ItemState::Pending);
    }
    updatePreview();

    const arx::JobSpec spec = currentSpec();
    m_jobOutputDir = spec.outputDir;
    m_finishedCount = 0;
    ui->progressBar->setRange(0, int(files.size()));
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Starting..."));
    setRunning(true);

    JobWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, files, spec] { worker->process(files, spec); },
                              Qt::QueuedConnection);
}

void MainWindow::onItemStarted(int index)
{
    QTreeWidgetItem *item = ui->fileList->topLevelItem(index);
    if (!item)
        return;
    setItemState(item, ItemState::Processing);
    ui->fileList->scrollToItem(item);
    ui->statusLabel->setText(tr("Processing %1 / %2: %3 (AI upscaling may take a while)")
                                 .arg(index + 1)
                                 .arg(ui->fileList->topLevelItemCount())
                                 .arg(item->text(ColFile)));
}

void MainWindow::onItemFinished(int index, const arx::ItemResult &r)
{
    QTreeWidgetItem *item = ui->fileList->topLevelItem(index);
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

    // Counts come from the list itself so the dialog always matches it.
    arx::BatchSummary shown = summary;
    shown.succeeded = shown.failed = shown.notProcessed = 0;
    QStringList failures;
    for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = ui->fileList->topLevelItem(i);
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

    if (!summary.fatalError.isEmpty())
        ui->statusLabel->setText(tr("Stopped: %1").arg(summary.fatalError));
    else if (summary.canceled)
        ui->statusLabel->setText(tr("Canceled."));
    else
        ui->statusLabel->setText(tr("Finished: %1 succeeded, %2 failed.")
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
    event->accept();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        populateCombos();
        updateQualityControls();
        for (int i = 0; i < ui->fileList->topLevelItemCount(); ++i)
            refreshItemTexts(ui->fileList->topLevelItem(i));
    }
    QMainWindow::changeEvent(event);
}
