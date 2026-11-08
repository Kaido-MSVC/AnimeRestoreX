#include "ui/PreviewPane.h"

#include "core/ImageLimits.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionComboBox>
#include <QStylePainter>
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

// A combo box that elides its text ("Fit to win…") when it is narrower than
// its contents, instead of cutting the text off.
class ElidingComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

    // Preferred width: the longest item fits the text field as the style sheet draws it
    // (QComboBox's own hint does not include the style sheet padding, so the text was
    // elided even with room to spare). The minimum width stays small (narrow windows).
    QSize sizeHint() const override
    {
        QSize s = QComboBox::sizeHint();
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        const int chrome = opt.rect.width() - field.width();
        int longest = 0;
        for (int i = 0; i < count(); ++i)
            longest = std::max(longest, fontMetrics().horizontalAdvance(itemText(i)));
        s.setWidth(std::max(s.width(), longest + chrome + fontMetrics().averageCharWidth()));
        return s;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        const QRect text = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        opt.currentText = fontMetrics().elidedText(opt.currentText, Qt::ElideRight, text.width());
        p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }
};

} // namespace

// ---------------------------------------------------------------- ImageCanvas

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ImageCanvas::setImage(const QImage &image, QSize size)
{
    m_thumb = image;
    m_full = image.size() == size ? image : QImage();
    m_size = size;
    m_message.clear();
    updateGeometry();
    update();
}

void ImageCanvas::setFullImage(const QImage &full)
{
    if (!hasImage() || full.size() != m_size)
        return;
    m_full = full;
    update();
}

void ImageCanvas::setMessage(const QString &text)
{
    m_full = QImage();
    m_thumb = QImage();
    m_size = QSize();
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
    p.fillRect(event->rect(), palette().base());  // the card surface around the image

    if (!hasImage()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    if (m_fit) {
        const QRectF target = fittedRect();
        if (m_thumb.hasAlphaChannel())
            p.fillRect(target, checkerBrush());
        const QImage &src = (target.width() <= m_thumb.width() || m_full.isNull()) ? m_thumb : m_full;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(target, src);
        return;
    }

    // 100% of the logical (output) size: draw only the exposed region.
    const QRect exposed = event->rect().intersected(QRect(QPoint(0, 0), m_logical));
    if (exposed.isEmpty())
        return;
    // Until the full-size pixels arrive, the scaled-down image stands in (smoothed).
    const QImage &src = m_full.isNull() ? m_thumb : m_full;
    if (src.hasAlphaChannel())
        p.fillRect(exposed, checkerBrush());
    const double sx = double(src.width()) / m_logical.width();
    const double sy = double(src.height()) / m_logical.height();
    const QRectF source(exposed.x() * sx, exposed.y() * sy, exposed.width() * sx,
                        exposed.height() * sy);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_full.isNull());  // full: show real pixels
    p.drawImage(QRectF(exposed), src, source);
}

// ---------------------------------------------------------------- PreviewPane

PreviewPane::PreviewPane(QWidget *parent) : QWidget(parent)
{
    m_pool.setMaxThreadCount(2);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_modeLabel = new QLabel(this);
    m_mode = new ElidingComboBox(this);
    m_modeLabel->setBuddy(m_mode);
    // Full text when there is room; in a narrow window the combo may shrink
    // (text elided) instead of raising the window's minimum width.
    m_mode->setMinimumWidth(fontMetrics().averageCharWidth() * 8 + 32);
    // The preferred width follows the item texts, also after a language switch at run
    // time (the default policy keeps the width of the first language shown).
    m_mode->setSizeAdjustPolicy(QComboBox::AdjustToContents);

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
        area->setObjectName(QStringLiteral("previewArea"));  // styled as a card (Theme)
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
        case ViewState::TooLarge: {
            const QSize s = isResult ? m_resSize : m_srcSize;
            return tr("No preview: the image is too large (%1 x %2 = %3 MP > %4 MP preview limit).")
                .arg(s.width())
                .arg(s.height())
                .arg(arx::limits::megapixels(arx::limits::pixelCount(s)),
                     arx::limits::megapixels(isResult ? maxResultPixels() : maxSourcePixels()));
        }
        case ViewState::Loaded:
            return {};
        case ViewState::Empty:
            break;
        }
        if (!isResult)
            return m_hasItems ? tr("Select an image in the list.") : tr("No images added yet.");
        if (m_sourcePath.isEmpty())
            return tr("Results appear here after processing.");
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

qint64 PreviewPane::maxSourcePixels()
{
    return arx::limits::kMaxInputPixels;
}

