// SPDX-License-Identifier: MPL-2.0

#include "features/brush/manager/AbrBrushImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Format notes, all established by diffing files exported from Photoshop 2023
// one changed parameter at a time (see the .abr lab notes):
//
//   u16 version, u16 subversion, then '8BIM' + 4cc key + u32 length + body,
//   each body followed by padding up to the next section.
//
//   'samp' holds the sampled tip bitmaps, 'desc' a single descriptor with every
//   preset, 'patt' the patterns, 'phry' the brush folder tree.
//
// Two traps that are easy to get wrong and silent when you do:
//
//   * Descriptor keys are 4cc's padded with spaces. The key is 'Cnt ', not
//     'Cnt', and 'H   ', not 'H'. Looking one up unpadded returns nothing at
//     all rather than failing, so descriptorValue() pads on a miss.
//   * The descriptor is sparse. A useXxx flag decides whether a whole group of
//     keys is written; an absent group means "feature off", never "defaults".
//     Values of a group whose flag is false are stale and must be ignored.

namespace ruwa::core::brushes {

namespace {

constexpr quint32 kTag8Bim = 0x3842494d;
constexpr quint32 kSubtagSamples = 0x73616d70; // 'samp'
constexpr quint32 kSubtagDescriptors = 0x64657363; // 'desc'

constexpr int kMaxTipExtent = 16384;
constexpr quint32 kMaxDescriptorItems = 200000;
constexpr quint32 kMaxStringLength = 1u << 20;
constexpr quint32 kMaxChannelSlots = 64;
constexpr int kMaxDescriptorDepth = 64;

/// Reserved entry holding a descriptor's class id, so a computedBrush can be
/// told apart from a sampledBrush or a bristle dBrush. No real key can collide
/// with it: descriptor keys are 4cc's of letters and spaces.
QString classKey()
{
    return QStringLiteral("@class");
}

// Ruwa clamps mirrored from BrushDynamicsTypes.h / PixelBrushModule.cpp.
constexpr double kSpacingMin = 0.005;
constexpr double kSpacingMax = 5.0;
constexpr double kCurveSmoothness = 0.65;

class AbrReader {
public:
    explicit AbrReader(QByteArray data)
        : m_data(std::move(data))
    {
    }

    qsizetype remaining() const { return m_data.size() - m_pos; }
    bool atEnd() const { return m_pos >= m_data.size(); }

    bool skip(qsizetype count)
    {
        if (count < 0 || remaining() < count) {
            return false;
        }
        m_pos += count;
        return true;
    }

    bool readU8(quint8& out)
    {
        if (remaining() < 1) {
            return false;
        }
        out = static_cast<quint8>(m_data.at(m_pos++));
        return true;
    }

    bool readU16(quint16& out)
    {
        if (remaining() < 2) {
            return false;
        }
        const auto* p = reinterpret_cast<const uchar*>(m_data.constData() + m_pos);
        out = static_cast<quint16>((p[0] << 8) | p[1]);
        m_pos += 2;
        return true;
    }

