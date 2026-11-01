// Widget-level tests (run with QT_QPA_PLATFORM=offscreen). The full-batch test
// drives the real main window and uses the waifu2x copy next to the test exe.
#include "TestImages.h"

#include "ui/AboutDialog.h"
#include "ui/CompletionDialog.h"
#include "ui/ElidedLabel.h"
#include "ui/LanguageManager.h"
#include "ui/WindowPlacement.h"
#include "ui/SettingsDialog.h"
#include "ui/MainWindow.h"
#include "ui/PreviewPane.h"

#include "core/AppSettings.h"

#include <QComboBox>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>

#include <functional>
#include <QApplication>
#include <QDir>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

class TestUi : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void dialogMixedCounts();
    void dialogAllFailed();
    void openMissingFolderWarns();
    void addFilesSkipsDuplicates();
    void fullBatchThroughMainWindow();
    void listElidesAndShowsFullReason();
    void folderMemoryRestoresIndependently();
    void autoLanguageIsNotSavedAsManual();
    void denoiseOnMainWindow();
    void oneXNeedsDenoise();
    void qualityShownOnlyWhenUsed();
    void startShowsPendingCountAndEmptyHint();
    void menusFollowWindowsLayout();
    void languageSwitchRetranslates();
    void windowIconFromResources();
    void aboutDialogContents();
    void aboutDialogLicenseFolder();
    void progressResetsWhenListEmpties();
    void windowFitsCommonScreens_data();
    void windowFitsCommonScreens();
    void minimumSizeWithinBudgetAllLanguages();
    void windowGeometryRestoresAndFallsBack();
    void statusMessagesStaySingleLine();
    void detailColumnFollowsListWidth();

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
    QVERIFY(m_dir.isValid());
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
    CompletionDialog dlg(s, m_dir.path(), {QStringLiteral("bad.png: Cannot decode")});
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Succeeded: 2")));
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Failed: 1")));
    QVERIFY(!dlg.summaryText().contains(QStringLiteral("Not processed")));
    QVERIFY(dlg.openFolderButton());
    QVERIFY(dlg.findChild<QPlainTextEdit *>());
}

void TestUi::dialogAllFailed()
{
    arx::BatchSummary s;
    s.failed = 1;
    s.notProcessed = 2;
    s.fatalError = QStringLiteral("No usable Vulkan GPU");
    CompletionDialog dlg(s, m_dir.path(), {QStringLiteral("a.png: GPU")});
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Succeeded: 0")));
    QVERIFY(dlg.summaryText().contains(QStringLiteral("Not processed: 2")));
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

void TestUi::fullBatchThroughMainWindow()
{
    const QString w2x = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("waifu2x/waifu2x-ncnn-vulkan.exe"));
    if (!QFileInfo::exists(w2x))
        QSKIP("waifu2x is not deployed next to the test executable");

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
    w.findChild<QPushButton *>(QStringLiteral("startButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!summary.isEmpty(), 120000);

    QVERIFY2(summary.contains(QStringLiteral("Succeeded: 2")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("Failed: 1")), qPrintable(summary));

    auto *list = w.findChild<QTreeWidget *>(QStringLiteral("fileList"));
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Done"));
    QCOMPARE(list->topLevelItem(1)->text(1), QStringLiteral("Failed"));
    QVERIFY(!list->topLevelItem(1)->text(2).isEmpty());
    QCOMPARE(list->topLevelItem(2)->text(1), QStringLiteral("Done"));
    auto *bar = w.findChild<QProgressBar *>(QStringLiteral("progressBar"));
    QCOMPARE(bar->value(), 3);
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("1_2x.png"))));
    QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("2_2x.png"))));
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

    const QString w2x = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("waifu2x/waifu2x-ncnn-vulkan.exe"));
    if (!QFileInfo::exists(w2x))
        QSKIP("waifu2x is not deployed next to the test executable");
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
    w.applySettings(200, false, int(arx::Language::TraditionalChinese));
    arx::AppSettings s;
    QVERIFY(!s.manualLanguage().has_value());
    QCOMPARE(s.tileSize(), 200);
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
    SettingsDialog sd(0, arx::Language::English, &w);
    QCOMPARE(sd.findChildren<QComboBox *>().size(), 2);  // tile + language

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

    scale->setCurrentIndex(scale->findData(1));
    noise->setCurrentIndex(noise->findData(1));
    QVERIFY(start->isEnabled());
    noise->setCurrentIndex(noise->findData(-1));  // 1x + no denoise does nothing
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

void TestUi::languageSwitchRetranslates()
{
    MainWindow w;
    auto *startButton = w.findChild<QPushButton *>(QStringLiteral("startButton"));
    QCOMPARE(startButton->text(), QStringLiteral("Start"));

    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_zh_TW.qm")), "embedded .qm missing");
    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_ja.qm")), "embedded .qm missing");

    // LanguageChange is delivered through the event loop.
    w.applySettings(0, true, int(arx::Language::TraditionalChinese));
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
    auto snapshot = [&](const QString &tag) {
        if (snapDir.isEmpty())
            return;
        w.resize(1100, 720);
        w.show();
        QTest::qWait(300);
        w.grab().save(QDir(snapDir).filePath(QStringLiteral("main_%1.png").arg(tag)));
        SettingsDialog sd(0, LanguageManager::instance().current(), &w);
        sd.show();
        QTest::qWait(200);
        sd.grab().save(QDir(snapDir).filePath(QStringLiteral("settings_%1.png").arg(tag)));
        AboutDialog ad(&w);
        ad.show();
        QTest::qWait(200);
        ad.grab().save(QDir(snapDir).filePath(QStringLiteral("about_%1.png").arg(tag)));
    };
    snapshot(QStringLiteral("zh_TW"));

    w.applySettings(0, true, int(arx::Language::Japanese));
    QTRY_COMPARE(startButton->text(), QStringLiteral("処理開始"));
    snapshot(QStringLiteral("ja"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::Japanese));

    w.applySettings(0, true, int(arx::Language::English));
    QTRY_COMPARE(startButton->text(), QStringLiteral("Start"));
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
    for (const QString &s : {QStringLiteral("AnimeRestoreX"), QStringLiteral("1.0.0"),
                             QStringLiteral("Kaido-MSVC"), QStringLiteral("MIT"),
                             QStringLiteral("waifu2x-ncnn-vulkan"), QStringLiteral("ncnn (Tencent)"),
                             QStringLiteral("nagadomi/waifu2x"), QStringLiteral("Qt 6.12"),
                             QStringLiteral("LGPL")})
        QVERIFY2(text.contains(s), qPrintable(s));
    QVERIFY(dlg.openLicensesButton());
    QVERIFY(dlg.aboutQtButton());

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
                 qPrintable(QStringLiteral("%1: %2x%3").arg(arx::languageCode(lang))
                                .arg(framed.width()).arg(framed.height())));
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
        // The name column keeps at least half of the list.
        QVERIFY2(list->header()->sectionSize(0) >= view / 2,
                 qPrintable(QStringLiteral("name %1 of %2").arg(list->header()->sectionSize(0)).arg(view)));
    }
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
