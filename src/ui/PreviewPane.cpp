#include "ui/PreviewPane.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

constexpr int kThumbSide = 2048;

const QBrush &checkerBrush()
{
    static const QBrush brush = [] {
        QPixmap pm(16, 16);
        pm.fill(QColor(204, 204, 204));
        QPainter p(&pm);
        p.fillRect(0, 0, 8, 8, QColor(245, 245, 245));
        p.fillRect(8, 8, 8, 8, QColor(245, 245, 245));
        return QBrush(pm);
    }();
    return brush;
}

} // namespace

// ---------------------------------------------------------------- ImageCanvas

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ImageCanvas::setImage(const QImage &full, const QImage &thumb)
{
    m_full = full;
    m_thumb = thumb;
    m_message.clear();
    updateGeometry();
    update();
}

void ImageCanvas::setMessage(const QString &text)
{
    m_full = QImage();
    m_thumb = QImage();
    m_message = text;
    updateGeometry();
    update();
}

void ImageCanvas::setLogicalSize(QSize size)
{
    m_logical = size;
    updateGeometry();
    update();
}

void ImageCanvas::setFitMode(bool fit)
{
    m_fit = fit;
    updateGeometry();
    update();
}

QSize ImageCanvas::sizeHint() const
{
    if (!m_fit && hasImage() && m_logical.isValid())
        return m_logical;
    return {200, 150};
}

double ImageCanvas::fitScale() const
{
    if (!m_logical.isValid() || m_logical.isEmpty())
        return 1.0;
    const double sx = double(width()) / m_logical.width();
    const double sy = double(height()) / m_logical.height();
    return std::min({sx, sy, 1.0});
}

QRectF ImageCanvas::fittedRect() const
{
    const double z = fitScale();
    const QSizeF s(m_logical.width() * z, m_logical.height() * z);
    return {QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0), s};
}

void ImageCanvas::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(event->rect(), palette().window());

    if (!hasImage()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    if (m_fit) {
        const QRectF target = fittedRect();
        if (m_full.hasAlphaChannel())
            p.fillRect(target, checkerBrush());
        const QImage &src = (target.width() <= m_thumb.width()) ? m_thumb : m_full;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(target, src);
        return;
    }

    // 100% of the logical (output) size: draw only the exposed region.
    const QRect exposed = event->rect().intersected(QRect(QPoint(0, 0), m_logical));
    if (exposed.isEmpty())
        return;
    if (m_full.hasAlphaChannel())
        p.fillRect(exposed, checkerBrush());
    const double sx = double(m_full.width()) / m_logical.width();
    const double sy = double(m_full.height()) / m_logical.height();
    const QRectF source(exposed.x() * sx, exposed.y() * sy, exposed.width() * sx,
                        exposed.height() * sy);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);  // show real pixels
    p.drawImage(QRectF(exposed), m_full, source);
}

// ---------------------------------------------------------------- PreviewPane

PreviewPane::PreviewPane(QWidget *parent) : QWidget(parent)
{
    m_pool.setMaxThreadCount(2);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_modeLabel = new QLabel(this);
    m_mode = new QComboBox(this);
    m_modeLabel->setBuddy(m_mode);

    // Each column: bold title row above its view. The preview mode sits at
    // the right end of the Result title row (no extra row of its own).
    auto *views = new QHBoxLayout;
    auto makeColumn = [&](QLabel *&title, QScrollArea *&area, ImageCanvas *&canvas, bool withMode) {
        auto *col = new QVBoxLayout;
        auto *head = new QHBoxLayout;
        title = new QLabel(this);
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        head->addWidget(title, 1);
        if (withMode) {
            head->addWidget(m_modeLabel);
            head->addWidget(m_mode);
        }
        // Same title row height in both columns, so the two views line up.
        title->setMinimumHeight(m_mode->sizeHint().height());
        area = new QScrollArea(this);
        canvas = new ImageCanvas;
        area->setWidget(canvas);
        area->setAlignment(Qt::AlignCenter);
        area->setMinimumSize(fontMetrics().averageCharWidth() * 16, fontMetrics().lineSpacing() * 7);
        col->addLayout(head);
        col->addWidget(area, 1);
        views->addLayout(col, 1);
    };
    makeColumn(m_srcTitle, m_srcArea, m_src, false);
    makeColumn(m_resTitle, m_resArea, m_res, true);
    root->addLayout(views, 1);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    root->addWidget(m_note);

    // Keep both views scrolled to the same place in 100% mode.
    auto sync = [](QScrollBar *a, QScrollBar *b) {
        QObject::connect(a, &QScrollBar::valueChanged, b, &QScrollBar::setValue);
        QObject::connect(b, &QScrollBar::valueChanged, a, &QScrollBar::setValue);
    };
    sync(m_srcArea->horizontalScrollBar(), m_resArea->horizontalScrollBar());
    sync(m_srcArea->verticalScrollBar(), m_resArea->verticalScrollBar());

    retranslate();
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { applyMode(); });
    clear();
}

PreviewPane::~PreviewPane()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void PreviewPane::retranslate()
{
    const int mode = m_mode->currentIndex();
    {
        const QSignalBlocker block(m_mode);
        m_mode->clear();
        m_mode->addItem(tr("Fit to window"));
        m_mode->addItem(tr("100% (output pixels)"));
        m_mode->setCurrentIndex(mode < 0 ? 0 : mode);
    }
    m_modeLabel->setText(tr("Preview:"));
    refreshMessages();
    applyMode();
}

