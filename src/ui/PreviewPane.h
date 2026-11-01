#pragma once

#include <QImage>
#include <QThreadPool>
#include <QWidget>

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

    void setImage(const QImage &full, const QImage &thumb);
    void setMessage(const QString &text);  // shown instead of an image
    void setLogicalSize(QSize size);       // size the image maps to in 100% mode
    void setFitMode(bool fit);
    bool hasImage() const { return !m_full.isNull(); }
    QSize imageSize() const { return m_full.size(); }
    double fitScale() const;               // displayed / logical in fit mode

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF fittedRect() const;

    QImage m_full;
    QImage m_thumb;  // <= 2048 px long side, used in fit mode
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

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Loaded {
        QImage full;
        QImage thumb;
        QString error;
    };
    enum class ViewState { Empty, Loading, Loaded, Error };
    static Loaded loadForPreview(const QString &path);
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
    quint64 m_generation = 0;
    QString m_sourcePath;
    QString m_resultPath;
    ResultHint m_hint = ResultHint::NotYet;
    ViewState m_srcState = ViewState::Empty;
    ViewState m_resState = ViewState::Empty;
    QString m_srcError;
    QString m_resError;
};
