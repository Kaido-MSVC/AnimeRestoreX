#pragma once

#include "core/BatchRunner.h"
#include "core/JobSpec.h"

#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <memory>

namespace Ui { class MainWindow; }
namespace arx { class AppSettings; }
class JobWorker;
class QAction;
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
    void applySettings(int noise, int tile, bool languageChanged, int language);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
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
    void removeSelected();
    void clearList();
    void start();
    void onItemStarted(int index);
    void onItemFinished(int index, const arx::ItemResult &r);
    void onJobFinished(const arx::BatchSummary &summary);
    void onOutputSpecChanged();
    void updatePreview();

    void setItemState(QTreeWidgetItem *item, ItemState state, const QString &detail = {});
    void refreshItemTexts(QTreeWidgetItem *item);
    static QString pathKey(const QString &path);

    std::unique_ptr<Ui::MainWindow> ui;
    std::unique_ptr<arx::AppSettings> m_settings;
    QAction *m_settingsAction = nullptr;
    QThread m_thread;
    JobWorker *m_worker = nullptr;
    QThreadPool m_scanPool;
    bool m_running = false;
    bool m_scanning = false;
    bool m_closing = false;
    int m_finishedCount = 0;
    QString m_jobOutputDir;
    QList<QTreeWidgetItem *> m_jobItems;  // items of the running job, by job index
    QSet<QString> m_keys;
    QString m_sourceDir;
};
