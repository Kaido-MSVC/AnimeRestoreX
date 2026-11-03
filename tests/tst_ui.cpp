// Widget-level tests (run with QT_QPA_PLATFORM=offscreen). The full-batch test
// drives the real main window and uses the Windows ML runtime and models deployed
// next to the test exe by the app build.
#include "TestImages.h"

#include "ui/AboutDialog.h"
#include "ui/CompletionDialog.h"
#include "ui/ElidedLabel.h"
#include "ui/EulaDialog.h"
#include "ui/LanguageManager.h"
#include "ui/WindowPlacement.h"
#include "ui/SettingsDialog.h"
#include "ui/MainWindow.h"
#include "ui/PreviewPane.h"
#include "ui/Theme.h"

#include "core/AppSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QScreen>
#include <QUrl>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>

#include <functional>
#include <QApplication>
#include <QDir>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {

// The app build copies the runtime DLLs and the models next to the test executables.
bool engineDeployed()
{
    const QDir app(QCoreApplication::applicationDirPath());
    return QFileInfo::exists(app.filePath(QStringLiteral("onnxruntime.dll"))) &&
           QFileInfo::exists(app.filePath(QStringLiteral("DirectML.dll"))) &&
           QFileInfo::exists(app.filePath(QStringLiteral("models/manifest.json")));
}

} // namespace

class TestUi : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void dialogMixedCounts();
    void dialogAllFailed();
    void openMissingFolderWarns();
    void addFilesSkipsDuplicates();
    void fullBatchThroughMainWindow();
    void cancelThroughMainWindow();
    void listElidesAndShowsFullReason();
    void folderMemoryRestoresIndependently();
    void autoLanguageIsNotSavedAsManual();
    void denoiseOnMainWindow();
    void oneXNeedsDenoise();
    void qualityShownOnlyWhenUsed();
    void startShowsPendingCountAndEmptyHint();
    void menusFollowWindowsLayout();
    void dropAddsFilesAndFolders();
    void dragOfNonLocalUrlsIsIgnored();
    void keyboardShortcuts();
    void languageSwitchRetranslates();
    void windowIconFromResources();
    void aboutDialogContents();
    void aboutDialogLicenseFolder();
    void licenseAgreement();
    void progressResetsWhenListEmpties();
    void windowFitsCommonScreens_data();
    void windowFitsCommonScreens();
    void minimumSizeWithinBudgetAllLanguages();
    void windowGeometryRestoresAndFallsBack();
    void statusMessagesStaySingleLine();
    void detailColumnFollowsListWidth();
    void themeStyleSheetIsComplete();
    void themeFollowsColorScheme();
    void listShowsThumbnails();

private:
    // Closes the next modal widget (message box / dialog) and returns its text.
    void autoCloseNextModal(QString *captured);
    QTemporaryDir m_dir;
};

void TestUi::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
    // Keep test settings out of the real registry.
    QCoreApplication::setOrganizationName(QStringLiteral("Kaido-MSVC-Test"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimeRestoreX-tst_ui"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("settings")));
    qRegisterMetaType<arx::JobSpec>();
    qRegisterMetaType<arx::ItemResult>();
    qRegisterMetaType<arx::BatchSummary>();
    qRegisterMetaType<QList<arx::ItemPlan>>();
    QVERIFY(m_dir.isValid());
    // Same look as the app (Fusion, font, style sheet), so the layout checks measure it.
    theme::install(*qApp);
}

void TestUi::themeStyleSheetIsComplete()
{
    for (auto mode : {theme::Mode::Light, theme::Mode::Dark}) {
        const QString qss = theme::styleSheet(mode);
        // Every {token} was replaced.
        QVERIFY2(!qss.contains(QRegularExpression(QStringLiteral("\\{[a-z_]+\\}"))), qPrintable(qss.left(200)));
        // The chevron images of this mode exist in the resources (1x and 2x).
        const QString m = mode == theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        for (const QString &dir : {QStringLiteral("down"), QStringLiteral("up")}) {
            QVERIFY(qss.contains(QStringLiteral(":/theme/chevron-%1-%2.png").arg(dir, m)));
            QVERIFY(QFile::exists(QStringLiteral(":/theme/chevron-%1-%2.png").arg(dir, m)));
            QVERIFY(QFile::exists(QStringLiteral(":/theme/chevron-%1-%2@2x.png").arg(dir, m)));
        }
    }
    QVERIFY(theme::palette(theme::Mode::Light).color(QPalette::Window).lightness() > 200);
    QVERIFY(theme::palette(theme::Mode::Dark).color(QPalette::Window).lightness() < 60);
}


void TestUi::autoCloseNextModal(QString *captured)
{
    auto *timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [timer, captured] {
        QWidget *w = QApplication::activeModalWidget();
        if (!w)
            return;
        if (auto *dlg = qobject_cast<CompletionDialog *>(w))
            *captured = dlg->summaryText();
        else if (qobject_cast<EulaDialog *>(w))
            *captured = w->findChild<QLabel *>()->text();
        else
            *captured = w->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"))
                            ? w->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"))->text()
                            : QStringLiteral("<modal>");
        w->close();
        timer->stop();
        timer->deleteLater();
    });
    timer->start();
}

void TestUi::dialogMixedCounts()
{
    arx::BatchSummary s;
    s.succeeded = 2;
    s.failed = 1;
    s.smallerTile = 1;
    using Item = CompletionDialog::Item;
    CompletionDialog dlg(s, m_dir.path(),
                         {Item{Item::Succeeded, QStringLiteral("a.png"), QStringLiteral("a_2x.png"), 0},
                          Item{Item::Failed, QStringLiteral("bad.png"), QStringLiteral("Cannot decode"), 0},
                          Item{Item::Succeeded, QStringLiteral("b.png"), QStringLiteral("b_2x.png"), 160}});
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Succeeded: 2")));
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Failed: 1")));
    QVERIFY(dlg.summaryText().contains(QStringLiteral("smaller tile size 160 (GPU memory): 1")));
    QVERIFY(!dlg.summaryText().contains(QStringLiteral("Not processed")));
    QVERIFY(dlg.openFolderButton());
    QVERIFY(dlg.findChild<QPlainTextEdit *>(QStringLiteral("itemList")));
    // Every item is listed under its kind; the tile-160 item is marked.
    const QString items = dlg.itemsText();
    QVERIFY2(items.contains(QStringLiteral("Failed (1):\n  bad.png: Cannot decode")), qPrintable(items));
    QVERIFY2(items.contains(QStringLiteral("Succeeded (2):\n  a.png → a_2x.png\n  b.png → b_2x.png [tile 160]")),
             qPrintable(items));
    QVERIFY(!items.contains(QStringLiteral("Not processed")));
}

