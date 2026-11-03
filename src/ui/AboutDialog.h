#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

// "Help > About AnimeRestoreX": version, author, MIT scope, third-party credits.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);

    QString bodyText() const;
    // GPU (or "CPU") the engine reported in this session; empty = not used yet.
    void setDeviceName(const QString &device);
    QPushButton *openLicensesButton() const { return m_openLicenses; }
    QPushButton *aboutQtButton() const { return m_aboutQt; }

    // Both LICENSE and THIRD_PARTY_NOTICES.md exist in `dir`.
    static bool licenseFilesPresent(const QString &dir);
    // Opens `dir` in Explorer, or warns and returns false if the files are missing.
    static bool openLicenseFolder(QWidget *parent, const QString &dir);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    QPushButton *m_openLicenses = nullptr;
    QPushButton *m_aboutQt = nullptr;
    QPushButton *m_close = nullptr;
    QString m_device;
};