    bool readU32(quint32& out)
    {
        if (remaining() < 4) {
            return false;
        }
        const auto* p = reinterpret_cast<const uchar*>(m_data.constData() + m_pos);
        out = (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16)
            | (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
        m_pos += 4;
        return true;
    }

    bool readI32(qint32& out)
    {
        quint32 value = 0;
        if (!readU32(value)) {
            return false;
        }
        out = static_cast<qint32>(value);
        return true;
    }

    bool readI64(qint64& out)
    {
        quint32 hi = 0;
        quint32 lo = 0;
        if (!readU32(hi) || !readU32(lo)) {
            return false;
        }
        out = static_cast<qint64>((static_cast<quint64>(hi) << 32) | lo);
        return true;
    }

    bool readDouble(double& out)
    {
        quint32 hi = 0;
        quint32 lo = 0;
        if (!readU32(hi) || !readU32(lo)) {
            return false;
        }
        const quint64 bits = (static_cast<quint64>(hi) << 32) | lo;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool readBytes(qsizetype count, QByteArray& out)
    {
        if (count < 0 || remaining() < count) {
            return false;
        }
        out = m_data.mid(m_pos, count);
        m_pos += count;
        return true;
    }

    bool readFourCC(quint32& out) { return readU32(out); }

private:
    QByteArray m_data;
    qsizetype m_pos = 0;
};

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

QString trimTrailingNulls(QString value)
{
    while (!value.isEmpty() && value.back() == QChar(u'\0')) {
        value.chop(1);
    }
    return value;
}

/// Length-prefixed ASCII key or class id. A zero length means a literal 4cc,
/// which is how nearly every key in a brush descriptor is stored.
bool readKeyString(AbrReader& reader, QString& out)
{
    quint32 length = 0;
    if (!reader.readU32(length)) {
        return false;
    }
    if (length == 0) {
        length = 4;
    }
    if (length > 1024) {
        return false;
    }
    QByteArray bytes;
    if (!reader.readBytes(length, bytes)) {
        return false;
    }
    out = trimTrailingNulls(QString::fromLatin1(bytes));
    return true;
}

bool readUnicodeString(AbrReader& reader, QString& out)
{
    quint32 charCount = 0;
    if (!reader.readU32(charCount) || charCount > kMaxStringLength) {
        return false;
    }
    QByteArray bytes;
    if (!reader.readBytes(static_cast<qsizetype>(charCount) * 2, bytes)) {
        return false;
    }
    QVector<char16_t> chars;
    chars.reserve(static_cast<int>(charCount));
    for (quint32 i = 0; i < charCount; ++i) {
        const auto hi = static_cast<uchar>(bytes.at(static_cast<int>(i * 2)));
        const auto lo = static_cast<uchar>(bytes.at(static_cast<int>(i * 2 + 1)));
        chars.append(static_cast<char16_t>((hi << 8) | lo));
    }
    out = trimTrailingNulls(QString::fromUtf16(chars.constData(), chars.size()));
    return true;
}

bool readClassId(AbrReader& reader, QString& out)
{
    QString name;
    if (!readUnicodeString(reader, name)) {
        return false;
    }
    return readKeyString(reader, out);
}

bool parseTypedValue(AbrReader& reader, QVariant& out, int depth);
bool parseDescriptorBody(AbrReader& reader, QVariantMap& out, int depth);

/// A reference ('obj '): a list of steps, each with its own layout. Brush
/// presets do not use these, but a malformed skip here desynchronises the whole
/// descriptor, so every step is consumed exactly.
bool parseReference(AbrReader& reader, QVariantList& out, int depth)
{
    quint32 itemCount = 0;
    if (!reader.readU32(itemCount) || itemCount > kMaxDescriptorItems) {
        return false;
    }

    for (quint32 i = 0; i < itemCount; ++i) {
        quint32 type = 0;
        if (!reader.readFourCC(type)) {
            return false;
        }
        QString classId;
        QString keyId;
        quint32 numeric = 0;
        switch (type) {
        case 0x70726f70: // 'prop'
            if (!readClassId(reader, classId) || !readKeyString(reader, keyId)) {
                return false;
            }
            out.append(keyId);
            break;
        case 0x436c7373: // 'Clss'
        case 0x476c6243: // 'GlbC'
        case 0x74797065: // 'type'
            if (!readClassId(reader, classId)) {
                return false;
            }
            out.append(classId);
            break;
        case 0x456e6d72: { // 'Enmr'
            QString enumId;
            if (!readClassId(reader, classId) || !readKeyString(reader, keyId)
                || !readKeyString(reader, enumId)) {
                return false;
            }
            out.append(enumId);
            break;
        }
        case 0x72656c65: // 'rele'
            if (!readClassId(reader, classId) || !reader.readU32(numeric)) {
                return false;
            }
            out.append(static_cast<int>(numeric));
            break;
        case 0x49646e74: // 'Idnt'
        case 0x696e6478: // 'indx'
            if (!reader.readU32(numeric)) {
                return false;
            }
            out.append(static_cast<int>(numeric));
            break;
        case 0x6e616d65: { // 'name'
            QString name;
            if (!readClassId(reader, classId) || !readUnicodeString(reader, name)) {
                return false;
            }
            out.append(name);
            break;
        }
        default:
            return false;
        }
    }
    (void) depth;
    return true;
}

bool parseTypedValue(AbrReader& reader, QVariant& out, int depth)
{
    if (depth > kMaxDescriptorDepth) {
        return false;
    }

    quint32 type = 0;
    if (!reader.readFourCC(type)) {
        return false;
    }

    switch (type) {
    case 0x4f626a63: // 'Objc'
    case 0x476c624f: { // 'GlbO'
        QVariantMap descriptor;
        if (!parseDescriptorBody(reader, descriptor, depth + 1)) {
            return false;
        }
        out = descriptor;
        return true;
    }
    case 0x566c4c73: { // 'VlLs'
        quint32 itemCount = 0;
        if (!reader.readU32(itemCount) || itemCount > kMaxDescriptorItems) {
            return false;
        }
        QVariantList list;
        list.reserve(static_cast<int>(std::min<quint32>(itemCount, 4096)));
        for (quint32 i = 0; i < itemCount; ++i) {
            QVariant item;
            if (!parseTypedValue(reader, item, depth + 1)) {
                return false;
            }
            list.append(item);
        }
        out = list;
        return true;
    }
    case 0x646f7562: { // 'doub'
        double value = 0.0;
        if (!reader.readDouble(value)) {
            return false;
        }
        out = value;
        return true;
    }
    case 0x556e7446: { // 'UntF' — unit plus value; the unit is implied by the key
        quint32 unit = 0;
        double value = 0.0;
        if (!reader.readFourCC(unit) || !reader.readDouble(value)) {
            return false;
        }
        out = value;
        return true;
    }
    case 0x556e466c: { // 'UnFl'
        quint32 unit = 0;
        quint32 count = 0;
        if (!reader.readFourCC(unit) || !reader.readU32(count) || count > kMaxDescriptorItems) {
            return false;
        }
        QVariantList values;
        for (quint32 i = 0; i < count; ++i) {
            double value = 0.0;
            if (!reader.readDouble(value)) {
                return false;
            }
            values.append(value);
        }
        out = values;
        return true;
    }
    case 0x54455854: { // 'TEXT'
        QString value;
        if (!readUnicodeString(reader, value)) {
            return false;
        }
        out = value;
        return true;
    }
    case 0x656e756d: { // 'enum'
        QString typeId;
        QString enumId;
        if (!readKeyString(reader, typeId) || !readKeyString(reader, enumId)) {
            return false;
        }
        out = enumId;
        return true;
    }
    case 0x6c6f6e67: { // 'long'
        qint32 value = 0;
        if (!reader.readI32(value)) {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }
    case 0x636f6d70: { // 'comp'
        qint64 value = 0;
        if (!reader.readI64(value)) {
            return false;
        }
        out = static_cast<qlonglong>(value);
        return true;
    }
    case 0x626f6f6c: { // 'bool'
        quint8 value = 0;
        if (!reader.readU8(value)) {
            return false;
        }
        out = (value != 0);
        return true;
    }
    case 0x6f626a20: { // 'obj ' — a reference, not a descriptor
        QVariantList reference;
        if (!parseReference(reader, reference, depth + 1)) {
            return false;
        }
        out = reference;
        return true;
    }
    case 0x74797065: // 'type'
    case 0x476c6243: { // 'GlbC'
        QString classId;
        if (!readClassId(reader, classId)) {
            return false;
        }
        out = classId;
        return true;
    }
    case 0x616c6973: // 'alis'
    case 0x74647461: // 'tdta'
    case 0x50746820: { // 'Pth '
        quint32 length = 0;
        if (!reader.readU32(length) || length > kMaxStringLength) {
            return false;
        }
        QByteArray value;
        if (!reader.readBytes(length, value)) {
            return false;
        }
        out = value;
        return true;
    }
    case 0x4f624172: { // 'ObAr'
        quint32 version = 0;
        QString classId;
        quint32 itemCount = 0;
        if (!reader.readU32(version) || !readClassId(reader, classId) || !reader.readU32(itemCount)
            || itemCount > kMaxDescriptorItems) {
            return false;
        }
        QVariantMap items;
        for (quint32 i = 0; i < itemCount; ++i) {
            QString key;
            QVariant value;
            if (!readKeyString(reader, key) || !parseTypedValue(reader, value, depth + 1)) {
                return false;
            }
            items.insert(key, value);
        }
        out = items;
        return true;
    }
    default:
        // An unknown OSType has an unknown length, so the reader can no longer
        // find the next value. Stopping beats emitting silent garbage.
        return false;
    }
}

bool parseDescriptorBody(AbrReader& reader, QVariantMap& out, int depth)
{
    if (depth > kMaxDescriptorDepth) {
        return false;
    }

    QString name;
    QString classId;
    if (!readUnicodeString(reader, name) || !readKeyString(reader, classId)) {
        return false;
    }

    quint32 itemCount = 0;
    if (!reader.readU32(itemCount) || itemCount > kMaxDescriptorItems) {
        return false;
    }

    out.insert(classKey(), classId);
    for (quint32 i = 0; i < itemCount; ++i) {
        QString key;
        QVariant value;
        if (!readKeyString(reader, key) || !parseTypedValue(reader, value, depth + 1)) {
            return false;
        }
        out.insert(key, value);
    }
    return true;
}

// ---------------------------------------------------------------------------
// descriptor access
// ---------------------------------------------------------------------------

/// Keys are 4cc's padded with spaces, so a caller asking for "Cnt" must still
/// find "Cnt ". Without this every short key silently reads as absent.
QVariant descriptorValue(const QVariantMap& map, const QString& key)
{
    auto it = map.constFind(key);
    if (it != map.constEnd()) {
        return it.value();
    }
    if (key.size() < 4) {
        it = map.constFind(key.leftJustified(4, QLatin1Char(' ')));
        if (it != map.constEnd()) {
            return it.value();
        }
    }
    return {};
}

bool descriptorNumber(const QVariantMap& map, const QString& key, double& out)
{
    const QVariant value = descriptorValue(map, key);
    if (!value.isValid()) {
        return false;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return false;
    }
    out = number;
    return true;
}

double descriptorNumberOr(const QVariantMap& map, const QString& key, double fallback)
{
    double value = fallback;
    return descriptorNumber(map, key, value) ? value : fallback;
}

bool descriptorFlag(const QVariantMap& map, const QString& key)
{
    const QVariant value = descriptorValue(map, key);
    return value.userType() == QMetaType::Bool && value.toBool();
}

QVariantMap descriptorMap(const QVariantMap& map, const QString& key)
{
    const QVariant value = descriptorValue(map, key);
    return value.userType() == QMetaType::QVariantMap ? value.toMap() : QVariantMap {};
}

QString descriptorText(const QVariantMap& map, const QString& key)
{
    const QVariant value = descriptorValue(map, key);
    return value.userType() == QMetaType::QString ? value.toString() : QString {};
}

QString descriptorClass(const QVariantMap& map)
{
    return map.value(classKey()).toString();
}

// ---------------------------------------------------------------------------
// tip bitmaps ('samp')
// ---------------------------------------------------------------------------

struct AbrTipBitmap {
    QString id;
    int width = 0;
    int height = 0;
    QByteArray alpha;
};

bool decodePackBitsRow(const QByteArray& input, int rowBytes, QByteArray& out)
{
    int offset = 0;
    while (offset < input.size() && out.size() < rowBytes) {
        const auto header = static_cast<qint8>(input.at(offset++));
        if (header >= 0) {
            const int count = header + 1;
            if (offset + count > input.size()) {
                return false;
            }
            const int appendCount = std::min(count, rowBytes - static_cast<int>(out.size()));
            out.append(input.constData() + offset, appendCount);
            offset += count;
        } else if (header > -128) {
            const int count = 1 - header;
            if (offset >= input.size()) {
                return false;
            }
            out.append(QByteArray(count, input.at(offset++)));
        }
    }
    return out.size() == rowBytes;
}

bool decodeBitmap(
    const QByteArray& bitmap, int width, int height, int depth, int compression, QByteArray& alpha)
{
    const int bytesPerPixel = depth / 8;
    const int rowBytes = width * bytesPerPixel;
    const int expectedBytes = rowBytes * height;
    QByteArray decoded;

    if (compression == 0) {
        if (bitmap.size() < expectedBytes) {
            return false;
        }
        decoded = bitmap.left(expectedBytes);
    } else if (compression == 1) {
        AbrReader reader(bitmap);
        QVector<quint16> rowSizes;
        rowSizes.reserve(height);
        for (int y = 0; y < height; ++y) {
            quint16 rowSize = 0;
            if (!reader.readU16(rowSize)) {
                return false;
            }
            rowSizes.append(rowSize);
        }
        decoded.reserve(expectedBytes);
        for (int y = 0; y < height; ++y) {
            QByteArray packedRow;
            if (!reader.readBytes(rowSizes[y], packedRow)) {
                return false;
            }
            QByteArray row;
            row.reserve(rowBytes);
            if (!decodePackBitsRow(packedRow, rowBytes, row)) {
                return false;
            }
            decoded.append(row);
        }
    } else {
        return false;
    }

    alpha.resize(width * height);
    if (depth == 8) {
        std::copy(decoded.cbegin(), decoded.cbegin() + alpha.size(), alpha.begin());
        return true;
    }
    if (depth == 16) {
        for (int i = 0; i < alpha.size(); ++i) {
            alpha[i] = decoded.at(i * 2);
        }
        return true;
    }
    return false;
}

bool parseImageData(AbrReader& reader, AbrTipBitmap& tip)
{
    quint32 top = 0;
    quint32 left = 0;
    quint32 bottom = 0;
    quint32 right = 0;
    quint16 depth = 0;
    quint8 compression = 0;
    if (!reader.readU32(top) || !reader.readU32(left) || !reader.readU32(bottom)
        || !reader.readU32(right) || !reader.readU16(depth) || !reader.readU8(compression)) {
        return false;
    }
    if (right <= left || bottom <= top || (depth != 8 && depth != 16) || compression > 1) {
        return false;
    }

    const int width = static_cast<int>(right - left);
    const int height = static_cast<int>(bottom - top);
    if (width <= 0 || height <= 0 || width > kMaxTipExtent || height > kMaxTipExtent) {
        return false;
    }

    QByteArray bitmap;
    if (!reader.readBytes(reader.remaining(), bitmap)) {
        return false;
    }

    QByteArray alpha;
    if (!decodeBitmap(bitmap, width, height, depth, compression, alpha)) {
        return false;
    }

    tip.width = width;
    tip.height = height;
    tip.alpha = std::move(alpha);
    return true;
}

/// One sample. Subversion 2 wraps the bitmap in a table of channel slots -
/// Photoshop writes 56 of them, PSD's maximum, and marks exactly one written.
bool parseSample(const QByteArray& data, int subversion, AbrTipBitmap& tip)
{
    AbrReader reader(data);
    quint8 idLength = 0;
    QByteArray idBytes;
    if (!reader.readU8(idLength) || !reader.readBytes(idLength, idBytes)) {
        return false;
    }
    tip.id = trimTrailingNulls(QString::fromLatin1(idBytes));

    if (subversion == 1) {
        return reader.skip(10) && parseImageData(reader, tip);
    }

    // Two constant shorts, a version, a byte count and the tip's rect in the
    // source document. None of them is needed once the channel table is
    // reached, but each has to be consumed to stay in step.
    quint16 unknown1 = 0;
    quint16 unknown2 = 0;
    quint32 sampleVersion = 0;
    quint32 sampleLength = 0;
    QByteArray bounds;
    quint32 channelSlots = 0;
    if (!reader.readU16(unknown1) || !reader.readU16(unknown2) || !reader.readU32(sampleVersion)
        || !reader.readU32(sampleLength) || !reader.readBytes(16, bounds)
        || !reader.readU32(channelSlots)) {
        return false;
    }
    if (channelSlots == 0 || channelSlots > kMaxChannelSlots) {
        return false;
    }

    for (quint32 i = 0; i < channelSlots; ++i) {
        quint32 isWritten = 0;
        if (!reader.readU32(isWritten)) {
            return false;
        }
        if (isWritten == 0) {
            continue;
        }

        quint32 channelLength = 0;
        quint32 unusedDepth = 0;
        if (!reader.readU32(channelLength) || !reader.readU32(unusedDepth)) {
            return false;
        }
        if (channelLength <= 4 || channelLength - 4 > static_cast<quint32>(reader.remaining())) {
            return false;
        }

        QByteArray channelData;
        if (!reader.readBytes(static_cast<qsizetype>(channelLength - 4), channelData)) {
            return false;
        }
        AbrReader channelReader(channelData);
        AbrTipBitmap channelTip = tip;
        if (parseImageData(channelReader, channelTip)) {
            tip = std::move(channelTip);
            return true;
        }
    }

    return false;
}

QVector<AbrTipBitmap> parseSamples(const QByteArray& body, int subversion)
{
    QVector<AbrTipBitmap> tips;
    AbrReader reader(body);

    while (reader.remaining() >= 4) {
        quint32 sampleLength = 0;
        if (!reader.readU32(sampleLength) || sampleLength == 0
            || sampleLength > static_cast<quint32>(reader.remaining())) {
            break;
        }

        QByteArray sampleData;
        if (!reader.readBytes(sampleLength, sampleData)) {
            break;
        }

        AbrTipBitmap tip;
        if (parseSample(sampleData, subversion, tip)) {
            tips.append(std::move(tip));
        }

        const qsizetype padding = (4 - (sampleLength % 4)) % 4;
        if (padding > 0 && !reader.skip(padding)) {
            break;
        }
    }

    return tips;
}

// ---------------------------------------------------------------------------
// preset -> Ruwa settings
// ---------------------------------------------------------------------------

/// The Control dropdown, as stored. The numbering is historical, not the order
/// of the entries in the UI: Rotation is 8 while Initial Direction and
/// Direction are 5 and 6, and 7 is unused. Read every value off this table;
/// never index it positionally.
enum class AbrControl {
    Off = 0,
    Fade = 1,
    PenPressure = 2,
    PenTilt = 3,
    StylusWheel = 4,
    InitialDirection = 5,
    Direction = 6,
    Rotation = 8,
};

QString controlDisplayName(int control)
{
    switch (static_cast<AbrControl>(control)) {
    case AbrControl::Off:
        return QObject::tr("Off");
    case AbrControl::Fade:
        return QObject::tr("Fade");
    case AbrControl::PenPressure:
        return QObject::tr("Pen Pressure");
    case AbrControl::PenTilt:
        return QObject::tr("Pen Tilt");
    case AbrControl::StylusWheel:
        return QObject::tr("Stylus Wheel");
    case AbrControl::InitialDirection:
        return QObject::tr("Initial Direction");
    case AbrControl::Direction:
        return QObject::tr("Direction");
    case AbrControl::Rotation:
        return QObject::tr("Rotation");
    }
    return QString::number(control);
}

/// One 'brVr' block: a jitter slider, a minimum, and a control dropdown.
struct AbrDynamicsBlock {
    double jitter = 0.0; // 0..1
    double minimum = 0.0; // 0..1
    int control = 0;
    int fadeSteps = 0;
};

AbrDynamicsBlock readDynamicsBlock(const QVariantMap& parent, const QString& key)
{
    const QVariantMap block = descriptorMap(parent, key);
    AbrDynamicsBlock out;
    out.jitter = descriptorNumberOr(block, QStringLiteral("jitter"), 0.0) / 100.0;
    out.minimum = descriptorNumberOr(block, QStringLiteral("Mnm "), 0.0) / 100.0;
    out.control = static_cast<int>(descriptorNumberOr(block, QStringLiteral("bVTy"), 0.0));
    out.fadeSteps = static_cast<int>(descriptorNumberOr(block, QStringLiteral("fStp"), 0.0));
    return out;
}

QVariantMap curvePoint(double x, double y)
{
    return {
        { QStringLiteral("x"), x },
        { QStringLiteral("y"), y },
        { QStringLiteral("smoothness"), kCurveSmoothness },
    };
}

QVariantMap makeBinding(const QString& mode, double y0, double y1)
{
    return {
        { QStringLiteral("mode"), mode },
        { QStringLiteral("enabled"), true },
        { QStringLiteral("points"), QVariantList { curvePoint(0.0, y0), curvePoint(1.0, y1) } },
    };
}

/// Everything a single preset turns into, plus the running list of features
/// that had nowhere to go.
class AbrPresetConverter {
public:
    explicit AbrPresetConverter(const QVariantMap& preset)
        : m_preset(preset)
    {
    }

    void run()
    {
        convertTipShape();
        convertShapeDynamics();
        convertTransfer();
        convertScattering();
        convertTexture();
        convertColorDynamics();
        convertRemaining();
        if (!m_bindings.isEmpty()) {
            m_settings.insert(QStringLiteral("dynamics.bindings"), m_bindings);
        }
    }

    const QVariantMap& settings() const { return m_settings; }
    const QStringList& unsupported() const { return m_unsupported; }
    const QString& tipId() const { return m_tipId; }
    QString name() const { return descriptorText(m_preset, QStringLiteral("Nm  ")).trimmed(); }

private:
    void drop(const QString& what, const QString& why)
    {
        m_unsupported.append(QObject::tr("%1: %2").arg(what, why));
    }

    void bind(const QString& setting, const QString& source, const QVariantMap& binding)
    {
        QVariantMap sources = m_bindings.value(setting).toMap();
        sources.insert(source, binding);
        m_bindings.insert(setting, sources);
    }

    /// Ruwa input source for a Photoshop control, or an empty string plus the
    /// reason there is none. Nothing is substituted: a control without a real
    /// equivalent is reported rather than mapped onto something that merely
    /// looks similar, which would move the brush in the wrong way.
    QString inputSourceFor(const AbrDynamicsBlock& block, const QString& setting, QString& reason)
    {
        switch (static_cast<AbrControl>(block.control)) {
        case AbrControl::Off:
            return {};
        case AbrControl::PenPressure:
            return QStringLiteral("tabletPressure");
        case AbrControl::PenTilt:
            // Ruwa's penTilt is the tilt azimuth - the direction the pen leans.
            // That is what Photoshop means by Pen Tilt for the angle, but not
            // for size or opacity, which follow the tilt magnitude instead.
            if (setting == QLatin1String("shape.angle")) {
                return QStringLiteral("penTilt");
            }
            reason = QObject::tr("Ruwa's pen tilt input is the tilt direction, not the tilt "
                                 "amount this control needs");
            return {};
        case AbrControl::InitialDirection:
            // Ruwa's strokeDirection is live, so the dab keeps turning with the
            // stroke instead of holding the angle it started at. Close enough to
            // be worth importing, different enough to be worth saying.
            reason = QObject::tr("mapped to the live stroke direction; Ruwa cannot freeze it at "
                                 "the start of the stroke");
            return QStringLiteral("strokeDirection");
        case AbrControl::Direction:
            return QStringLiteral("strokeDirection");
        case AbrControl::Fade:
            reason = QObject::tr("fade runs over a number of dabs, which Ruwa has no input for");
            return {};
        case AbrControl::StylusWheel:
            reason = QObject::tr("no stylus wheel input");
            return {};
        case AbrControl::Rotation:
            reason = QObject::tr("no pen rotation input");
            return {};
        }
        reason = QObject::tr("unknown control");
        return {};
    }

    /// Size, opacity, flow and roundness all share one shape: a jitter that
    /// only ever reduces the value, floored by the block's minimum, plus a
    /// control that ramps the value from that same floor up to full.
    void convertScaledBlock(
        const AbrDynamicsBlock& block, const QString& setting, double floor, const QString& label)
    {
        if (block.jitter > 0.0) {
            const double low = std::clamp(std::max(floor, 1.0 - block.jitter), 0.0, 1.0);
            bind(setting, QStringLiteral("randomValue"),
                makeBinding(QStringLiteral("multiply"), low, 1.0));
        }

        QString reason;
        const QString source = inputSourceFor(block, setting, reason);
        if (!source.isEmpty()) {
            bind(setting, source,
                makeBinding(QStringLiteral("multiply"), std::clamp(floor, 0.0, 1.0), 1.0));
        }
        if (!reason.isEmpty()) {
            drop(QObject::tr("%1 control (%2)").arg(label, controlDisplayName(block.control)),
                reason);
        }
    }

    void convertTipShape()
    {
        const QVariantMap tip = descriptorMap(m_preset, QStringLiteral("Brsh"));
        if (tip.isEmpty()) {
            return;
        }
        m_tipId = descriptorText(tip, QStringLiteral("sampledData"));

        if (descriptorClass(tip) == QLatin1String("dBrush")) {
            drop(QObject::tr("Bristle tip"),
                QObject::tr("Ruwa has no bristle engine; imported as a plain tip"));
        }

        double value = 0.0;
        if (descriptorNumber(tip, QStringLiteral("Spcn"), value)) {
            const double spacing = value / 100.0;
            if (spacing > kSpacingMax) {
                drop(QObject::tr("Spacing %1%").arg(value, 0, 'f', 0),
                    QObject::tr("clamped to Ruwa's maximum of %1%")
                        .arg(kSpacingMax * 100.0, 0, 'f', 0));
            }
            m_settings.insert(
                QStringLiteral("shape.spacing"), std::clamp(spacing, kSpacingMin, kSpacingMax));
        }
        if (descriptorNumber(tip, QStringLiteral("Angl"), value)) {
            double angle = std::fmod(value, 360.0);
            if (angle < 0.0) {
                angle += 360.0;
            }
            m_settings.insert(QStringLiteral("shape.angle"), angle);
        }
        if (descriptorNumber(tip, QStringLiteral("Rndn"), value)) {
            m_settings.insert(
                QStringLiteral("shape.roundness"), std::clamp(value / 100.0, 0.0, 1.0));
        }

        if (!m_tipId.isEmpty()) {
            // A sampled tip already carries its own edge; softening it again
            // would only blur the alpha that was imported.
            m_settings.insert(QStringLiteral("shape.hardness"), 1.0);
        } else if (descriptorNumber(tip, QStringLiteral("Hrdn"), value)) {
            m_settings.insert(
                QStringLiteral("shape.hardness"), std::clamp(value / 100.0, 0.0, 1.0));
        }

        if (descriptorFlag(tip, QStringLiteral("flipX"))
            || descriptorFlag(tip, QStringLiteral("flipY"))) {
            drop(QObject::tr("Flip X/Y"), QObject::tr("no per-tip mirror setting"));
        }
    }

    void convertShapeDynamics()
    {
        if (!descriptorFlag(m_preset, QStringLiteral("useTipDynamics"))) {
            return;
        }

        const double minimumDiameter
            = descriptorNumberOr(m_preset, QStringLiteral("minimumDiameter"), 0.0) / 100.0;
        const double minimumRoundness
            = descriptorNumberOr(m_preset, QStringLiteral("minimumRoundness"), 0.0) / 100.0;

        convertScaledBlock(readDynamicsBlock(m_preset, QStringLiteral("szVr")),
            QStringLiteral("radius.multiplier"), minimumDiameter, QObject::tr("Size"));
        convertScaledBlock(readDynamicsBlock(m_preset, QStringLiteral("roundnessDynamics")),
            QStringLiteral("shape.roundness"), minimumRoundness, QObject::tr("Roundness"));

        const AbrDynamicsBlock angle = readDynamicsBlock(m_preset, QStringLiteral("angleDynamics"));
        if (angle.jitter > 0.0) {
            // Full jitter is a whole turn of spread centred on the tip's own
            // angle, so the added range is symmetric.
            const double half = angle.jitter * 180.0;
            bind(QStringLiteral("shape.angle"), QStringLiteral("randomValue"),
                makeBinding(QStringLiteral("add"), -half, half));
        }

        QString reason;
        const QString source = inputSourceFor(angle, QStringLiteral("shape.angle"), reason);
        if (!source.isEmpty()) {
            // Add rather than override, even though override is the editor's
            // default for this pair: evaluateDynamicsSlotValue returns at the
            // first active override binding and would discard the jitter
            // binding sharing this slot. Add also matches Photoshop, where the
            // tip's angle offsets the direction, and shape.angle wraps instead
            // of clamping so the sum stays meaningful.
            bind(QStringLiteral("shape.angle"), source,
                makeBinding(QStringLiteral("add"), 0.0, 360.0));
        }
        if (!reason.isEmpty()) {
            drop(QObject::tr("Angle control (%1)").arg(controlDisplayName(angle.control)), reason);
        }

        if (descriptorFlag(m_preset, QStringLiteral("flipX"))
            || descriptorFlag(m_preset, QStringLiteral("flipY"))) {
            drop(QObject::tr("Flip X/Y Jitter"), QObject::tr("no mirror jitter setting"));
        }
        if (descriptorFlag(m_preset, QStringLiteral("brushProjection"))) {
            drop(QObject::tr("Brush Projection"), QObject::tr("no equivalent"));
        }
    }

    void convertTransfer()
    {
        if (!descriptorFlag(m_preset, QStringLiteral("usePaintDynamics"))) {
            return;
        }

        const AbrDynamicsBlock opacity = readDynamicsBlock(m_preset, QStringLiteral("opVr"));
        convertScaledBlock(
            opacity, QStringLiteral("opacity.multiplier"), opacity.minimum, QObject::tr("Opacity"));

        // Flow lives under 'prVr', not the 'flVr' an older reading of the
        // format assumed.
        const AbrDynamicsBlock flow = readDynamicsBlock(m_preset, QStringLiteral("prVr"));
        convertScaledBlock(flow, QStringLiteral("shape.flow"), flow.minimum, QObject::tr("Flow"));

        const std::pair<QString, QString> mixerOnly[] = {
            { QStringLiteral("wtVr"), QObject::tr("Wetness Jitter") },
            { QStringLiteral("mxVr"), QObject::tr("Mix Jitter") },
        };
        for (const auto& entry : mixerOnly) {
            const AbrDynamicsBlock block = readDynamicsBlock(m_preset, entry.first);
            if (block.jitter > 0.0 || block.control != 0) {
                drop(entry.second, QObject::tr("Mixer Brush only; Ruwa's mixing has no jitter"));
            }
        }
    }

    void convertScattering()
    {
        if (!descriptorFlag(m_preset, QStringLiteral("useScatter"))) {
            return;
        }

        const AbrDynamicsBlock scatter
            = readDynamicsBlock(m_preset, QStringLiteral("scatterDynamics"));
        if (scatter.jitter > 0.0) {
            if (scatter.jitter > 1.0) {
                drop(QObject::tr("Scatter %1%").arg(scatter.jitter * 100.0, 0, 'f', 0),
                    QObject::tr("Ruwa's position scatter stops at 100%"));
            }
            m_settings.insert(
                QStringLiteral("scatter.position"), std::clamp(scatter.jitter, 0.0, 1.0));
        }

        QString reason;
        const QString source = inputSourceFor(scatter, QStringLiteral("scatter.position"), reason);
        if (!source.isEmpty()) {
            bind(QStringLiteral("scatter.position"), source,
                makeBinding(
                    QStringLiteral("multiply"), std::clamp(scatter.minimum, 0.0, 1.0), 1.0));
        }
        if (!reason.isEmpty()) {
            drop(QObject::tr("Scatter control (%1)").arg(controlDisplayName(scatter.control)),
                reason);
        }

        const double count = descriptorNumberOr(m_preset, QStringLiteral("Cnt "), 1.0);
        if (count > 1.0) {
            drop(QObject::tr("Count %1").arg(count, 0, 'f', 0),
                QObject::tr("Ruwa stamps one dab per step"));
        }
        if (readDynamicsBlock(m_preset, QStringLiteral("countDynamics")).jitter > 0.0) {
            drop(QObject::tr("Count Jitter"), QObject::tr("no dab count to vary"));
        }
    }

    void convertTexture()
    {
        if (!descriptorFlag(m_preset, QStringLiteral("useTexture"))) {
            return;
        }
        const QString pattern = descriptorText(
            descriptorMap(m_preset, QStringLiteral("Txtr")), QStringLiteral("Nm  "));
        drop(pattern.isEmpty() ? QObject::tr("Texture") : QObject::tr("Texture (%1)").arg(pattern),
            QObject::tr("the pattern lives in the file's pattern section and Ruwa's texture is a "
                        "different system"));
    }

    void convertColorDynamics()
    {
        if (!descriptorFlag(m_preset, QStringLiteral("useColorDynamics"))) {
            return;
        }

        const double hue = descriptorNumberOr(m_preset, QStringLiteral("H   "), 0.0);
        if (hue > 0.0) {
            const double half = std::clamp(hue / 100.0, 0.0, 1.0) * 180.0;
            bind(QStringLiteral("color.hue"), QStringLiteral("randomValue"),
                makeBinding(QStringLiteral("add"), -half, half));
        }

        const std::pair<QString, QString> scaled[] = {
            { QStringLiteral("Strt"), QStringLiteral("color.saturation") },
            { QStringLiteral("Brgh"), QStringLiteral("color.lightness") },
        };
        for (const auto& entry : scaled) {
            const double amount = descriptorNumberOr(m_preset, entry.first, 0.0);
            if (amount > 0.0) {
                const double low = std::clamp(1.0 - amount / 100.0, 0.0, 1.0);
                bind(entry.second, QStringLiteral("randomValue"),
                    makeBinding(QStringLiteral("multiply"), low, 1.0));
            }
        }

        if (readDynamicsBlock(m_preset, QStringLiteral("clVr")).jitter > 0.0) {
            drop(QObject::tr("Foreground/Background Jitter"),
                QObject::tr("Ruwa's brush paints one colour"));
        }
    }

    void convertRemaining()
    {
        if (descriptorFlag(descriptorMap(m_preset, QStringLiteral("dualBrush")),
                QStringLiteral("useDualBrush"))) {
            drop(QObject::tr("Dual Brush"), QObject::tr("Ruwa stamps a single tip"));
        }
        if (descriptorFlag(m_preset, QStringLiteral("useBrushPose"))) {
            drop(QObject::tr("Brush Pose"), QObject::tr("no fixed pose override"));
        }
    }

    QVariantMap m_preset;
    QVariantMap m_settings;
    QVariantMap m_bindings;
    QStringList m_unsupported;
    QString m_tipId;
};

QVector<QVariantMap> parsePresets(const QByteArray& body)
{
    QVector<QVariantMap> presets;

    // The section body is a four-byte descriptor version followed by an
    // ordinary descriptor whose single 'Brsh' key lists every preset.
    AbrReader reader(body);
    quint32 descriptorVersion = 0;
    if (!reader.readU32(descriptorVersion)) { // 16 in every file seen; only its size matters
        return presets;
    }

    QVariantMap root;
    if (!parseDescriptorBody(reader, root, 0)) {
        return presets;
    }

    const QVariant brushes = descriptorValue(root, QStringLiteral("Brsh"));
    if (brushes.userType() != QMetaType::QVariantList) {
        return presets;
    }
    for (const QVariant& item : brushes.toList()) {
        if (item.userType() == QMetaType::QVariantMap) {
            presets.append(item.toMap());
        }
    }
    return presets;
}

// ---------------------------------------------------------------------------
// output
// ---------------------------------------------------------------------------

QString safeFileName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("brush") : name.left(80);
}

bool saveTipPng(const AbrTipBitmap& tip, const QString& path)
{
    QImage image(tip.width, tip.height, QImage::Format_ARGB32);
    if (image.isNull()) {
        return false;
    }

    for (int y = 0; y < tip.height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        const int rowBase = y * tip.width;
        for (int x = 0; x < tip.width; ++x) {
            const int alpha = static_cast<uchar>(tip.alpha.at(rowBase + x));
            row[x] = qRgba(255, 255, 255, alpha);
        }
    }

    return image.save(path, "PNG");
}

} // namespace

bool importAbrBrushTips(
    const QString& filePath, QVector<AbrImportedTip>& tips, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QObject::tr("Cannot open ABR file: %1").arg(file.errorString()));
        return false;
    }

