#pragma once

#include "core/JobSpec.h"

#include <QMainWindow>
#include <QThread>

#include <memory>

namespace Ui { class MainWindow; }
class JobWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void populateCombos();
    void updateQualityControls();
    void setRunning(bool running);
    arx::JobSpec currentSpec() const;
    void browseInput();
    void browseOutput();
    void start();

    std::unique_ptr<Ui::MainWindow> ui;
    QThread m_thread;
    JobWorker *m_worker = nullptr;
    bool m_running = false;
    QStringList m_files;
};