void TestUi::dialogAllFailed()
{
    arx::BatchSummary s;
    s.failed = 1;
    s.notProcessed = 2;
    s.fatalError = QStringLiteral("No usable Vulkan GPU");
    using Item = CompletionDialog::Item;
    CompletionDialog dlg(s, m_dir.path(),
                         {Item{Item::Failed, QStringLiteral("a.png"), QStringLiteral("GPU"), 0},
                          Item{Item::NotProcessed, QStringLiteral("b.png"), QString(), 0},
                          Item{Item::NotProcessed, QStringLiteral("c.png"), QString(), 0}});
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Succeeded: 0")));
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Not processed: 2")));
    QVERIFY2(dlg.itemsText().contains(QStringLiteral("Not processed (2):\n  b.png\n  c.png")),
             qPrintable(dlg.itemsText()));
    bool foundNoOutput = false, foundFatal = false;
    for (QLabel *l : dlg.findChildren<QLabel *>()) {
        foundNoOutput |= l->text().contains(QStringLiteral("No output files"));
        foundFatal |= l->text().contains(QStringLiteral("No usable Vulkan GPU"));
    }
    QVERIFY(foundNoOutput);
    QVERIFY(foundFatal);
}

void TestUi::openMissingFolderWarns()
{
    QString captured;
    autoCloseNextModal(&captured);
    QVERIFY(!CompletionDialog::openFolder(nullptr, m_dir.filePath(QStringLiteral("gone"))));
    QVERIFY(captured.contains(QStringLiteral("no longer exists")));
}

void TestUi::addFilesSkipsDuplicates()
{
    const QString a = m_dir.filePath(QStringLiteral("dup.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    MainWindow w;
    QCOMPARE(w.addFiles({a, a, a.toUpper(), m_dir.filePath(QStringLiteral("x.bmp"))}), 1);
    QCOMPARE(w.findChild<QTreeWidget *>(QStringLiteral("fileList"))->topLevelItemCount(), 1);
}

void TestUi::cancelThroughMainWindow()
{
    // Real engine: cancel a 4x image during inference. The UI keeps responding the whole
    // time, the batch ends within a few seconds, and nothing is left in the output folder.
    if (!engineDeployed())
        QSKIP("onnxruntime.dll / models are not deployed next to the test executable");
    const QString src = m_dir.filePath(QStringLiteral("取消 入力"));
    const QString out = m_dir.filePath(QStringLiteral("取消 出力"));
    QVERIFY(QDir().mkpath(src));
    QVERIFY(QDir().mkpath(out));
    const QString big = QDir(src).filePath(QStringLiteral("big.png"));
    const QString small = QDir(src).filePath(QStringLiteral("small.png"));
    QVERIFY(testimg::make(2000, 1500, false).save(big, "png"));  // 3 MP -> 48 MP at 4x
    QVERIFY(testimg::make(64, 48, false).save(small, "png"));

    MainWindow w;
    w.show();
    QCOMPARE(w.addFiles({big, small}), 2);
    w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->setText(QDir::toNativeSeparators(out));
    auto *scale = w.findChild<QComboBox *>(QStringLiteral("scaleCombo"));
    scale->setCurrentIndex(scale->findData(4));
    auto *itemLabel = w.findChild<ElidedLabel *>(QStringLiteral("itemProgressLabel"));
    auto *cancel = w.findChild<QPushButton *>(QStringLiteral("cancelButton"));

    // Largest gap between two 10 ms ticks of the GUI event loop.
    QElapsedTimer tick;
    qint64 maxGap = 0;
    QTimer probe;
    probe.setInterval(10);
    connect(&probe, &QTimer::timeout, this, [&] {
        maxGap = qMax(maxGap, tick.restart());
    });

    QString summary;
    autoCloseNextModal(&summary);
    tick.start();
    probe.start();
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(itemLabel->text().startsWith(QStringLiteral("AI processing")), 30000);
    // Session creation (a few seconds) happens at the start of the stage; cancel after it,
    // while tiles are running (the 4x image needs well over 10 s on a GTX 1060).
    QTest::qWait(6000);
    QVERIFY2(summary.isEmpty(), "the image finished before the cancel; use a larger image");
    QElapsedTimer sinceCancel;
    sinceCancel.start();
    cancel->click();
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 30000);
    const qint64 cancelMs = sinceCancel.elapsed();
    probe.stop();
    qInfo() << "cancel -> summary ms:" << cancelMs << "max GUI gap ms:" << maxGap;

    QVERIFY2(summary.contains(QStringLiteral("Not processed: 2")), qPrintable(summary));
    QVERIFY(cancelMs < 5000);
    QVERIFY2(maxGap < 500, qPrintable(QString::number(maxGap)));
    QCOMPARE(QDir(out).entryList(QDir::Files | QDir::Hidden).size(), 0);  // no output, no .tmp
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QVERIFY(list->topLevelItem(0)->text(1) != QStringLiteral("Done"));
    QVERIFY(w.findChild<QPushButton *>(QStringLiteral("startButton"))->isEnabled());
}

void TestUi::fullBatchThroughMainWindow()
{
    if (!engineDeployed())
        QSKIP("onnxruntime.dll / models are not deployed next to the test executable");

    const QString src = m_dir.filePath(QStringLiteral("批次 入力"));
    const QString out = m_dir.filePath(QStringLiteral("批次 出力"));
    QVERIFY(QDir().mkpath(src));
    QVERIFY(QDir().mkpath(out));
    const QString good1 = QDir(src).filePath(QStringLiteral("1.png"));
    const QString good2 = QDir(src).filePath(QStringLiteral("2.jpg"));
    const QString bad = QDir(src).filePath(QStringLiteral("壊れ.png"));
    QVERIFY(testimg::make(64, 48, true).save(good1, "png"));
    QVERIFY(testimg::make(48, 64, false).convertToFormat(QImage::Format_RGB32).save(good2, "jpeg"));
    {
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\nbroken");
    }

    MainWindow w;
    w.show();
    QCOMPARE(w.addFiles({good1, bad, good2}), 3);
    w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->setText(QDir::toNativeSeparators(out));

    QString summary;
    autoCloseNextModal(&summary);
    auto *bar = w.findChild<QProgressBar *>(QStringLiteral("progressBar"));
    auto *itemBar = w.findChild<QProgressBar *>(QStringLiteral("itemProgressBar"));
    QVERIFY(!itemBar->isVisible());
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    // While running: per-image bar and a batch bar marked as an estimate.
    QVERIFY(itemBar->isVisible());
    QVERIFY2(bar->text().startsWith(QStringLiteral("0 / 3 · est. ")), qPrintable(bar->text()));
    // The text is shown by the label above the thin bar.
    auto *batchLabel = w.findChild<ElidedLabel *>(QStringLiteral("batchProgressLabel"));
    QCOMPARE(batchLabel->text(), bar->text());
    QVERIFY(w.findChild<ElidedLabel *>(QStringLiteral("itemProgressLabel"))->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 120000);

    QVERIFY2(summary.contains(QStringLiteral("Succeeded: 2")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("Failed: 1")), qPrintable(summary));
    QVERIFY(!itemBar->isVisible());
    QCOMPARE(bar->text(), QStringLiteral("3 / 3"));

    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Done"));
    QCOMPARE(list->topLevelItem(1)->text(1), QStringLiteral("Failed"));
    QVERIFY(!list->topLevelItem(1)->text(2).isEmpty());
    QCOMPARE(list->topLevelItem(2)->text(1), QStringLiteral("Done"));
    QCOMPARE(bar->value(), 3);
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("1_2x.png"))));
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("2_2x.png"))));
    // Measured speeds are stored under v2/, per GPU, grouped by model hash, EP, tile, scale.
    {
        QSettings raw;
        raw.beginGroup(QStringLiteral("v2/progress/rates"));
        const QStringList devices = raw.childGroups();
        QCOMPARE(devices.size(), 1);
        raw.beginGroup(devices.first());
        const QStringList keys = raw.allKeys();
        const QRegularExpression inference(
            QStringLiteral("^nunif/swin_unet/art/noise1_scale2x@[0-9a-f]{8}/dml/t256/x2$"));
        QVERIFY2(std::any_of(keys.cbegin(), keys.cend(), [&](const QString &k) { return inference.match(k).hasMatch(); }),
                 qPrintable(keys.join(QLatin1Char(' '))));
        qInfo() << "stored rate keys:" << keys;
    }
    QVERIFY(!QSettings().contains(QStringLiteral("progress/lastDevice")));  // v1.2 key untouched
    QVERIFY(w.findChild<QPushButton *>(QStringLiteral("startButton"))->isEnabled());

    // Second start: only the non-"Done" item (the broken one) is processed.
    const int filesBefore = int(QDir(out).entryList(QDir::Files).size());
    summary.clear();
    autoCloseNextModal(&summary);
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 120000);
    QVERIFY2(summary.contains(QStringLiteral("Succeeded: 0")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("Failed: 1")), qPrintable(summary));
    QCOMPARE(int(QDir(out).entryList(QDir::Files).size()), filesBefore);  // no "(1)" copies
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Done"));
    QCOMPARE(bar->maximum(), 1);

    // Changing an output setting resets finished items to "Pending".
    auto *scale = w.findChild<QComboBox *>(QStringLiteral("scaleCombo"));
    scale->setCurrentIndex(scale->findData(4));
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Pending"));
    QCOMPARE(list->topLevelItem(2)->text(1), QStringLiteral("Pending"));
    QCOMPARE(list->topLevelItem(1)->text(1), QStringLiteral("Failed"));  // untouched
    summary.clear();
    autoCloseNextModal(&summary);
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 120000);
    QVERIFY2(summary.contains(QStringLiteral("Succeeded: 2")), qPrintable(summary));
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("1_4x.png"))));
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("2_4x.png"))));

    // The other output settings reset finished items as well.
    for (auto change : std::initializer_list<std::function<void()>>{
             [&] { w.findChild<QComboBox *>(QStringLiteral("formatCombo"))->setCurrentIndex(2); },
             [&] { w.findChild<QComboBox *>(QStringLiteral("webpModeCombo"))->setCurrentIndex(1); },
             [&] { w.findChild<QComboBox *>(QStringLiteral("webpModeCombo"))->setCurrentIndex(0); },
             [&] { w.findChild<QSpinBox *>(QStringLiteral("qualitySpin"))->setValue(50); }}) {
        list->topLevelItem(0)->setData(0, Qt::UserRole + 1, int(MainWindow::ItemState::Done));
        change();
        QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Pending"));
    }

    // Optional visual check: ARX_UI_SNAPSHOT=<dir> saves screenshots of the preview.
    const QString snapDir = qEnvironmentVariable("ARX_UI_SNAPSHOT");
    if (!snapDir.isEmpty()) {
        w.resize(1100, 720);
        list->setCurrentItem(list->topLevelItem(0));
        QTest::qWait(1500);
        w.grab().save(QDir(snapDir).filePath(QStringLiteral("preview_fit.png")));
        auto *mode = w.findChild<PreviewPane *>()->findChild<QComboBox *>();
        mode->setCurrentIndex(1);
        QTest::qWait(500);
        w.grab().save(QDir(snapDir).filePath(QStringLiteral("preview_100.png")));
        list->setCurrentItem(list->topLevelItem(1));
        QTest::qWait(800);
        w.grab().save(QDir(snapDir).filePath(QStringLiteral("preview_failed.png")));
    }
}

