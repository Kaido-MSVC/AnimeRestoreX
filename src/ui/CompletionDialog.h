#pragma once

#include "core/BatchRunner.h"

#include <QDialog>
#include <QList>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Shown at the end of every batch (a single image is a batch of one): the counts, then
// every item grouped as succeeded / failed / not processed. Items made with the smaller
// tile 160 are marked, because the tile size changes the result slightly.
class CompletionDialog : public QDialog {
    Q_OBJECT
public:
    struct Item {
        enum Kind { Succeeded, Failed, NotProcessed };
        Kind kind = Succeeded;
        QString file;          // source file name
        QString detail;        // output file name, or the failure reason
        int smallerTile = 0;   // tile used if Auto had to use a smaller one, else 0
    };

    CompletionDialog(const arx::BatchSummary &summary, const QString &outputDir,
                     const QList<Item> &items, QWidget *parent = nullptr);

    QString summaryText() const;
    // The per-item list as shown ("" if there are no items).
    QString itemsText() const;
    QPushButton *openFolderButton() const { return m_openButton; }

    // Opens the folder in Explorer. Returns false (and warns) if it is gone.
    static bool openFolder(QWidget *parent, const QString &dir);

private:
    QLabel *m_summary = nullptr;
    QPlainTextEdit *m_items = nullptr;
    QPushButton *m_openButton = nullptr;
    QString m_outputDir;
};
