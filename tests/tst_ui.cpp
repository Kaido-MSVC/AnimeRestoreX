// Widget-level tests (run with QT_QPA_PLATFORM=offscreen). The full-batch test
// drives the real main window and uses the waifu2x copy next to the test exe.
#include "TestImages.h"

#include "ui/CompletionDialog.h"
#include "ui/LanguageManager.h"
#include "ui/SettingsDialog.h"
#include "ui/MainWindow.h"
#include "ui/PreviewPane.h"

#include "core/AppSettings.h"

#include <QComboBox>
#include <QHeaderView>
#include <QMenuBar>
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
    void languageSwitchRetranslates();

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
    scale->setCurrentIndex(1);  // 4x
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
        QVERIFY(w.findChild<QLabel *>(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("not available")));
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
    w.applySettings(2, 200, false, int(arx::Language::TraditionalChinese));
    arx::AppSettings s;
    QVERIFY(!s.manualLanguage().has_value());
    QCOMPARE(s.noiseLevel(), 2);
    QCOMPARE(s.tileSize(), 200);
}

void TestUi::languageSwitchRetranslates()
{
    MainWindow w;
    auto *startButton = w.findChild<QPushButton *>(QStringLiteral("startButton"));
    QCOMPARE(startButton->text(), QStringLiteral("Start"));

    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_zh_TW.qm")), "embedded .qm missing");
    QVERIFY2(QFile::exists(QStringLiteral(":/i18n/animerestorex_ja.qm")), "embedded .qm missing");

    // LanguageChange is delivered through the event loop.
    w.applySettings(1, 0, true, int(arx::Language::TraditionalChinese));
    QCOMPARE(LanguageManager::instance().current(), arx::Language::TraditionalChinese);
    QTRY_COMPARE(startButton->text(), QStringLiteral("開始處理"));
    QCOMPARE(w.menuBar()->actions().first()->text(), QStringLiteral("設定(&S)"));
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
        SettingsDialog sd(1, 0, LanguageManager::instance().current(), &w);
        sd.show();
        QTest::qWait(200);
        sd.grab().save(QDir(snapDir).filePath(QStringLiteral("settings_%1.png").arg(tag)));
    };
    snapshot(QStringLiteral("zh_TW"));

    w.applySettings(1, 0, true, int(arx::Language::Japanese));
    QTRY_COMPARE(startButton->text(), QStringLiteral("処理開始"));
    snapshot(QStringLiteral("ja"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::Japanese));

    w.applySettings(1, 0, true, int(arx::Language::English));
    QTRY_COMPARE(startButton->text(), QStringLiteral("Start"));
    QCOMPARE(arx::AppSettings().manualLanguage(), std::optional(arx::Language::English));
    QSettings().remove(QStringLiteral("ui/language"));
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
