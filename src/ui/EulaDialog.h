#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Microsoft license terms of the Windows ML Runtime package (license.txt of the pinned
// Microsoft.Windows.AI.MachineLearning package, embedded as :/legal/WindowsML-license.txt and
// shipped as licenses/WindowsML/license.txt). The interface text is translated; the terms are
// shown in their original English.
//
//  - Consent: first start, or the terms changed (their SHA-256 differs from the accepted one).
//    "I Agree" is enabled only after the check box is ticked; "I Do Not Agree", Esc and the
//    close button reject, and the program then ends.
//  - View: read-only, opened from the About dialog.
//
// Only builds on the Windows ML runtime have terms to accept (required() is false otherwise).
class EulaDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Consent, View };

    explicit EulaDialog(Mode mode, QWidget *parent = nullptr);

    static bool required();            // the runtime of this build comes with terms to accept
    static QString termsId();          // SHA-256 (hex) of the embedded license.txt; empty if none
    static QString agreementText();    // contents of the embedded license.txt (empty if none)
    // True when no consent is required, or `accepted` (stored in the settings) is termsId().
    static bool isAccepted(const QString &accepted);
    // Shown when the acceptance could not be written to the settings.
    static QString saveFailedMessage();

    Mode mode() const { return m_mode; }
    QLabel *introLabel() const { return m_intro; }
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
