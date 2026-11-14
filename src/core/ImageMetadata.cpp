#include "core/ImageMetadata.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include <vector>

namespace arx::metadata {

namespace {

constexpr qsizetype kMaxIcc = 32 * 1024 * 1024;   // larger profiles are not read
constexpr qsizetype kMaxExif = 4 * 1024 * 1024;   // larger EXIF blocks are not read
const QByteArray kPngSig("\x89PNG\r\n\x1a\n", 8);
const QByteArray kExifHeader("Exif\0\0", 6);
const QByteArray kIccHeader("ICC_PROFILE\0", 12);

QByteArray stripExifHeader(QByteArray d)
{
    // "Exif\0\0" is the JPEG APP1 identifier; some writers also put it into PNG / WebP.
    if (d.size() >= 6 && d.startsWith(QByteArray("Exif\0", 5)))
        d.remove(0, 6);
    return d;
}

bool readExact(QIODevice &dev, char *buf, qint64 n) { return dev.read(buf, n) == n; }

bool skip(QIODevice &dev, qint64 n)
{
    if (n == 0)
        return true;
    if (!dev.isSequential())
        return dev.seek(dev.pos() + n) && dev.pos() <= dev.size();
    return dev.skip(n) == n;
}

// zlib stream (PNG iCCP) -> bytes. qUncompress takes the expected size as a 4-byte prefix;
// the hint is raised until the profile fits.
QByteArray inflate(const QByteArray &z)
{
    for (qsizetype hint = std::max<qsizetype>(4096, z.size() * 4); hint <= kMaxIcc; hint *= 4) {
        QByteArray in(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(hint), in.data());
        in += z;
        QByteArray out = qUncompress(in);
        if (!out.isEmpty())
            return out;
    }
    return {};
}

void readPng(QIODevice &dev, SourceInfo &info)
{
    char hdr[8];
    for (int guard = 0; guard < 1'000'000; ++guard) {
        if (!readExact(dev, hdr, 8))
            return;
        const quint32 len = qFromBigEndian<quint32>(hdr);
        const QByteArray type(hdr + 4, 4);
        if (len > 0x7FFFFFFFu)
            return;
        const bool want = type == "IHDR" || (type == "iCCP" && info.icc.isEmpty()) ||
                          (type == "eXIf" && info.exif.isEmpty() && info.exifError.isEmpty());
        if (want) {
            if ((type == "iCCP" && len > kMaxIcc) || (type == "eXIf" && len > kMaxExif)) {
                if (type == "eXIf")
                    info.exifError = QStringLiteral("eXIf chunk too large (%1 bytes)").arg(len);
                if (!skip(dev, qint64(len) + 4))
                    return;
                continue;
            }
            QByteArray d(qsizetype(len), Qt::Uninitialized);
            if (!readExact(dev, d.data(), len)) {
                if (type == "eXIf")
                    info.exifError = QStringLiteral("eXIf chunk is truncated");
                return;
            }
            if (!skip(dev, 4))  // CRC
                return;
            if (type == "IHDR" && len == 13) {
                const int depth = quint8(d[8]), colourType = quint8(d[9]);
                // Pillow: colour type 0 -> "1" (1 bit) or "L"; 4 -> "LA". nunif keeps L / LA.
                info.grayscale = (colourType == 0 || colourType == 4) && depth >= 2;
            } else if (type == "iCCP") {
                const qsizetype nul = d.indexOf('\0');
                if (nul > 0 && nul < 80 && nul + 2 <= d.size() && d[nul + 1] == 0)
                    info.icc = inflate(d.mid(nul + 2));
            } else if (type == "eXIf") {
                info.exif = stripExifHeader(d);
            }
            continue;
        }
        if (type == "IEND")
            return;
        if (!skip(dev, qint64(len) + 4))
            return;
    }
}

void readJpeg(QIODevice &dev, SourceInfo &info)
{
    std::map<int, QByteArray> iccParts;
    int iccCount = 0;
    bool iccBad = false;
    for (int guard = 0; guard < 100'000; ++guard) {
        char c = 0;
        if (!dev.getChar(&c))
            break;
        if (quint8(c) != 0xFF)
            continue;  // not at a marker: resynchronise
        quint8 m = 0xFF;
        while (m == 0xFF) {
            if (!dev.getChar(&c))
                return;
            m = quint8(c);
        }
        if (m == 0xD9 || m == 0xDA)  // EOI / SOS: the metadata segments come before
            break;
        if (m == 0x01 || m == 0x00 || (m >= 0xD0 && m <= 0xD8))
            continue;  // no length
        char lb[2];
        if (!readExact(dev, lb, 2))
            break;
        const int len = qFromBigEndian<quint16>(lb);
        if (len < 2)
            break;
        const int n = len - 2;
        const bool sof = m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
        if (m == 0xE1 || m == 0xE2 || sof) {
            QByteArray d(n, Qt::Uninitialized);
            if (!readExact(dev, d.data(), n)) {
                if (m == 0xE1 && info.exif.isEmpty())
                    info.exifError = QStringLiteral("APP1 segment is truncated");
                break;
            }
            if (sof && n >= 6) {
                info.grayscale = quint8(d[5]) == 1;  // Pillow: one component -> "L"
            } else if (m == 0xE1 && info.exif.isEmpty() && info.exifError.isEmpty() &&
                       d.startsWith(QByteArray("Exif\0", 5))) {
                info.exif = stripExifHeader(d);
            } else if (m == 0xE2 && d.startsWith(kIccHeader) && n >= 14) {
                const int seq = quint8(d[12]), count = quint8(d[13]);
                if (seq < 1 || count < 1 || seq > count || (iccCount && iccCount != count) || iccParts.count(seq))
                    iccBad = true;
                iccCount = count;
                iccParts[seq] = d.mid(14);
            }
        } else if (!skip(dev, n)) {
            break;
        }
    }
    if (!iccBad && iccCount && int(iccParts.size()) == iccCount) {
        QByteArray icc;
        for (const auto &[seq, part] : iccParts)
            icc += part;
        if (icc.size() <= kMaxIcc)
            info.icc = icc;
    }
}

void readWebP(QIODevice &dev, SourceInfo &info)
{
    char hdr[12];
    if (!readExact(dev, hdr, 12) || QByteArray(hdr, 4) != "RIFF" || QByteArray(hdr + 8, 4) != "WEBP")
        return;
    for (int guard = 0; guard < 10'000; ++guard) {
        char ch[8];
        if (!readExact(dev, ch, 8))
            return;
        const QByteArray type(ch, 4);
        const quint32 len = qFromLittleEndian<quint32>(ch + 4);
        const qint64 padded = qint64(len) + (len & 1);
        const bool want = (type == "ICCP" && info.icc.isEmpty() && len <= kMaxIcc) ||
                          (type == "EXIF" && info.exif.isEmpty() && info.exifError.isEmpty());
        if (type == "EXIF" && len > kMaxExif) {
            info.exifError = QStringLiteral("EXIF chunk too large (%1 bytes)").arg(len);
        } else if (want) {
            QByteArray d(qsizetype(len), Qt::Uninitialized);
            if (!readExact(dev, d.data(), len)) {
                if (type == "EXIF")
                    info.exifError = QStringLiteral("EXIF chunk is truncated");
                return;
            }
            if (type == "ICCP")
                info.icc = d;
            else
                info.exif = stripExifHeader(d);
            if (!skip(dev, padded - len))
                return;
            continue;
        }
        if (!skip(dev, padded))
            return;
    }
}

// ---------- EXIF (TIFF) ----------

struct Range {
    qsizetype begin = 0, end = 0;
};

class Tiff {
public:
    explicit Tiff(QByteArray &b) : m_b(b) {}
    bool little = true;
    qsizetype size() const { return m_b.size(); }
    bool has(qsizetype off, qsizetype n) const { return off >= 0 && n >= 0 && off <= size() && n <= size() - off; }
    quint16 u16(qsizetype off) const
    {
        const char *p = m_b.constData() + off;
        return little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
    }
    quint32 u32(qsizetype off) const
    {
        const char *p = m_b.constData() + off;
        return little ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    }
    void put16(qsizetype off, quint16 v)
    {
        char *p = m_b.data() + off;
        little ? qToLittleEndian(v, p) : qToBigEndian(v, p);
    }
    void put32(qsizetype off, quint32 v)
    {
        char *p = m_b.data() + off;
        little ? qToLittleEndian(v, p) : qToBigEndian(v, p);
    }

private:
    QByteArray &m_b;
};

qint64 typeSize(quint16 type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;   // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8: return 2;                   // SHORT SSHORT
    case 4: case 9: case 11: case 13: return 4; // LONG SLONG FLOAT IFD
    case 5: case 10: case 12: return 8;         // RATIONAL SRATIONAL DOUBLE
    default: return 0;                          // unknown: value not checked
    }
}

struct Entry {
    quint16 tag = 0, type = 0;
    quint32 count = 0;
    qsizetype at = 0;  // offset of the 12-byte entry
};

// Parses one IFD: the table and the out-of-line values must lie inside the block.
std::expected<std::vector<Entry>, QString> parseIfd(const Tiff &t, qsizetype off, const char *name,
                                                    std::vector<Range> &ranges, qsizetype *nextAt = nullptr)
{
    if (!t.has(off, 2))
        return std::unexpected(QStringLiteral("%1 offset %2 is outside the EXIF block").arg(QLatin1String(name)).arg(off));
    const int count = t.u16(off);
    if (count > 4096)
        return std::unexpected(QStringLiteral("%1 has %2 entries").arg(QLatin1String(name)).arg(count));
    const qsizetype tableEnd = off + 2 + 12 * qsizetype(count);
    if (!t.has(off, tableEnd - off + (nextAt ? 4 : 0)))
        return std::unexpected(QStringLiteral("%1 (%2 entries) runs past the end of the EXIF block")
                                   .arg(QLatin1String(name)).arg(count));
    // The 4-byte next-IFD offset belongs to the IFD (sub-IFDs: kept when it is there).
    ranges.push_back({off, tableEnd + (nextAt || t.has(tableEnd, 4) ? 4 : 0)});
    if (nextAt)
        *nextAt = tableEnd;
    std::vector<Entry> entries;
    entries.reserve(count);
    for (int i = 0; i < count; ++i) {
        Entry e;
        e.at = off + 2 + 12 * qsizetype(i);
        e.tag = t.u16(e.at);
        e.type = t.u16(e.at + 2);
        e.count = t.u32(e.at + 4);
        const qint64 size = typeSize(e.type) * qint64(e.count);
        if (size > 4) {
            const qsizetype v = t.u32(e.at + 8);
            if (!t.has(v, size))
                return std::unexpected(QStringLiteral("value of tag 0x%1 in %2 is outside the EXIF block")
                                           .arg(e.tag, 4, 16, QLatin1Char('0')).arg(QLatin1String(name)));
            ranges.push_back({v, v + qsizetype(size)});
        }
        entries.push_back(e);
    }
    return entries;
}

const Entry *findTag(const std::vector<Entry> &entries, quint16 tag)
{
    for (const Entry &e : entries)
        if (e.tag == tag)
            return &e;
    return nullptr;
}

bool overlaps(const Range &a, const Range &b) { return a.begin < b.end && b.begin < a.end; }

// ---------- writing ----------

void appendBe32(QByteArray &b, quint32 v)
{
    char p[4];
    qToBigEndian(v, p);
    b.append(p, 4);
}

void appendLe32(QByteArray &b, quint32 v)
{
    char p[4];
    qToLittleEndian(v, p);
    b.append(p, 4);
}

QByteArray pngChunk(const QByteArray &type, const QByteArray &data)
{
    QByteArray c;
    appendBe32(c, quint32(data.size()));
    c += type;
    c += data;
    appendBe32(c, crc32(c.constData() + 4, c.size() - 4));
    return c;
}

QByteArray jpegSegment(quint8 marker, const QByteArray &data)
{
    QByteArray s;
    s.append('\xFF').append(char(marker));
    char len[2];
    qToBigEndian<quint16>(quint16(data.size() + 2), len);
    s.append(len, 2);
    s += data;
    return s;
}

std::expected<QByteArray, QString> embedPng(const QByteArray &in, const QByteArray &icc, const QByteArray &exif)
{
    if (!in.startsWith(kPngSig) || in.size() < 33 || in.mid(12, 4) != "IHDR" ||
        qFromBigEndian<quint32>(in.constData() + 8) != 13)
        return std::unexpected(QStringLiteral("the encoded PNG has an unexpected layout"));
    QByteArray extra;
    if (!icc.isEmpty()) {
        // keyword, NUL, compression method 0, zlib stream (qCompress prefixes 4 size bytes)
        QByteArray d("ICC Profile");
        d.append('\0').append('\0');
        d += qCompress(icc, 9).mid(4);
        extra += pngChunk("iCCP", d);
    }
    if (!exif.isEmpty())
        extra += pngChunk("eXIf", exif);
    QByteArray out;
    out.reserve(in.size() + extra.size());
    out += in.left(33);
    out += extra;
    out += QByteArrayView(in).mid(33);
    return out;
}

std::expected<QByteArray, QString> embedJpeg(const QByteArray &in, const QByteArray &icc, const QByteArray &exif)
{
    if (in.size() < 4 || quint8(in[0]) != 0xFF || quint8(in[1]) != 0xD8)
        return std::unexpected(QStringLiteral("the encoded JPEG has no SOI marker"));
    if (exif.size() > kMaxJpegExif)
        return std::unexpected(QStringLiteral("EXIF block too large for JPEG (%1 bytes, at most %2)")
                                   .arg(exif.size()).arg(kMaxJpegExif));
    constexpr qsizetype kIccPart = 65535 - 2 - 14;
    const qsizetype parts = (icc.size() + kIccPart - 1) / kIccPart;
    if (parts > 255)
        return std::unexpected(QStringLiteral("ICC profile too large for JPEG (%1 bytes)").arg(icc.size()));
    // ICC after APP0 (JFIF), which Qt writes first.
    qsizetype pos = 2;
    if (quint8(in[2]) == 0xFF && quint8(in[3]) == 0xE0 && in.size() >= 6)
        pos = 4 + qFromBigEndian<quint16>(in.constData() + 4);
    if (pos > in.size())
        return std::unexpected(QStringLiteral("the encoded JPEG has a damaged APP0 segment"));
    QByteArray out;
    out.reserve(in.size() + exif.size() + icc.size() + 64);
    out += in.left(2);
    if (!exif.isEmpty())
        out += jpegSegment(0xE1, kExifHeader + exif);
    out += QByteArrayView(in).mid(2, pos - 2);
    for (qsizetype i = 0; i < parts; ++i) {
        QByteArray d = kIccHeader;
        d.append(char(i + 1)).append(char(parts));
        d += QByteArrayView(icc).mid(i * kIccPart, kIccPart);
        out += jpegSegment(0xE2, d);
    }
    out += QByteArrayView(in).mid(pos);
    return out;
}

std::expected<QByteArray, QString> embedWebP(const QByteArray &in, const QByteArray &icc, const QByteArray &exif)
{
    if (in.size() < 20 || !in.startsWith("RIFF") || in.mid(8, 4) != "WEBP" ||
        qsizetype(qFromLittleEndian<quint32>(in.constData() + 4)) + 8 != in.size())
        return std::unexpected(QStringLiteral("the encoded WebP has an unexpected RIFF header"));
    struct Chunk {
        QByteArray type;
        QByteArrayView data;
    };
    std::vector<Chunk> chunks;
    for (qsizetype p = 12; p < in.size();) {
        if (in.size() - p < 8)
            return std::unexpected(QStringLiteral("the encoded WebP has a truncated chunk"));
        const QByteArray type = in.mid(p, 4);
        const qsizetype len = qFromLittleEndian<quint32>(in.constData() + p + 4);
        if (len > in.size() - p - 8)
            return std::unexpected(QStringLiteral("the encoded WebP has a truncated chunk"));
        if (type == "ICCP" || type == "EXIF" || type == "ANIM")
            return std::unexpected(QStringLiteral("the encoded WebP already has a %1 chunk").arg(QLatin1String(type)));
        chunks.push_back({type, QByteArrayView(in).mid(p + 8, len)});
        p += 8 + len + (len & 1);
    }
    if (chunks.empty())
        return std::unexpected(QStringLiteral("the encoded WebP has no chunks"));

    QByteArray vp8x;
    if (chunks.front().type == "VP8X") {
        if (chunks.front().data.size() != 10)
            return std::unexpected(QStringLiteral("the encoded WebP has a damaged VP8X chunk"));
        vp8x = chunks.front().data.toByteArray();
        chunks.erase(chunks.begin());
    } else {
        // Simple format: the canvas size (and the alpha hint) come from the bitstream header.
        const Chunk &img = chunks.front();
        quint32 w = 0, h = 0;
        bool alpha = false;
        if (img.type == "VP8L" && img.data.size() >= 5 && quint8(img.data[0]) == 0x2F) {
            const quint32 v = qFromLittleEndian<quint32>(img.data.data() + 1);
            w = (v & 0x3FFF) + 1;
            h = ((v >> 14) & 0x3FFF) + 1;
            alpha = (v >> 28) & 1;
        } else if (img.type == "VP8 " && img.data.size() >= 10 && quint8(img.data[3]) == 0x9D &&
                   quint8(img.data[4]) == 0x01 && quint8(img.data[5]) == 0x2A) {
            w = qFromLittleEndian<quint16>(img.data.data() + 6) & 0x3FFF;
            h = qFromLittleEndian<quint16>(img.data.data() + 8) & 0x3FFF;
        } else {
            return std::unexpected(QStringLiteral("the encoded WebP has an unknown bitstream"));
        }
        if (w == 0 || h == 0)
            return std::unexpected(QStringLiteral("the encoded WebP has no canvas size"));
        // flags (1), reserved (3), canvas width - 1 (24 bits), canvas height - 1 (24 bits)
        vp8x = QByteArray(12, '\0');
        vp8x[0] = alpha ? char(0x10) : char(0);
        qToLittleEndian<quint32>(w - 1, vp8x.data() + 4);  // byte 7 is overwritten next
        qToLittleEndian<quint32>(h - 1, vp8x.data() + 7);  // byte 10 is cut off below
        vp8x.truncate(10);
    }
    if (!icc.isEmpty())
        vp8x[0] = char(quint8(vp8x[0]) | 0x20);
    if (!exif.isEmpty())
        vp8x[0] = char(quint8(vp8x[0]) | 0x08);

    auto chunk = [](QByteArray &out, const QByteArray &type, QByteArrayView data) {
        out += type;
        appendLe32(out, quint32(data.size()));
        out += data;
        if (data.size() & 1)
            out += '\0';
    };
    QByteArray out("RIFF\0\0\0\0WEBP", 12);
    out.reserve(in.size() + icc.size() + exif.size() + 64);
    chunk(out, "VP8X", vp8x);
    if (!icc.isEmpty())
        chunk(out, "ICCP", icc);
    bool exifDone = exif.isEmpty();
    for (const Chunk &c : chunks) {
        if (!exifDone && c.type == "XMP ") {  // order: ... image data, EXIF, XMP
            chunk(out, "EXIF", exif);
            exifDone = true;
        }
        chunk(out, c.type, c.data);
    }
    if (!exifDone)
        chunk(out, "EXIF", exif);
    qToLittleEndian<quint32>(quint32(out.size() - 8), out.data() + 4);
    return out;
}

} // namespace

