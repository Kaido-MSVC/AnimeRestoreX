#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// The end-user licence agreement (EULA.txt, embedded as :/legal/EULA.txt; Appendix A is the
// Microsoft Windows ML Runtime licence). The interface text is translated; the agreement
// itself is shown in its original English.
//
//  - Consent: first start, or the agreement version changed. "I Agree" is enabled only
//    after the check box is ticked; "I Do Not Agree", Esc and the close button reject.
//  - View: read-only, opened from the About dialog.
class EulaDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Consent, View };

    explicit EulaDialog(Mode mode, QWidget *parent = nullptr);

    static QString currentVersion();   // ARX_EULA_VERSION, e.g. "2.0.0-1"
    static QString agreementText();    // contents of :/legal/EULA.txt (empty if missing)
    // True when `accepted` (stored in the settings) is the current version.
    static bool isAccepted(const QString &accepted);
    // Shown when the accepted version could not be written to the settings.
    static QString saveFailedMessage();

    Mode mode() const { return m_mode; }
    QCheckBox *acceptCheck() const { return m_accept; }
    QPushButton *agreeButton() const { return m_agree; }
    QPushButton *declineButton() const { return m_decline; }
    QPlainTextEdit *textView() const { return m_text; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    Mode m_mode;
    QLabel *m_intro = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QCheckBox *m_accept = nullptr;
    QPushButton *m_agree = nullptr;
    QPushButton *m_decline = nullptr;
    QPushButton *m_close = nullptr;
};