qint64 PreviewPane::maxResultPixels()
{
    return arx::limits::kMaxOutputPixels;
}

PreviewPane::Loaded PreviewPane::loadForPreview(const QString &path, qint64 maxPixels, bool full)
{
    Loaded l;
    QImageReader reader(path);
    reader.setAutoTransform(true);

    // Header only: nothing is decoded before the size is known to be within the limit.
    const QSize stored = reader.size();  // as stored in the file, before its orientation
    const arx::limits::HeaderCheck check = arx::limits::checkHeader(stored, maxPixels);
    if (check == arx::limits::HeaderCheck::UnknownSize) {
        l.error = reader.canRead() ? tr("Unknown image size") : reader.errorString();
        return l;
    }
    const QSize size = (reader.transformation() & QImageIOHandler::TransformationRotate90)
                           ? stored.transposed()
                           : stored;
    l.size = size;
    if (check == arx::limits::HeaderCheck::TooLarge) {
        l.tooLarge = true;
        return l;
    }

    // Fit mode needs no more than the thumbnail: let the decoder scale down. The scaled
    // size applies before the orientation is applied (Qt 6.12), so it uses the stored size.
    l.isFull = full || (size.width() <= kThumbSide && size.height() <= kThumbSide);
    if (!l.isFull)
        reader.setScaledSize(stored.scaled(kThumbSide, kThumbSide, Qt::KeepAspectRatio));

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
    if (l.isFull && img.size() != size)  // header and data disagree: keep what was decoded
        l.size = img.size();
    l.image = std::move(img);
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
    m_pool.clear();  // drop requests that have not started; running ones are discarded later
    m_srcFullRequested = m_resFullRequested = false;
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
    ++m_generation;
    m_pool.clear();  // drop requests that have not started; running ones are discarded later
    m_srcFullRequested = m_resFullRequested = false;
    m_sourcePath = sourcePath;
    m_resultPath = resultPath;

    const bool full = m_mode->currentIndex() == 1;  // 100% mode needs the full-size pixels
    m_srcState = ViewState::Loading;
    requestLoad(sourcePath, false, full);
    m_srcFullRequested = full;
    if (resultPath.isEmpty()) {
        m_resState = ViewState::Empty;
    } else {
        m_resState = ViewState::Loading;
        requestLoad(resultPath, true, full);
        m_resFullRequested = full;
    }
    refreshMessages();
    applyMode();
}

void PreviewPane::requestLoad(const QString &path, bool isResult, bool full)
{
    const quint64 gen = m_generation.load();
    const qint64 maxPixels = isResult ? maxResultPixels() : maxSourcePixels();
    m_pool.start([this, gen, path, isResult, full, maxPixels] {
        if (gen != m_generation.load())
            return;  // the selection changed before this request started: do not decode
        Loaded l = loadForPreview(path, maxPixels, full);
        if (!l.image.isNull())
            ++m_decodes;
        QMetaObject::invokeMethod(this, [this, gen, path, isResult, l = std::move(l)] {
            if (gen != m_generation.load())
                return;  // selection changed meanwhile
            ImageCanvas *c = isResult ? m_res : m_src;
            ViewState &state = isResult ? m_resState : m_srcState;
            if (state == ViewState::Loaded) {
                // Full-size pixels for 100% mode of an image already shown scaled down.
                if (l.isFull && !l.image.isNull()) {
                    c->setFullImage(l.image);
                    emit imageShown(path, isResult);
                }
                return;
            }
            if (l.tooLarge) {
                state = ViewState::TooLarge;
                (isResult ? m_resSize : m_srcSize) = l.size;
            } else if (l.image.isNull()) {
                state = ViewState::Error;
                (isResult ? m_resError : m_srcError) = l.error;
            } else {
                state = ViewState::Loaded;
                c->setImage(l.image, l.size);
                emit imageShown(path, isResult);
            }
            refreshMessages();
            applyMode();
        }, Qt::QueuedConnection);
    });
}

void PreviewPane::applyMode()
{
    const bool fit = m_mode->currentIndex() != 1;
    // 100% mode: images shown scaled down get their full-size pixels (once per selection).
    if (!fit) {
        if (m_srcState == ViewState::Loaded && !m_src->hasFullImage() && !m_srcFullRequested) {
            m_srcFullRequested = true;
            requestLoad(m_sourcePath, false, true);
        }
        if (m_resState == ViewState::Loaded && !m_res->hasFullImage() && !m_resFullRequested) {
            m_resFullRequested = true;
            requestLoad(m_resultPath, true, true);
        }
    }
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