    AbrReader reader(file.readAll());
    quint16 version = 0;
    quint16 subversion = 0;
    if (!reader.readU16(version) || !reader.readU16(subversion)) {
        setError(errorMessage, QObject::tr("Not a valid ABR file."));
        return false;
    }
    if (version < 6 || version > 10 || (subversion != 1 && subversion != 2)) {
        setError(errorMessage,
            QObject::tr("Unsupported ABR version: %1.%2.").arg(version).arg(subversion));
        return false;
    }

    QVector<AbrTipBitmap> bitmaps;
    QVector<QVariantMap> presets;
    while (reader.remaining() >= 12) {
        quint32 tag = 0;
        quint32 subtag = 0;
        quint32 bodyLength = 0;
        if (!reader.readU32(tag) || tag != kTag8Bim || !reader.readU32(subtag)
            || !reader.readU32(bodyLength)
            || bodyLength > static_cast<quint32>(reader.remaining())) {
            // Trailing bytes that are not another section end the walk; what
            // has been read so far is still usable.
            break;
        }

        QByteArray body;
        if (!reader.readBytes(bodyLength, body)) {
            setError(errorMessage, QObject::tr("Corrupted ABR section body."));
            return false;
        }

        if (subtag == kSubtagSamples) {
            bitmaps += parseSamples(body, subversion);
        } else if (subtag == kSubtagDescriptors) {
            presets += parsePresets(body);
        }

        // Sections are padded; rather than assume an alignment, step forward to
        // the next section signature.
        for (int i = 0; i < 4 && reader.remaining() >= 4; ++i) {
            AbrReader probe = reader;
            quint32 nextTag = 0;
            if (probe.readU32(nextTag) && nextTag == kTag8Bim) {
                break;
            }
            if (!reader.skip(1)) {
                break;
            }
        }
    }