void TestUi::listElidesAndShowsFullReason()
{
    // A real-size rejected image (30.8 MP) like the one reported in stage 2,
    // checked through the list so the full reason is reachable.
    MainWindow w;
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QCOMPARE(list->textElideMode(), Qt::ElideMiddle);
    QCOMPARE(list->header()->sectionResizeMode(0), QHeaderView::Stretch);

    const QString path = m_dir.filePath(QStringLiteral("very long file name for elide check 4800x6416.png"));
    QImage big(4800, 6416, QImage::Format_RGB32);
    big.fill(Qt::white);
    QVERIFY(big.save(path, "png"));
    w.addFiles({path});
    w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->setText(QDir::toNativeSeparators(m_dir.path()));

    if (!engineDeployed())
        QSKIP("onnxruntime.dll / models are not deployed next to the test executable");
    QString summary;
    autoCloseNextModal(&summary);
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 60000);

    QTreeWidgetItem *item = list->topLevelItem(0);
    QCOMPARE(item->text(1), QStringLiteral("Failed"));
    const QString reason = item->text(2);
    QVERIFY2(reason.contains(QStringLiteral("30.8 MP")) && reason.contains(QStringLiteral("4.5 MP")),
             qPrintable(reason));
    QVERIFY(item->toolTip(2).contains(QStringLiteral("30.8 MP")));
    QVERIFY(item->toolTip(1).contains(QStringLiteral("30.8 MP")));
    QVERIFY(item->toolTip(0).contains(QStringLiteral("4800x6416.png")));
}

void TestUi::folderMemoryRestoresIndependently()
{
    const QString src = m_dir.filePath(QStringLiteral("記憶 來源"));
    const QString out = m_dir.filePath(QStringLiteral("記憶 出力"));
    QVERIFY(QDir().mkpath(src));
    QVERIFY(QDir().mkpath(out));
    {
        arx::AppSettings s;
        s.setSourceDir(src);
        s.setOutputDir(out);
        QVERIFY(s.sync());
    }
    {
        MainWindow w;  // "restart"
        QCOMPARE(QDir::cleanPath(w.sourceFolder()), QDir::cleanPath(src));
        QCOMPARE(QDir::fromNativeSeparators(w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->text()),
                 QDir::cleanPath(out));
    }

    // The output folder disappears: only the output falls back.
    QVERIFY(QDir(out).removeRecursively());
    {
        MainWindow w;
        QCOMPARE(QDir::cleanPath(w.sourceFolder()), QDir::cleanPath(src));
        const QString shown = QDir::fromNativeSeparators(
            w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->text());
        QCOMPARE(shown, arx::defaultImageFolder());
        QVERIFY(w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("not available")));
    }
    // The stale value is not overwritten by the fallback.
    QCOMPARE(arx::AppSettings().outputDir(), out);

    // The source folder disappears: only the source falls back.
    QVERIFY(QDir().mkpath(out));
    QVERIFY(QDir(src).removeRecursively());
    {
        MainWindow w;
        QCOMPARE(w.sourceFolder(), arx::defaultImageFolder());
        QCOMPARE(QDir::fromNativeSeparators(w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->text()),
                 QDir::cleanPath(out));
    }
}

