#pragma once

#include <QImage>
#include <QThreadPool>
#include <QWidget>

#include <atomic>

class QComboBox;
class QLabel;
class QScrollArea;

// Paints one image either fitted into the viewport or at a fixed "logical"
// size (the output resolution), so that the original and the result line up
// pixel for pixel. Only the exposed region is drawn in 100% mode.
class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // `image` is the decoded picture, either at full size or scaled down at decode
    // time; `size` is the size of the file's image (after its orientation).
    void setImage(const QImage &image, QSize size);
    // Full-size pixels for 100% mode, added later to an image shown scaled down.
    void setFullImage(const QImage &full);
    void setMessage(const QString &text);  // shown instead of an image
    void setLogicalSize(QSize size);       // size the image maps to in 100% mode
    void setFitMode(bool fit);
    bool hasImage() const { return !m_thumb.isNull(); }
    bool hasFullImage() const { return !m_full.isNull(); }
    QSize imageSize() const { return m_size; }
    QSize decodedSize() const { return m_thumb.size(); }  // pixels used in fit mode
    QString message() const { return m_message; }
    double fitScale() const;               // displayed / logical in fit mode

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF fittedRect() const;

    QImage m_full;   // full size; null until needed (100% mode) for a large image
    QImage m_thumb;  // <= 2048 px long side, used in fit mode
    QSize m_size;    // full size of the image
    QString m_message;
    QSize m_logical;
    bool m_fit = true;
};

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget *parent = nullptr);
    ~PreviewPane() override;

    // What to say in the result view when there is no result file.
    enum class ResultHint { NotYet, Processing, Failed, NotProcessed };

    // resultPath may be empty; then `hint` decides the result-view message.
    void showItem(const QString &sourcePath, const QString &resultPath,
                  ResultHint hint = ResultHint::NotYet);
    void clear();
    void retranslate();
    // Whether the file list has items (changes the empty-preview message).
    void setHasItems(bool hasItems);

    // Size limits of the preview, checked from the file header before decoding.
    // The original follows the processing limit; a result can be as large as an output.
    static qint64 maxSourcePixels();
    static qint64 maxResultPixels();
    // Images decoded so far (stale requests are skipped before decoding); for tests.
    int decodeCount() const { return m_decodes.load(); }

signals:
    // A view received an image of the current selection (also after a 100% reload).
    void imageShown(const QString &path, bool isResult);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Loaded {
        QImage image;       // full size, or scaled down to the thumbnail size
        QSize size;         // full size (after orientation); set also when too large
        bool isFull = false;
        bool tooLarge = false;
        QString error;
    };
    enum class ViewState { Empty, Loading, Loaded, Error, TooLarge };
    // Reads the header, rejects images over `maxPixels` without decoding them, and
    // decodes either at full size or scaled down to the thumbnail size.
    static Loaded loadForPreview(const QString &path, qint64 maxPixels, bool full);
    // Queues a decode of `path` for one view; skipped if the selection changed first.
    void requestLoad(const QString &path, bool isResult, bool full);
    void applyMode();
    void updateNote();
    void refreshMessages();  // re-creates translated messages without reloading images

    QComboBox *m_mode = nullptr;
    QLabel *m_srcTitle = nullptr;
    QLabel *m_resTitle = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_modeLabel = nullptr;
    QScrollArea *m_srcArea = nullptr;
    QScrollArea *m_resArea = nullptr;
    ImageCanvas *m_src = nullptr;
    ImageCanvas *m_res = nullptr;

    QThreadPool m_pool;
    // Bumped by every selection change; read by the workers before they decode.
    std::atomic<quint64> m_generation{0};
    std::atomic_int m_decodes{0};
    bool m_srcFullRequested = false;
    bool m_resFullRequested = false;
    QSize m_srcSize;  // header size when an image is too large to preview
    QSize m_resSize;
    QString m_sourcePath;
    QString m_resultPath;
    ResultHint m_hint = ResultHint::NotYet;
    bool m_hasItems = false;
    ViewState m_srcState = ViewState::Empty;
    ViewState m_resState = ViewState::Empty;
    QString m_srcError;
    QString m_resError;
};