    if (presets.isEmpty()) {
        setError(errorMessage, QObject::tr("ABR file does not contain any brush presets."));
        return false;
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        setError(errorMessage, QObject::tr("Cannot locate application data folder."));
        return false;
    }
    const QString importId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString targetDir
        = QDir(appDataPath).filePath(QStringLiteral("brush-assets/abr/%1").arg(importId));
    if (!QDir().mkpath(targetDir)) {
        setError(errorMessage, QObject::tr("Cannot create brush asset folder."));
        return false;
    }

    QVector<AbrImportedTip> importedTips;
    importedTips.reserve(presets.size());
    for (int i = 0; i < presets.size(); ++i) {
        AbrPresetConverter converter(presets[i]);
        converter.run();

        AbrImportedTip tip;
        tip.name = converter.name();
        if (tip.name.isEmpty()) {
            tip.name = QObject::tr("Brush %1").arg(i + 1);
        }
        tip.settings = converter.settings();
        tip.unsupported = converter.unsupported();

        if (!converter.tipId().isEmpty()) {
            const auto bitmap = std::find_if(
                bitmaps.cbegin(), bitmaps.cend(), [&converter](const AbrTipBitmap& candidate) {
                    return candidate.id == converter.tipId();
                });
            if (bitmap == bitmaps.cend()) {
                tip.unsupported.append(
                    QObject::tr("Tip image: the referenced bitmap is missing from the file"));
            } else {
                const QString fileName = QStringLiteral("%1_%2.png")
                                             .arg(i + 1, 3, 10, QLatin1Char('0'))
                                             .arg(safeFileName(tip.name));
                const QString imagePath = QDir(targetDir).absoluteFilePath(fileName);
                if (!saveTipPng(*bitmap, imagePath)) {
                    setError(errorMessage,
                        QObject::tr("Cannot save imported brush tip image: %1").arg(imagePath));
                    return false;
                }
                tip.imagePath = QFileInfo(imagePath).absoluteFilePath();

                // A sampled tip is rarely square, and the dab quad is built
                // from one radius, so without these the bitmap would be
                // stretched to fill a circular footprint. Both scales clamp to
                // [0, 1], so the longer side becomes the radius and the shorter
                // one is expressed as a fraction of it.
                const int maxExtent = std::max(bitmap->width, bitmap->height);
                if (maxExtent > 0) {
                    tip.settings.insert(QStringLiteral("dab.xScale"),
                        static_cast<double>(bitmap->width) / maxExtent);
                    tip.settings.insert(QStringLiteral("dab.yScale"),
                        static_cast<double>(bitmap->height) / maxExtent);
                }
            }
        }

        importedTips.append(std::move(tip));
    }

    tips = std::move(importedTips);
    return true;
}

} // namespace ruwa::core::brushes
