#pragma once

#include "core/BatchRunner.h"
#include "core/JobSpec.h"
#include "core/Progress.h"

#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <memory>

namespace Ui { class MainWindow; }
namespace arx { class AppSettings; }
class ElidedLabel;
class JobWorker;
class QAction;
class QLabel;
class QMenu;
class QTimer;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    enum class ItemState { Pending, Processing, Done, Failed, NotProcessed };

    // Adds files to the list (skips duplicates / unsupported). Returns number added.
    int addFiles(const QStringList &files);

    // Start folder for the import dialogs (restored from settings).
    QString sourceFolder() const { return m_sourceDir; }

    // Applies values from the settings dialog (also used by tests).
    void applySettings(int tile, bool languageChanged, int language);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Files / folders dropped from Explorer are added like "Add Images / Add Folder".
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void scanAndAdd(const QStringList &dirs, const QStringList &files);
    void setDropHighlight(bool on);
    void updateDetailColumnWidth();
    int statusColumnWidth() const;      // preferred width of the Status column
    void updateEmptyHintWidth();
    void updateNoiseChoices();          // "None" is not offered with 1x
    QString processingProblem() const;  // empty if upscale / denoise can be run
    void onProcessingChanged();
    int pendingCount() const;           // items that Start would process
    void retranslate();
    void populateCombos();
    void updateQualityControls();
    void updateButtons();
    void setRunning(bool running);
    arx::JobSpec currentSpec() const;
    void restoreFolders();
    void saveSettings();

    void browseImages();
    void browseFolder();
    void browseOutput();
    void openSettings();
    void openAbout();
    void resetProgress();  // empty list: show "0 / 0"
    void removeSelected();
    void clearList();
    void start();
    void onItemStarted(int index);
    void onItemFinished(int index, const arx::ItemResult &r);
    void onJobFinished(const arx::BatchSummary &summary);
    void refreshProgress();  // estimated item / batch progress while running
    QString stageText(const arx::ProgressSnapshot &snap) const;
    QString remainingText(double seconds) const;
    void onOutputSpecChanged();
    void updatePreview();

    void setItemState(QTreeWidgetItem *item, ItemState state, const QString &detail = {});
    void refreshItemTexts(QTreeWidgetItem *item);
    static QString pathKey(const QString &path);

    std::unique_ptr<Ui::MainWindow> ui;
    std::unique_ptr<arx::AppSettings> m_settings;
    ElidedLabel *m_status = nullptr;
    QLabel *m_emptyHint = nullptr;
    QWidget *m_dropOverlay = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_removeAction = nullptr;
    QMenu *m_fileMenu = nullptr;
    QAction *m_addImagesAction = nullptr;
    QAction *m_addFolderAction = nullptr;
    QAction *m_exitAction = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QAction *m_settingsAction = nullptr;
    QMenu *m_helpMenu = nullptr;
    QAction *m_aboutAction = nullptr;
    QThread m_thread;
    JobWorker *m_worker = nullptr;
    QThreadPool m_scanPool;
    bool m_running = false;
    bool m_scanning = false;
    bool m_closing = false;
    int m_finishedCount = 0;
    arx::ProgressEstimator m_estimator;
    QElapsedTimer m_clock;
    QTimer *m_progressTimer = nullptr;
    QString m_device;       // GPU reported by the engine in this session
    QString m_ratesDevice;  // device whose measured speeds the estimator uses
    void onDeviceDetected(const QString &device);
    QString m_jobOutputDir;
    QList<QTreeWidgetItem *> m_jobItems;  // items of the running job, by job index
    QSet<QString> m_keys;
    QString m_sourceDir;
};
