#pragma once

#include "core/JobSpec.h"

#include <QByteArray>
#include <QString>

#include <expected>

class QIODevice;

// Metadata that AnimeRestoreX carries from the source file to the output file (v2.1 P3):
// the embedded ICC profile, EXIF, and whether the source is a grayscale image.
// Only reads the container structure (PNG chunks, JPEG segments, WebP RIFF chunks); the
// pixels are decoded by Qt. No third-party metadata library is used.
namespace arx::metadata {

struct SourceInfo {
    // Embedded ICC profile as stored in the file (PNG iCCP decompressed; JPEG APP2 parts
    // joined). Empty if there is none or it could not be read.
    QByteArray icc;
    // TIFF stream of the EXIF block ("II*\0..." / "MM\0*..."), without "Exif\0\0".
    QByteArray exif;
    // Why the EXIF block could not be read from the container (empty if fine or absent).
    QString exifError;
    // nunif pil_io._load_image (0d3d92c) keeps an image grayscale when Pillow opens it as
    // L / LA (with tRNS: LA): PNG colour type 0 or 4 with bit depth >= 2, one-component JPEG.
    // Palette PNGs ("P") and 1-bit PNGs ("1") are converted to RGB there, so they are not.
    bool grayscale = false;
};

// Reads the metadata of a PNG, JPEG or WebP file from the start of `dev` (left open).
SourceInfo read(QIODevice &dev);
SourceInfo read(const QString &path);

// The data colour space of an ICC profile (header bytes 16-19): "RGB ", "GRAY", "CMYK", ...
QByteArray iccColorSpace(const QByteArray &icc);

// EXIF Orientation (IFD0 tag 0x0112, one SHORT or LONG value) of a TIFF stream: 1-8.
// Returns 1 (as stored, nothing to turn) when there is no tag, the value is not 1-8, or the
// header / IFD0 cannot be read.
int exifOrientation(const QByteArray &tiff);

// EXIF for the output: Orientation set to 1 (all formats are loaded upright, v2.1 P5),
// PixelXDimension / PixelYDimension set to the output size if present, the thumbnail
// (IFD1) removed. Everything else is kept byte for byte.
// Fails with a reason if the TIFF structure is damaged; the caller writes no EXIF then.
std::expected<QByteArray, QString> prepareExif(const QByteArray &tiff, int width, int height);

// Largest EXIF block that fits into one JPEG APP1 segment.
inline constexpr qsizetype kMaxJpegExif = 65535 - 2 - 6;

// Inserts the ICC profile and / or EXIF block into an encoded file (Qt writes neither):
//  - PNG: iCCP and eXIf right after IHDR (before IDAT);
//  - JPEG: APP1 "Exif" right after SOI, APP2 "ICC_PROFILE" segments after APP0 (JFIF);
//  - WebP: extended format (VP8X with the ICC / EXIF flags), ICCP right after VP8X,
//    EXIF after the image data, as the WebP container specification orders them.
// Returns the new file contents, or a reason when the input is not what the Qt writers
// produce or a block does not fit.
std::expected<QByteArray, QString> embed(const QByteArray &encoded, OutputFormat format, const QByteArray &icc,
                                         const QByteArray &exif);

// CRC-32 (PNG / zlib polynomial), exposed for tests.
quint32 crc32(const char *data, qsizetype size, quint32 crc = 0);

} // namespace arx::metadata