quint32 crc32(const char *data, qsizetype size, quint32 crc)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (qsizetype i = 0; i < size; ++i)
        crc = table[(crc ^ quint8(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

SourceInfo read(QIODevice &dev)
{
    SourceInfo info;
    const QByteArray head = dev.peek(12);
    if (head.startsWith(kPngSig)) {
        dev.skip(8);
        readPng(dev, info);
    } else if (head.size() >= 3 && quint8(head[0]) == 0xFF && quint8(head[1]) == 0xD8) {
        dev.skip(2);
        readJpeg(dev, info);
    } else if (head.size() == 12 && head.startsWith("RIFF") && head.mid(8, 4) == "WEBP") {
        readWebP(dev, info);
    }
    return info;
}

SourceInfo read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return read(f);
}

QByteArray iccColorSpace(const QByteArray &icc)
{
    return icc.size() >= 128 ? icc.mid(16, 4) : QByteArray();
}

std::expected<QByteArray, QString> prepareExif(const QByteArray &tiffIn, bool orientationApplied, int width,
                                               int height)
{
    QByteArray b = tiffIn;
    Tiff t(b);
    if (b.size() < 8)
        return std::unexpected(QStringLiteral("EXIF block too short (%1 bytes)").arg(b.size()));
    if (b.startsWith(QByteArray("II*\0", 4)))
        t.little = true;
    else if (b.startsWith(QByteArray("MM\0*", 4)))
        t.little = false;
    else
        return std::unexpected(QStringLiteral("EXIF block has no TIFF header"));

    std::vector<Range> kept{{0, 8}};
    qsizetype nextAt = 0;
    const qsizetype ifd0 = t.u32(4);
    auto e0 = parseIfd(t, ifd0, "IFD0", kept, &nextAt);
    if (!e0)
        return std::unexpected(e0.error());

    std::vector<qsizetype> seen{ifd0};
    auto subIfd = [&](const std::vector<Entry> &parent, quint16 tag,
                      const char *name) -> std::expected<std::vector<Entry>, QString> {
        const Entry *e = findTag(parent, tag);
        if (!e)
            return std::vector<Entry>{};
        if ((e->type != 4 && e->type != 13) || e->count != 1)
            return std::unexpected(QStringLiteral("%1 pointer has type %2").arg(QLatin1String(name)).arg(e->type));
        const qsizetype off = t.u32(e->at + 8);
        if (std::ranges::find(seen, off) != seen.end())
            return std::unexpected(QStringLiteral("%1 points back to an IFD already read").arg(QLatin1String(name)));
        seen.push_back(off);
        return parseIfd(t, off, name, kept);
    };
    auto exifIfd = subIfd(*e0, 0x8769, "Exif IFD");
    if (!exifIfd)
        return std::unexpected(exifIfd.error());
    auto gpsIfd = subIfd(*e0, 0x8825, "GPS IFD");
    if (!gpsIfd)
        return std::unexpected(gpsIfd.error());
    auto interopIfd = subIfd(*exifIfd, 0xA005, "Interoperability IFD");
    if (!interopIfd)
        return std::unexpected(interopIfd.error());

    // Orientation (IFD0, SHORT): the loader already turned the pixels upright.
    if (const Entry *o = findTag(*e0, 0x0112); o && orientationApplied && o->type == 3 && o->count == 1)
        t.put16(o->at + 8, 1);
    // PixelXDimension / PixelYDimension (Exif IFD, SHORT or LONG): the output size.
    for (auto [tag, v] : {std::pair<quint16, int>{0xA002, width}, std::pair<quint16, int>{0xA003, height}}) {
        const Entry *e = findTag(*exifIfd, tag);
        if (!e || e->count != 1 || (e->type != 3 && e->type != 4))
            continue;
        if (e->type == 3 && v > 0xFFFF)
            t.put16(e->at + 2, 4);  // SHORT -> LONG; the value still fits in the entry
        if (e->type == 4 || v > 0xFFFF)
            t.put32(e->at + 8, quint32(v));
        else
            t.put16(e->at + 8, quint16(v));
    }

    // Thumbnail: unlink IFD1, then clear its table, values and image data where they do not
    // overlap anything kept, and cut them off when they are at the end of the block.
    const qsizetype ifd1 = t.u32(nextAt);
    t.put32(nextAt, 0);
    if (ifd1 != 0) {
        std::vector<Range> dropped;
        if (auto e1 = parseIfd(t, ifd1, "IFD1", dropped)) {
            for (auto [offTag, lenTag] : {std::pair<quint16, quint16>{0x0201, 0x0202}, {0x0111, 0x0117}}) {
                const Entry *o = findTag(*e1, offTag), *l = findTag(*e1, lenTag);
                if (!o || !l || o->count != 1 || l->count != 1)
                    continue;
                auto value = [&](const Entry *e) -> qsizetype { return e->type == 3 ? t.u16(e->at + 8) : t.u32(e->at + 8); };
                const qsizetype off = value(o), len = value(l);
                if (t.has(off, len))
                    dropped.push_back({off, off + len});
            }
            const bool clash = std::ranges::any_of(dropped, [&](const Range &d) {
                return std::ranges::any_of(kept, [&](const Range &k) { return overlaps(d, k); });
            });
            if (!clash) {
                for (const Range &d : dropped)
                    std::fill(b.begin() + d.begin, b.begin() + d.end, '\0');
                qsizetype keptEnd = 0;
                for (const Range &k : kept)
                    keptEnd = std::max(keptEnd, k.end);
                if (std::ranges::all_of(dropped, [&](const Range &d) { return d.begin >= keptEnd; }))
                    b.truncate(keptEnd);
            }
        }
    }
    return b;
}

std::expected<QByteArray, QString> embed(const QByteArray &encoded, OutputFormat format, const QByteArray &icc,
                                         const QByteArray &exif)
{
    if (icc.isEmpty() && exif.isEmpty())
        return encoded;
    switch (format) {
    case OutputFormat::Png: return embedPng(encoded, icc, exif);
    case OutputFormat::Jpeg: return embedJpeg(encoded, icc, exif);
    case OutputFormat::WebP: return embedWebP(encoded, icc, exif);
    }
    return std::unexpected(QStringLiteral("unknown output format"));
}

} // namespace arx::metadata
