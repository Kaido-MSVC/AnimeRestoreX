#pragma once

#include <QFrame>

class QAction;

// Single-line text that is elided at the right edge instead of wrapping, so
// long messages never change the window's minimum height. The full text is
// available as a tooltip and through the context menu ("Copy").
class ElidedLabel : public QFrame {
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);

    void setText(const QString &text);
    QString text() const { return m_text; }
    void clear() { setText(QString()); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QString displayText() const;

    QString m_text;
    QAction *m_copy = nullptr;
};
