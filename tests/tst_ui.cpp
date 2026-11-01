// Widget-level tests (run with QT_QPA_PLATFORM=offscreen). The full-batch test
// drives the real main window and uses the waifu2x copy next to the test exe.
#include "TestImages.h"

#include "ui/CompletionDialog.h"
#include "ui/MainWindow.h"
#include "ui/PreviewPane.h"

#include <QComboBox>
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

private:
    // Closes the next modal widget (message box / dialog) and returns its text.
    void autoCloseNextModal(QString *captured);
    QTemporaryDir m_dir;
};

void TestUi::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
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

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