void TestUi::autoLanguageIsNotSavedAsManual()
{
    QSettings().remove(QStringLiteral("ui/language"));
    MainWindow w;
    // Settings dialog accepted without changing the language.
    w.applySettings(160, int(arx::ComputeDevice::Cpu), false, int(arx::Language::TraditionalChinese));
    arx::AppSettings s;
    QVERIFY(!s.manualLanguage().has_value());
    QCOMPARE(s.tileSize(), 160);
    QCOMPARE(s.computeDevice(), arx::ComputeDevice::Cpu);
    s.setTileSize(arx::AppSettings::kDefaultTile);
    s.setComputeDevice(arx::ComputeDevice::Gpu);
    QVERIFY(s.sync());
}

void TestUi::denoiseOnMainWindow()
{
    const QString a = m_dir.filePath(QStringLiteral("dn.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    {
        arx::AppSettings s;
        s.setNoiseLevel(2);
        QVERIFY(s.sync());
    }
    MainWindow w;
    auto *noise = w.findChild<QComboBox *>(QStringLiteral("noiseCombo"));
    QVERIFY(noise);
    QCOMPARE(noise->currentData().toInt(), 2);  // restored from the settings

    // Changing it is saved at once and resets finished items like other output settings.
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    w.addFiles({a});
    list->topLevelItem(0)->setData(0, Qt::UserRole + 1, int(MainWindow::ItemState::Done));
    noise->setCurrentIndex(noise->findData(3));
    QCOMPARE(arx::AppSettings().noiseLevel(), 3);
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Pending"));

    // The settings dialog has no denoise control any more.
    SettingsDialog sd(0, arx::ComputeDevice::Gpu, arx::Language::English, &w);
    QCOMPARE(sd.findChildren<QComboBox *>().size(), 3);  // tile + device + language
    // Only validated tiles; nothing at 640 or above.
    const QList<QComboBox *> combos = sd.findChildren<QComboBox *>();
    QComboBox *tiles = combos.first();
    QCOMPARE(tiles->count(), 4);
    for (int i = 0; i < tiles->count(); ++i)
        QVERIFY(arx::AppSettings::isValidTile(tiles->itemData(i).toInt()));
    // Devices: DirectML and CPU only; choosing the CPU explains that it is much slower.
    QComboBox *devices = combos.at(1);
    QCOMPARE(devices->count(), 2);
    auto hintText = [&] {
        QString all;
        for (QLabel *l : sd.findChildren<QLabel *>())
            all += l->text() + QLatin1Char('\n');
        return all;
    };
    QVERIFY(!hintText().contains(QStringLiteral("much slower than the GPU")));
    devices->setCurrentIndex(devices->findData(int(arx::ComputeDevice::Cpu)));
    QVERIFY2(hintText().contains(QStringLiteral("much slower than the GPU")), qPrintable(hintText()));
    QCOMPARE(sd.computeDevice(), arx::ComputeDevice::Cpu);

    arx::AppSettings s;
    s.setNoiseLevel(arx::AppSettings::kDefaultNoise);
    QVERIFY(s.sync());
}

void TestUi::oneXNeedsDenoise()
{
    const QString a = m_dir.filePath(QStringLiteral("1x.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    MainWindow w;
    w.addFiles({a});
    auto *scale = w.findChild<QComboBox *>(QStringLiteral("scaleCombo"));
    auto *noise = w.findChild<QComboBox *>(QStringLiteral("noiseCombo"));
    auto *start = w.findChild<QPushButton *>(QStringLiteral("startButton"));
    QCOMPARE(scale->currentData().toInt(), 2);  // default stays 2x
    QVERIFY(scale->findData(1) >= 0);

    // "None" is disabled with 1x and comes back with 2x / 4x.
    auto *model = qobject_cast<QStandardItemModel *>(noise->model());
    QVERIFY(model);
    const int none = noise->findData(-1);
    noise->setCurrentIndex(none);
    QVERIFY(model->item(none)->isEnabled());
    scale->setCurrentIndex(scale->findData(1));
    QVERIFY(!model->item(none)->isEnabled());
    QCOMPARE(noise->currentData().toInt(), arx::AppSettings::kDefaultNoise);  // moved off "None"
    QVERIFY(start->isEnabled());
    scale->setCurrentIndex(scale->findData(4));
    QVERIFY(model->item(none)->isEnabled());
    scale->setCurrentIndex(scale->findData(1));

    // Second line of defense: 1x + no denoise (set from code) still cannot start.
    noise->setCurrentIndex(none);
    QVERIFY(!start->isEnabled());
    QVERIFY(start->toolTip().contains(QStringLiteral("1x")));
    QVERIFY(w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("1x")));
    scale->setCurrentIndex(scale->findData(2));
    QVERIFY(start->isEnabled());
    noise->setCurrentIndex(noise->findData(arx::AppSettings::kDefaultNoise));
}

void TestUi::qualityShownOnlyWhenUsed()
{
    MainWindow w;
    w.show();
    auto *format = w.findChild<QComboBox *>(QStringLiteral("formatCombo"));
    auto *mode = w.findChild<QComboBox *>(QStringLiteral("webpModeCombo"));
    auto *quality = w.findChild<QSpinBox *>(QStringLiteral("qualitySpin"));
    format->setCurrentIndex(0);  // PNG
    QVERIFY(!quality->isVisible());
    QVERIFY(format->toolTip().contains(QStringLiteral("lossless")));
    format->setCurrentIndex(1);  // JPEG
    QVERIFY(quality->isVisible());
    QCOMPARE(quality->maximum(), 100);
    format->setCurrentIndex(2);  // WebP
    mode->setCurrentIndex(0);    // lossy
    QVERIFY(quality->isVisible());
    QCOMPARE(quality->maximum(), 99);
    mode->setCurrentIndex(1);    // lossless
    QVERIFY(!quality->isVisible());
    format->setCurrentIndex(0);
}

void TestUi::startShowsPendingCountAndEmptyHint()
{
    const QString a = m_dir.filePath(QStringLiteral("c1.png"));
    const QString b = m_dir.filePath(QStringLiteral("c2.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    QVERIFY(testimg::make(8, 8, false).save(b, "png"));
    MainWindow w;
    w.show();
    auto *start = w.findChild<QPushButton *>(QStringLiteral("startButton"));
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QVERIFY(start->isDefault());
    QCOMPARE(start->text(), QStringLiteral("Start"));
    QLabel *hint = nullptr;
    for (QLabel *l : list->viewport()->findChildren<QLabel *>())
        hint = l;
    QVERIFY(hint && hint->isVisible());

    w.addFiles({a, b});
    QCOMPARE(start->text(), QStringLiteral("Start (2)"));
    QVERIFY(!hint->isVisible());
    list->topLevelItem(0)->setData(0, Qt::UserRole + 1, int(MainWindow::ItemState::Done));
    w.findChild<QComboBox *>(QStringLiteral("formatCombo"))->setCurrentIndex(0);  // no change: stays
    list->clearSelection();
    list->topLevelItem(1)->setSelected(true);
    w.findChild<QPushButton *>(QStringLiteral("removeButton"))->click();
    QCOMPARE(start->text(), QStringLiteral("Start"));  // only a Done item is left
    w.findChild<QPushButton *>(QStringLiteral("clearButton"))->click();
    QVERIFY(hint->isVisible());
}

void TestUi::menusFollowWindowsLayout()
{
    MainWindow w;
    QStringList titles;
    for (QAction *a : w.menuBar()->actions())
        titles << a->text();
    QCOMPARE(titles, (QStringList{QStringLiteral("&File"), QStringLiteral("&Tools"), QStringLiteral("&Help")}));
}

void TestUi::dropAddsFilesAndFolders()
{
    const QString dropDir = m_dir.filePath(QStringLiteral("拖放 フォルダ"));
    QVERIFY(QDir().mkpath(dropDir));
    const QString loose = m_dir.filePath(QStringLiteral("drop-loose.png"));
    const QString text = m_dir.filePath(QStringLiteral("drop-note.txt"));
    QVERIFY(testimg::make(8, 8, false).save(loose, "png"));
    QVERIFY(testimg::make(8, 8, false).save(QDir(dropDir).filePath(QStringLiteral("in-1.png")), "png"));
    QVERIFY(testimg::make(8, 8, false).convertToFormat(QImage::Format_RGB32)
                .save(QDir(dropDir).filePath(QStringLiteral("in-2.jpg")), "jpeg"));
    {
        QFile f(text);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    MainWindow w;
    w.show();
    const QString sourceBefore = w.sourceFolder();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(loose), QUrl::fromLocalFile(text), QUrl::fromLocalFile(dropDir)});
    const QPoint pos = w.rect().center();

    QDragEnterEvent enter(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &drop);
    QVERIFY(drop.isAccepted());

    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QTRY_COMPARE(list->topLevelItemCount(), 3);  // loose png + 2 from the folder (scanned off-thread)
    const QString status = w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"))->text();
    QVERIFY2(status.contains(QStringLiteral("Unsupported files skipped: 1")), qPrintable(status));
    QCOMPARE(w.sourceFolder(), sourceBefore);  // a drop does not change the dialog folder

    // Dropping the same things again adds nothing (a real drag enters first).
    QDragEnterEvent enterAgain(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &enterAgain);
    QVERIFY(enterAgain.isAccepted());
    QDropEvent again(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &again);
    QVERIFY(again.isAccepted());
    QTRY_VERIFY2(w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("Duplicates skipped: 3")),
                 qPrintable(w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"))->text()));
    QCOMPARE(list->topLevelItemCount(), 3);
}

void TestUi::dragOfNonLocalUrlsIsIgnored()
{
    MainWindow w;
    w.show();
    QMimeData mime;
    mime.setUrls({QUrl(QStringLiteral("https://example.com/a.png"))});
    QDragEnterEvent enter(QPointF(w.rect().center()), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &enter);
    QVERIFY(!enter.isAccepted());

    QMimeData textOnly;
    textOnly.setText(QStringLiteral("hello"));
    QDragEnterEvent enter2(QPointF(w.rect().center()), Qt::CopyAction, &textOnly, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &enter2);
    QVERIFY(!enter2.isAccepted());
}

void TestUi::keyboardShortcuts()
{
    MainWindow w;
    auto findAction = [&](const QKeySequence &key) -> QAction * {
        for (QAction *a : w.findChildren<QAction *>())
            if (a->shortcuts().contains(key))
                return a;
        return nullptr;
    };
    QVERIFY(findAction(QKeySequence(Qt::CTRL | Qt::Key_O)));
    QVERIFY(findAction(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)));
    QVERIFY(findAction(QKeySequence(Qt::CTRL | Qt::Key_Comma)));
    QAction *start = findAction(QKeySequence(Qt::CTRL | Qt::Key_Return));
    QVERIFY(start);
    QVERIFY(start->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Enter)));  // keypad Enter
    QCOMPARE(start->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Enter));  // shown as "Ctrl+Enter"

    // Same access key for "Add Folder" in the menu and on the button (O, not F:
    // Alt+F opens the File menu).
    auto *folderButton = w.findChild<QPushButton *>(QStringLiteral("addFolderButton"));
    QAction *folderAction = findAction(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QCOMPARE(folderAction->text(), folderButton->text());
    QVERIFY(folderAction->text().contains(QStringLiteral("&o")));
    QVERIFY(!start->isEnabled());  // empty list, like the Start button

    // Delete removes the selected items while the list has the focus.
    const QString a = m_dir.filePath(QStringLiteral("k1.png"));
    const QString b = m_dir.filePath(QStringLiteral("k2.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    QVERIFY(testimg::make(8, 8, false).save(b, "png"));
    w.addFiles({a, b});
    QVERIFY(start->isEnabled());
    w.show();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    list->setFocus();
    list->clearSelection();
    list->topLevelItem(0)->setSelected(true);
    QTest::keyClick(list, Qt::Key_Delete);
    QCOMPARE(list->topLevelItemCount(), 1);

    // No shortcut for Cancel (a stray key must not stop a long batch).
    auto *cancel = w.findChild<QPushButton *>(QStringLiteral("cancelButton"));
    QVERIFY(cancel->shortcut().isEmpty());
}

void TestUi::languageSwitchRetranslates()
{
    MainWindow w;
    auto *startButton = w.findChild<QPushButton *>(QStringLiteral("startButton"));
    QCOMPARE(startButton->text(), QStringLiteral("Start"));

    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_zh_TW.qm")), "embedded .qm missing");
    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_ja.qm")), "embedded .qm missing");

    // LanguageChange is delivered through the event loop.
    w.applySettings(0, int(arx::ComputeDevice::Gpu), true, int(arx::Language::TraditionalChinese));
    QCOMPARE(LanguageManager::instance().current(), arx::Language::TraditionalChinese);
    QTRY_COMPARE(startButton->text(), QStringLiteral("開始處理"));
    QCOMPARE(w.menuBar()->actions().first()->text(), QStringLiteral("檔案(&F)"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::TraditionalChinese));

    // Dialogs created after the switch are translated too.
    arx::BatchSummary s;
    s.succeeded = 1;
    CompletionDialog dlg(s, m_dir.path(), {});
    QVERIFY2(dlg.summaryText().contains(QStringLiteral("成功：1")), qPrintable(dlg.summaryText()));

    const QString snapDir = qEnvironmentVariable("ARX_UI_SNAPSHOT");
    // Light and dark theme for every language; the settings dialog with the CPU chosen
    // (longest hint text).
    auto snapshot = [&](const QString &lang) {
        if (snapDir.isEmpty())
            return;
        const theme::Mode before = theme::currentMode();
        // A neutral folder instead of the user's own Pictures path (screenshots go to docs/).
        w.findChild<QLineEdit *>(QStringLiteral("outputDirEdit"))->setText(QStringLiteral("D:\\Images\\Output"));
        for (theme::Mode mode : {theme::Mode::Light, theme::Mode::Dark}) {
            theme::apply(mode);
            const QString tag = lang + (mode == theme::Mode::Light ? QStringLiteral("_light")
                                                                    : QStringLiteral("_dark"));
            w.resize(1100, 720);
            w.show();
            QTest::qWait(300);
            w.grab().save(QDir(snapDir).filePath(QStringLiteral("main_%1.png").arg(tag)));
            SettingsDialog sd(0, arx::ComputeDevice::Cpu, LanguageManager::instance().current(), &w);
            sd.show();
            QTest::qWait(200);
            sd.grab().save(QDir(snapDir).filePath(QStringLiteral("settings_%1.png").arg(tag)));
            AboutDialog ad(&w);
            ad.setDeviceName(QStringLiteral("NVIDIA GeForce GTX 1060 6GB"));
            ad.show();
            QTest::qWait(200);
            ad.grab().save(QDir(snapDir).filePath(QStringLiteral("about_%1.png").arg(tag)));
        }
        theme::apply(before);
    };
    snapshot(QStringLiteral("zh_TW"));

    w.applySettings(0, int(arx::ComputeDevice::Gpu), true, int(arx::Language::Japanese));
    QTRY_COMPARE(startButton->text(), QStringLiteral("処理開始"));
    snapshot(QStringLiteral("ja"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::Japanese));

    w.applySettings(0, int(arx::ComputeDevice::Gpu), true, int(arx::Language::English));
    QTRY_COMPARE(startButton->text(), QStringLiteral("Start"));
    snapshot(QStringLiteral("en"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::English));
    QSettings().remove(QStringLiteral("ui/language"));
}

void TestUi::windowIconFromResources()
{
    for (int s : {16, 24, 32, 48, 64, 256})
        QVERIFY2(QFile::exists(QStringLiteral(":/icons/app_%1.png").arg(s)), "embedded icon PNG missing");
    MainWindow w;
    const QIcon icon = w.windowIcon();
    QVERIFY(!icon.isNull());
    const QList<QSize> sizes = icon.availableSizes();
    QVERIFY(sizes.contains(QSize(16, 16)));
    QVERIFY(sizes.contains(QSize(256, 256)));
}

void TestUi::aboutDialogContents()
{
    AboutDialog dlg;
    const QString text = dlg.bodyText();
    for (const QString &s : {QStringLiteral("AnimeRestoreX"), QStringLiteral(ARX_TEST_VERSION),
                             QStringLiteral("Kaido-MSVC"), QStringLiteral("MIT"),
                             QStringLiteral("Windows ML 2.5.96"), QStringLiteral("ONNX Runtime " ARX_ORT_VERSION),
                             QStringLiteral("DirectML"), QStringLiteral("nagadomi/nunif"), QStringLiteral("Qt 6.12"),
                             QStringLiteral("LGPL")})
        QVERIFY2(text.contains(s), qPrintable(s));
    // Versions as deployed (DirectML from the DLL next to the exe, models from the manifest).
    if (engineDeployed()) {
        QVERIFY2(text.contains(QStringLiteral("DirectML 1.15.4")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("waifu2x_onnx_models_20250502.zip")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("https://github.com/nagadomi/nunif")), qPrintable(text));
    }
    QVERIFY(text.contains(QStringLiteral("Processing device: not used yet in this session")));
    dlg.setDeviceName(QStringLiteral("NVIDIA GeForce GTX 1060 6GB"));
    QVERIFY(dlg.bodyText().contains(QStringLiteral("Processing device: NVIDIA GeForce GTX 1060 6GB")));
    QVERIFY(dlg.openLicensesButton());
    QVERIFY(dlg.aboutQtButton());

    // THIRD_PARTY_NOTICES.md is generated with the same version (title and exe row).
    QFile notices(QStringLiteral(ARX_TEST_NOTICES));
    QVERIFY2(notices.open(QIODevice::ReadOnly), qPrintable(notices.fileName()));
    const QString n = QString::fromUtf8(notices.readAll());
    QVERIFY(n.startsWith(QStringLiteral("# AnimeRestoreX " ARX_TEST_VERSION " ")));
    QVERIFY(n.contains(QStringLiteral("| `AnimeRestoreX.exe` | AnimeRestoreX | " ARX_TEST_VERSION " | MIT |")));
    QVERIFY(!n.contains(QStringLiteral("@PROJECT_VERSION@")));

    // Help menu entry exists in the main window.
    MainWindow w;
    bool found = false;
    for (QAction *a : w.menuBar()->actions())
        if (a->menu())
            for (QAction *sub : a->menu()->actions())
                found |= sub->text().contains(QStringLiteral("About AnimeRestoreX"));
    QVERIFY(found);

    // Translated together with the rest of the UI.
    LanguageManager::instance().apply(arx::Language::TraditionalChinese);
    AboutDialog zh;
    QCOMPARE(zh.windowTitle(), QStringLiteral("關於 AnimeRestoreX"));
    QVERIFY(zh.bodyText().contains(QStringLiteral("第三方元件")));
    LanguageManager::instance().apply(arx::Language::Japanese);
    AboutDialog ja;
    QCOMPARE(ja.windowTitle(), QStringLiteral("AnimeRestoreX について"));
    LanguageManager::instance().apply(arx::Language::English);
}

void TestUi::aboutDialogLicenseFolder()
{
    const QString dir = m_dir.filePath(QStringLiteral("授權 資料夾"));
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(!AboutDialog::licenseFilesPresent(dir));

    QString captured;
    autoCloseNextModal(&captured);
    QVERIFY(!AboutDialog::openLicenseFolder(nullptr, dir));
    QVERIFY2(captured.contains(QStringLiteral("not found")), qPrintable(captured));

    for (const char *name : {"LICENSE", "THIRD_PARTY_NOTICES.md"}) {
        QFile f(QDir(dir).filePath(QString::fromLatin1(name)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QVERIFY(AboutDialog::licenseFilesPresent(dir));
}

void TestUi::licenseAgreement()
{
    // Version and text: the embedded copy is the generated EULA.txt that is shipped.
    QCOMPARE(EulaDialog::currentVersion(), QStringLiteral(ARX_TEST_EULA_VERSION));
    const QString text = EulaDialog::agreementText();
    QVERIFY(!text.isEmpty());
    QFile shipped(QStringLiteral(ARX_TEST_EULA));
    QVERIFY2(shipped.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(shipped.fileName()));
    QCOMPARE(text, QString::fromUtf8(shipped.readAll()));
    QVERIFY(text.contains(QStringLiteral("EULA version: " ARX_TEST_EULA_VERSION)));
    QVERIFY(text.startsWith(QStringLiteral("AnimeRestoreX " ARX_TEST_VERSION " - End-User License Agreement")));
    QVERIFY(!text.contains(QLatin1Char('@')));  // every @VAR@ was filled in
    // Appendix A is the Microsoft licence, unmodified apart from line endings.
    QFile ms(QStringLiteral(ARX_TEST_WINML_LICENSE));
    QVERIFY(ms.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString msText = QString::fromUtf8(ms.readAll());
    QVERIFY(msText.contains(QStringLiteral("DISTRIBUTABLE CODE")));
    QVERIFY2(text.contains(msText.trimmed()), "Appendix A differs from licenses/WindowsML/license.txt");

    QVERIFY(!EulaDialog::isAccepted(QString()));
    QVERIFY(!EulaDialog::isAccepted(QStringLiteral("1.0.0-0")));  // an older version asks again
    QVERIFY(EulaDialog::isAccepted(EulaDialog::currentVersion()));

    // Consent: "I Agree" only after the check box; declining rejects.
    {
        EulaDialog dlg(EulaDialog::Mode::Consent);
        QVERIFY(dlg.textView()->isReadOnly());
        QCOMPARE(dlg.textView()->toPlainText(), text);
        QVERIFY(dlg.acceptCheck() && dlg.agreeButton() && dlg.declineButton());
        QVERIFY(!dlg.agreeButton()->isEnabled());
        dlg.acceptCheck()->setChecked(true);
        QVERIFY(dlg.agreeButton()->isEnabled());
        dlg.acceptCheck()->setChecked(false);
        QVERIFY(!dlg.agreeButton()->isEnabled());
        dlg.show();
        dlg.declineButton()->click();
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
    }
    {
        EulaDialog dlg(EulaDialog::Mode::Consent);
        dlg.show();
        dlg.acceptCheck()->setChecked(true);
        dlg.agreeButton()->click();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
    }
    {
        EulaDialog dlg(EulaDialog::Mode::View);
        QVERIFY(!dlg.acceptCheck() && !dlg.agreeButton());
        QCOMPARE(dlg.textView()->toPlainText(), text);
    }

    // Settings: the read-only helpers write nothing; acceptance is stored under v2/.
    {
        QSettings raw;
        raw.clear();
        raw.sync();
    }
    QCOMPARE(arx::AppSettings::acceptedEulaVersion(), QString());
    QVERIFY(!arx::AppSettings::storedManualLanguage().has_value());
    QVERIFY(QSettings().allKeys().isEmpty());
    {
        arx::AppSettings s;
        s.setAcceptedEulaVersion(EulaDialog::currentVersion());
        QVERIFY(s.sync());
    }
    QCOMPARE(QSettings().value(QStringLiteral("v2/eula/acceptedVersion")).toString(), EulaDialog::currentVersion());
    QVERIFY(EulaDialog::isAccepted(arx::AppSettings::acceptedEulaVersion()));
    for (const QString &k : QSettings().allKeys())
        QVERIFY2(k.startsWith(QStringLiteral("v2/")), qPrintable(k));

    // About opens it; interface text is translated, the agreement stays in English.
    AboutDialog about;
    QVERIFY(about.licenseAgreementButton());
    QCOMPARE(about.licenseAgreementButton()->text(), QStringLiteral("License Agreement"));
    QString captured;
    autoCloseNextModal(&captured);
    about.licenseAgreementButton()->click();
    QVERIFY2(captured.contains(QStringLiteral("original English")), qPrintable(captured));

    LanguageManager::instance().apply(arx::Language::TraditionalChinese);
    {
        EulaDialog zh(EulaDialog::Mode::Consent);
        QCOMPARE(zh.agreeButton()->text(), QStringLiteral("我同意"));
        QCOMPARE(zh.textView()->toPlainText(), text);
        QVERIFY(!zh.windowTitle().contains(QStringLiteral("License Agreement")));
    }
    LanguageManager::instance().apply(arx::Language::Japanese);
    {
        EulaDialog ja(EulaDialog::Mode::Consent);
        QCOMPARE(ja.agreeButton()->text(), QStringLiteral("同意する"));
        QVERIFY(!ja.windowTitle().contains(QStringLiteral("License Agreement")));
    }
    LanguageManager::instance().apply(arx::Language::English);
}

void TestUi::progressResetsWhenListEmpties()
{
    const QString a = m_dir.filePath(QStringLiteral("p1.png"));
    const QString b = m_dir.filePath(QStringLiteral("p2.png"));
    QVERIFY(testimg::make(8, 8, false).save(a, "png"));
    QVERIFY(testimg::make(8, 8, false).save(b, "png"));

    MainWindow w;
    auto *bar = w.findChild<QProgressBar *>(QStringLiteral("progressBar"));
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QCOMPARE(bar->text(), QStringLiteral("0 / 0"));  // at startup

    // Clear List
    w.addFiles({a, b});
    w.findChild<QPushButton *>(QStringLiteral("clearButton"))->click();
    QCOMPARE(list->topLevelItemCount(), 0);
    QCOMPARE(bar->text(), QStringLiteral("0 / 0"));

    // Remove every item with "Remove"
    w.addFiles({a, b});
    bar->setRange(0, 2);  // simulate a finished run showing "2 / 2"
    bar->setValue(2);
    bar->setFormat(QStringLiteral("%v / %m"));
    list->selectAll();
    w.findChild<QPushButton *>(QStringLiteral("removeButton"))->click();
    QCOMPARE(list->topLevelItemCount(), 0);
    QCOMPARE(bar->text(), QStringLiteral("0 / 0"));
}

// Available screen areas in logical pixels (taskbar 48 physical px removed):
// 1920x1080 and 1366x768 at the display scales the v1.1 plan requires.
void TestUi::windowFitsCommonScreens_data()
{
    QTest::addColumn<QRect>("available");
    QTest::newRow("1920x1080@100") << QRect(0, 0, 1920, 1032);
    QTest::newRow("1920x1080@125") << QRect(0, 0, 1536, 826);
    QTest::newRow("1920x1080@150") << QRect(0, 0, 1280, 688);
    QTest::newRow("1366x768@100") << QRect(0, 0, 1366, 720);
    QTest::newRow("1366x768@125") << QRect(0, 0, 1093, 576);
}

// Layout sizes depend on the real Windows font metrics; the offscreen
// platform measures text differently. These tests run in the separate
// "tst_ui_layout" CTest entry on the windows platform.
#define REQUIRE_REAL_PLATFORM()                                                        \
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))                 \
        QSKIP("layout metrics need the windows platform (see tst_ui_layout)")

void TestUi::windowFitsCommonScreens()
{
    REQUIRE_REAL_PLATFORM();
    QFETCH(QRect, available);
    MainWindow w;
    const QSize minimum = w.minimumSizeHint();
    const QRect r = arx::fitWindowRect(QSize(1100, 720), minimum, available);
    QVERIFY2(arx::fitsInside(r, available), qPrintable(QStringLiteral("%1x%2 at %3,%4")
                                                          .arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y())));
    QVERIFY(r.width() >= minimum.width() && r.height() >= minimum.height());
    QVERIFY(r.width() <= 1100 && r.height() <= 720);
}

void TestUi::minimumSizeWithinBudgetAllLanguages()
{
    REQUIRE_REAL_PLATFORM();
    // v1.1 budget: framed minimum window <= 620 x 500 logical px in every language.
    const QSize budget(620, 500);
    const QMargins f = arx::kEstimatedFrame;
    for (auto lang : {arx::Language::English, arx::Language::TraditionalChinese, arx::Language::Japanese}) {
        LanguageManager::instance().apply(lang);
        MainWindow w;
        w.show();
        QTest::qWait(50);
        const QSize framed = w.minimumSizeHint().grownBy(f);
        QVERIFY2(framed.width() <= budget.width() && framed.height() <= budget.height(),
                 qPrintable(QStringLiteral("%1: %2x%3 (list min %4)").arg(arx::languageCode(lang))
                                .arg(framed.width()).arg(framed.height())
                                .arg(w.findChild<QTreeWidget *>(QStringLiteral("fileList"))->minimumWidth())));

        // The two-line empty-list hint is never wrapped or cut, even at the minimum size.
        w.resize(w.minimumSizeHint());
        QTest::qWait(50);
        QLabel *hint = nullptr;
        for (auto *l : w.findChild<QTreeWidget *>(QStringLiteral("fileList"))->viewport()->findChildren<QLabel *>())
            if (l->isVisible())
                hint = l;
        QVERIFY(hint);
        QCOMPARE(hint->text().count(QLatin1Char('\n')), 1);
        QVERIFY(!hint->wordWrap());
        QVERIFY2(hint->sizeHint().width() <= hint->width(),
                 qPrintable(QStringLiteral("%1: hint %2 > %3").arg(arx::languageCode(lang))
                                .arg(hint->sizeHint().width()).arg(hint->width())));
    }
    LanguageManager::instance().apply(arx::Language::English);
}

void TestUi::windowGeometryRestoresAndFallsBack()
{
    REQUIRE_REAL_PLATFORM();
    {
        MainWindow w;
        w.show();
        w.resize(640, 480);
        QTest::qWait(50);
        QVERIFY(w.close());  // closeEvent stores the geometry
    }
    QVERIFY(!arx::AppSettings().windowGeometry().isEmpty());
    {
        MainWindow w;
        QCOMPARE(w.size(), QSize(640, 480));
    }

    // Damaged data: the window is fitted to the screen instead.
    {
        arx::AppSettings s;
        s.setWindowGeometry(QByteArray("not a geometry"));
        QVERIFY(s.sync());
    }
    {
        MainWindow w;
        QVERIFY(arx::fitsInside(w.geometry(), QGuiApplication::primaryScreen()->availableGeometry()));
    }
    arx::AppSettings s;
    s.setWindowGeometry({});
    s.setSplitterState({});
    QVERIFY(s.sync());
}

void TestUi::statusMessagesStaySingleLine()
{
    MainWindow w;
    w.show();
    auto *status = w.findChild<ElidedLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(status);
    const int before = w.minimumSizeHint().height();
    const QString longText = QStringLiteral("very long status message ").repeated(40) + QStringLiteral("\nsecond line");
    status->setText(longText);
    QTest::qWait(20);
    QCOMPARE(w.minimumSizeHint().height(), before);
    QCOMPARE(status->text(), longText);               // full text kept
    QVERIFY(status->toolTip().contains(QStringLiteral("second line")));
}

void TestUi::detailColumnFollowsListWidth()
{
    REQUIRE_REAL_PLATFORM();
    MainWindow w;
    w.show();
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    for (int width : {700, 1100}) {
        w.resize(width, 600);
        QTest::qWait(50);
        const int view = list->viewport()->width();
        // The name column keeps at least 40% of the list.
        QVERIFY2(list->header()->sectionSize(0) >= view * 2 / 5,
                 qPrintable(QStringLiteral("name %1 of %2").arg(list->header()->sectionSize(0)).arg(view)));
        if (width == 1100)  // the default window size shows all three columns
            QVERIFY2(!list->isColumnHidden(2),
                     qPrintable(QStringLiteral("view %1 name %2 status %3 ch %4")
                                    .arg(view).arg(list->header()->sectionSize(0))
                                    .arg(list->header()->sectionSize(1))
                                    .arg(list->fontMetrics().averageCharWidth())));
    }
}

void TestUi::themeFollowsColorScheme()
{
    REQUIRE_REAL_PLATFORM();
    auto *hints = QGuiApplication::styleHints();
    MainWindow w;
    w.show();
    for (auto scheme : {Qt::ColorScheme::Dark, Qt::ColorScheme::Light}) {
        hints->setColorScheme(scheme);  // what Windows does when the user switches
        const auto expected = scheme == Qt::ColorScheme::Dark ? theme::Mode::Dark : theme::Mode::Light;
        QTRY_COMPARE(theme::currentMode(), expected);
        QCOMPARE(qApp->palette().color(QPalette::Window), theme::palette(expected).color(QPalette::Window));
        QTRY_COMPARE(w.palette().color(QPalette::Window), theme::palette(expected).color(QPalette::Window));
    }
    hints->unsetColorScheme();
    QTRY_COMPARE(theme::currentMode(), theme::systemMode());
}

void TestUi::listShowsThumbnails()
{
    const QString a = m_dir.filePath(QStringLiteral("thumb 縮圖.png"));
    QVERIFY(testimg::make(300, 120, true).save(a, "png"));
    MainWindow w;
    w.show();
    QCOMPARE(w.addFiles({a}), 1);
    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QTreeWidgetItem *item = list->topLevelItem(0);
    // The decoded thumbnail replaces the blank placeholder (not fully transparent).
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const QImage img = item->icon(0).pixmap(list->iconSize()).toImage();
        const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
        return center.alpha() > 0;
    }(), 5000);
    QVERIFY(list->iconSize().height() >= 32);
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
