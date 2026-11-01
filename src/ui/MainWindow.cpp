#include "ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "core/ImageEncoder.h"
#include "ui/JobWorker.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>

namespace {

// Stage 1 provisional defaults; final values are decided by the A9 tests
// and become configurable in the settings dialog (stage 3).
constexpr int kDefaultNoise = 1;
constexpr int kDefaultTile = 0;  // auto

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

    ui->outputDirEdit->setText(QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));

    m_worker = new JobWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &JobWorker::itemStarted, this, [this](int) {
        ui->statusLabel->setText(tr("Processing... (AI upscaling may take a while)"));
    });
    connect(m_worker, &JobWorker::itemFinished, this, [this](int, const arx::ItemResult &r) {
        if (r.ok)
            ui->statusLabel->setText(tr("Done: %1").arg(QDir::toNativeSeparators(r.outputPath)));
        else if (r.canceled)
            ui->statusLabel->setText(tr("Canceled."));
        else
            ui->statusLabel->setText(tr("Failed: %1").arg(r.error));
    });
    connect(m_worker, &JobWorker::jobFinished, this,
            [this](int, int, bool canceled, const QString &fatal) {
                if (!fatal.isEmpty())
                    ui->statusLabel->setText(tr("Cannot start: %1").arg(fatal));
                else if (canceled)
                    ui->statusLabel->setText(tr("Canceled."));
                setRunning(false);
            });
    m_thread.start();

    connect(ui->browseInputButton, &QPushButton::clicked, this, &MainWindow::browseInput);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(ui->cancelButton, &QPushButton::clicked, this, [this] {
        m_worker->requestCancel();
        ui->statusLabel->setText(tr("Canceling..."));
    });
    connect(ui->formatCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateQualityControls);
    connect(ui->webpModeCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateQualityControls);
    updateQualityControls();
}

MainWindow::~MainWindow()
{
    if (m_thread.isRunning()) {
        m_worker->requestCancel();
        m_thread.quit();
        m_thread.wait();
    }
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

void MainWindow::setRunning(bool running)
{
    m_running = running;
    ui->startButton->setEnabled(!running);
    ui->cancelButton->setEnabled(running);
    ui->browseInputButton->setEnabled(!running);
    ui->browseOutputButton->setEnabled(!running);
    ui->formatCombo->setEnabled(!running);
    ui->webpModeCombo->setEnabled(!running);
    ui->scaleCombo->setEnabled(!running);
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

void MainWindow::browseInput()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (file.isEmpty())
        return;
    m_files = {file};
    ui->inputEdit->setText(QDir::toNativeSeparators(file));
}

void MainWindow::browseOutput()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Folder"), QDir::fromNativeSeparators(ui->outputDirEdit->text()));
    if (dir.isEmpty())
        return;
    ui->outputDirEdit->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::start()
{
    if (m_running)
        return;
    if (m_files.isEmpty()) {
        ui->statusLabel->setText(tr("Please select an image first."));
        return;
    }
    setRunning(true);
    ui->statusLabel->setText(tr("Starting..."));
    const QStringList files = m_files;
    const arx::JobSpec spec = currentSpec();
    JobWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, files, spec] { worker->process(files, spec); },
                              Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_running) {
        // Kill the inference child and let the worker remove its temp files.
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
    }
    QMainWindow::changeEvent(event);
}
