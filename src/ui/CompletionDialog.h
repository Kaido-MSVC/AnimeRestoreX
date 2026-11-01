#pragma once

#include "core/BatchRunner.h"

#include <QDialog>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Shown at the end of every batch (a single image is a batch of one).
class CompletionDialog : public QDialog {
    Q_OBJECT
public:
    // failures: "file name: reason" lines, shown when non-empty.
    CompletionDialog(const arx::BatchSummary &summary, const QString &outputDir,
                     const QStringList &failures, QWidget *parent = nullptr);

    QString summaryText() const;
    QPushButton *openFolderButton() const { return m_openButton; }

    // Opens the folder in Explorer. Returns false (and warns) if it is gone.
    static bool openFolder(QWidget *parent, const QString &dir);

private:
    QLabel *m_summary = nullptr;
    QPushButton *m_openButton = nullptr;
    QString m_outputDir;
};
