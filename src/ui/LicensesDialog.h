#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// "About > Licenses and Notices": every licence and third-party notice shipped next to the
// executable, shown read-only. The list comes from the files themselves (LICENSE,
// THIRD_PARTY_NOTICES.md, licenses/...), so it cannot drift from what the package contains.
// Texts stay in their original language; only the interface around them is translated.
class LicensesDialog : public QDialog {
    Q_OBJECT
public:
    explicit LicensesDialog(const QString &appDir, QWidget *parent = nullptr);

    // Paths relative to `appDir`, '/' separated: LICENSE and THIRD_PARTY_NOTICES.md first, then
    // licenses/... sorted. Only files that exist; README.md and index.json are not licence texts.
    static QStringList licenseFiles(const QString &appDir);

    QTreeWidget *fileTree() const { return m_tree; }
    QPlainTextEdit *textView() const { return m_text; }
    QLabel *missingLabel() const { return m_missing; }
    QString currentFile() const;   // as in licenseFiles(); empty if none is selected
    bool selectFile(const QString &relativePath);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildTree();
    void showCurrent();
    void retranslate();
    QString componentName(const QString &folder) const;

    QString m_appDir;
    QLabel *m_intro = nullptr;
    QLabel *m_missing = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QPushButton *m_close = nullptr;
};
