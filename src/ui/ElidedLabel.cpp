#include "ui/ElidedLabel.h"

#include <QAction>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>

ElidedLabel::ElidedLabel(QWidget *parent) : QFrame(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_copy = new QAction(tr("Copy"), this);
    connect(m_copy, &QAction::triggered, this, [this] { QGuiApplication::clipboard()->setText(m_text); });
    addAction(m_copy);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    m_copy->setEnabled(false);
}

void ElidedLabel::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    m_copy->setEnabled(!text.isEmpty());
    setToolTip(text.isEmpty() ? QString()
                              : QStringLiteral("<p style='white-space:pre-wrap'>%1</p>").arg(text.toHtmlEscaped()));
    updateGeometry();
    update();
}

QString ElidedLabel::displayText() const
{
    QString t = m_text;
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return t;
}

QSize ElidedLabel::sizeHint() const
{
    const QMargins m = contentsMargins();
    return {fontMetrics().horizontalAdvance(displayText()) + m.left() + m.right(),
            fontMetrics().height() + m.top() + m.bottom()};
}

QSize ElidedLabel::minimumSizeHint() const
{
    const QMargins m = contentsMargins();
    return {0, fontMetrics().height() + m.top() + m.bottom()};
}

void ElidedLabel::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    if (m_text.isEmpty())
        return;
    QPainter p(this);
    p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
    const QRect r = contentsRect();
    p.drawText(r, Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine,
               fontMetrics().elidedText(displayText(), Qt::ElideRight, r.width()));
}

void ElidedLabel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        m_copy->setText(tr("Copy"));
    else if (event->type() == QEvent::FontChange)
        updateGeometry();
    QFrame::changeEvent(event);
}
