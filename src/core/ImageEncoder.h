#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

class QIODevice;

namespace arx {

class ImageEncoder {
    Q_DECLARE_TR_FUNCTIONS(ImageEncoder)
public:
    static QByteArray formatName(OutputFormat f);   // "png" / "jpeg" / "webp"
    static QString extension(OutputFormat f);       // "png" / "jpg" / "webp"

    // Quality value actually handed to QImageWriter (-1 = writer default).
    // WebP: lossless -> 100 (Qt's handler switches to lossless at 100), lossy -> 0..99.
    static int effectiveQuality(const EncodeOptions &opt);

    // JPEG: composites transparent images onto white. Others: unchanged.
    static QImage prepare(const QImage &image, OutputFormat f);

    // Encodes into an already opened device. Returns false and sets *error on failure.
    static bool encode(const QImage &image, const EncodeOptions &opt, QIODevice *device,
                       QString *error);

    // What the job adds to a plain encode (v2.1 P3). All empty / false = the plain encode.
    struct Extras {
        // The pixels already hold gray values (R = G = B): PNG and JPEG without alpha are
        // written as one-channel grayscale; PNG with alpha and WebP stay RGB(A).
        bool grayscale = false;
        QByteArray icc;   // embedded as is
        QByteArray exif;  // TIFF stream, embedded as is
    };
    // *metadataError is set (and the file written without ICC / EXIF) when the encoded file
    // cannot take them; only encoding failures return false.
    static bool encode(const QImage &image, const EncodeOptions &opt, const Extras &extras, QIODevice *device,
                       QString *error, QString *metadataError = nullptr);

    // Pillow Image.convert("L") (libImaging/Convert.c rgb2l / rgba2la, Pillow 13.0.0):
    // L = (R*19595 + G*38470 + B*7471 + 0x8000) >> 16, written into R, G and B; alpha unchanged.
    static void toGrayscale(uchar *pixels, int width, int height, int channels, qsizetype stride);
    static uchar luma(uchar r, uchar g, uchar b)
    {
        return uchar((r * 19595u + g * 38470u + b * 7471u + 0x8000u) >> 16);
    }
    // One-channel Grayscale8 copy of the R channel of an RGB888 image.
    static QImage grayPlane(const QImage &rgb888);
};

} // namespace arx