void PreviewPane::refreshMessages()
{
    auto message = [this](ViewState state, const QString &error, bool isResult) -> QString {
        switch (state) {
        case ViewState::Loading:
            return tr("Loading...");
        case ViewState::Error:
            return tr("Cannot load the preview: %1").arg(error);
        case ViewState::Loaded:
            return {};
        case ViewState::Empty:
            break;
        }
        if (!isResult)
            return m_hasItems ? tr("Select an image in the list.") : tr("No images added yet.");
        if (m_sourcePath.isEmpty())
            return {};
        switch (m_hint) {
        case ResultHint::NotYet:       return tr("Not processed yet.");
        case ResultHint::Processing:   return tr("Processing...");
        case ResultHint::Failed:       return tr("Processing failed. See the Details column.");
        case ResultHint::NotProcessed: return tr("Not processed.");
        }
        return {};
    };
    if (m_srcState != ViewState::Loaded)
        m_src->setMessage(message(m_srcState, m_srcError, false));
    if (m_resState != ViewState::Loaded)
        m_res->setMessage(message(m_resState, m_resError, true));
}

void PreviewPane::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(event);
}

void PreviewPane::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateNote();
}

PreviewPane::Loaded PreviewPane::loadForPreview(const QString &path)
{
    Loaded l;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        l.error = reader.errorString();
        return l;
    }
    img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                    : QImage::Format_RGB32);
    if (img.isNull()) {
        l.error = tr("Out of memory");
        return l;
    }
    if (img.width() > kThumbSide || img.height() > kThumbSide)
        l.thumb = img.scaled(kThumbSide, kThumbSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    else
        l.thumb = img;
    l.full = std::move(img);
    return l;
}

void PreviewPane::setHasItems(bool hasItems)
{
    if (hasItems == m_hasItems)
        return;
    m_hasItems = hasItems;
    refreshMessages();
}

void PreviewPane::clear()
{
    ++m_generation;
    m_sourcePath.clear();
    m_resultPath.clear();
    m_hint = ResultHint::NotYet;
    m_srcState = m_resState = ViewState::Empty;
    refreshMessages();
    applyMode();
}

void PreviewPane::showItem(const QString &sourcePath, const QString &resultPath, ResultHint hint)
{
    if (sourcePath == m_sourcePath && resultPath == m_resultPath) {
        if (hint != m_hint) {  // same images, only the message changes
            m_hint = hint;
            refreshMessages();
        }
        return;
    }
    m_hint = hint;
    const quint64 gen = ++m_generation;
    m_sourcePath = sourcePath;
    m_resultPath = resultPath;

    auto load = [this, gen](const QString &path, bool isResult) {
        m_pool.start([this, gen, path, isResult] {
            Loaded l = loadForPreview(path);
            QMetaObject::invokeMethod(this, [this, gen, isResult, l = std::move(l)] {
                if (gen != m_generation)
                    return;  // selection changed meanwhile
                ImageCanvas *c = isResult ? m_res : m_src;
                ViewState &state = isResult ? m_resState : m_srcState;
                if (l.full.isNull()) {
                    state = ViewState::Error;
                    (isResult ? m_resError : m_srcError) = l.error;
                } else {
                    state = ViewState::Loaded;
                    c->setImage(l.full, l.thumb);
                }
                refreshMessages();
                applyMode();
            }, Qt::QueuedConnection);
        });
    };

    m_srcState = ViewState::Loading;
    load(sourcePath, false);
    if (resultPath.isEmpty()) {
        m_resState = ViewState::Empty;
    } else {
        m_resState = ViewState::Loading;
        load(resultPath, true);
    }
    refreshMessages();
    applyMode();
}

void PreviewPane::applyMode()
{
    const bool fit = m_mode->currentIndex() != 1;
    const QSize logical = m_res->hasImage() ? m_res->imageSize() : m_src->imageSize();
    for (auto [area, canvas] : {std::pair{m_srcArea, m_src}, std::pair{m_resArea, m_res}}) {
        canvas->setLogicalSize(logical);
        canvas->setFitMode(fit);
        area->setWidgetResizable(fit || !canvas->hasImage());
        if (!fit && canvas->hasImage())
            canvas->resize(logical);
    }
    updateNote();
}

void PreviewPane::updateNote()
{
    const auto sizeText = [](QSize s) { return QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()); };

    m_srcTitle->setText(m_src->hasImage() ? tr("Original (%1)").arg(sizeText(m_src->imageSize()))
                                          : tr("Original"));
    m_resTitle->setText(m_res->hasImage() ? tr("Result (%1)").arg(sizeText(m_res->imageSize()))
                                          : tr("Result"));

    if (!m_src->hasImage() && !m_res->hasImage()) {
        m_note->clear();
        return;
    }

    QStringList parts;
    const bool fit = m_mode->currentIndex() != 1;
    const ImageCanvas *ref = m_res->hasImage() ? m_res : m_src;
    if (fit) {
        const double z = ref->fitScale();
        if (z < 0.999)
            parts << tr("Scaled-down preview (%1% of actual size); the output file is full size.")
                         .arg(qRound(z * 100));
        else
            parts << tr("Shown at actual size.");
    } else {
        parts << tr("Showing output pixels at 100%.");
    }
    if (m_res->hasImage() && m_src->hasImage() && m_src->imageSize() != m_res->imageSize())
        parts << tr("The original is enlarged to the output size for comparison only (not AI processed).");
    m_note->setText(parts.join(QLatin1Char(' ')));
}
