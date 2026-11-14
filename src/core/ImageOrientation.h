#pragma once

#include "core/ImageMetadata.h"

#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QSize>
#include <QString>

// Reading an image upright (v2.1 P5). Qt 6.12 applies the EXIF orientation of JPEG files
// only; PNG (eXIf) and WebP (EXIF) are decoded as stored. nunif turns every format upright
// with Pillow ImageOps.exif_transpose (nunif/utils/pil_io.py), so PNG and WebP get the same
// treatment here: the Orientation is read with the P3 EXIF parser and applied after decoding.
//
// The loader of the job, the preview and the list thumbnails all read through OrientedReader,
// so the three always show the same orientation.
namespace arx::orientation {

// EXIF Orientation 1-8 -> Qt transformation, the mapping Pillow's exif_transpose uses:
// 2 FLIP_LEFT_RIGHT, 3 ROTATE_180, 4 FLIP_TOP_BOTTOM, 5 TRANSPOSE, 6 ROTATE_270, 7 TRANSVERSE,
// 8 ROTATE_90 (Pillow rotates counter-clockwise). Anything else: none.
QImageIOHandler::Transformations fromExif(int orientation);

// Applies `t` the way QImageReader's autoTransform does: mirror (horizontal) and / or flip
// (vertical) first, then rotate 90 degrees clockwise. Exact pixel moves, alpha included.
QImage apply(const QImage &image, QImageIOHandler::Transformations t);

// Size after `t` (width and height swapped when it rotates by 90 degrees).
QSize applied(QSize stored, QImageIOHandler::Transformations t);

} // namespace arx::orientation

namespace arx {

class OrientedReader {
public:
    // `meta` (optional) is the metadata already read from `path`; without it, PNG / WebP
    // files are read for their EXIF here.
    explicit OrientedReader(const QString &path, const metadata::SourceInfo *meta = nullptr);
    OrientedReader(const OrientedReader &) = delete;
    OrientedReader &operator=(const OrientedReader &) = delete;

    // The underlying reader (format, imageCount, errorString, ...). Do not change its
    // autoTransform or scaled size directly.
    QImageReader &reader() { return m_reader; }

    // Header size as stored in the file, before the orientation (invalid if unknown).
    QSize storedSize() const { return m_stored; }
    // Header size of the upright image.
    QSize size() const { return orientation::applied(m_stored, m_transform); }
    // The transformation that read() applies (by Qt for JPEG, here for PNG / WebP).
    QImageIOHandler::Transformations transformation() const { return m_transform; }

    // Decode scaled down to fit a side x side box (the box is square, so the stored and the
    // upright orientation give the same scale).
    void scaleToFit(int side);

    // Decodes and returns the upright image (null on failure, see reader().errorString()).
    QImage read();

private:
    QImageReader m_reader;
    QSize m_stored;
    QImageIOHandler::Transformations m_transform = QImageIOHandler::TransformationNone;
    bool m_applyHere = false;  // PNG / WebP: Qt does not apply the orientation
};

} // namespace arx
