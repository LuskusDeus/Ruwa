// SPDX-License-Identifier: MPL-2.0

#include "features/brush/manager/AbrBrushImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace ruwa::core::brushes {

namespace {

constexpr quint32 kTag8Bim = 0x3842494d;
constexpr quint32 kSubtagSamples = 0x73616d70;
constexpr quint32 kSubtagDescriptors = 0x64657363;
constexpr int kMaxTipExtent = 16384;
constexpr int kMaxDescriptorItems = 10000;

struct AbrTipBitmap {
    QString brushId;
    QString name;
    QVariantMap settings;
    int width = 0;
    int height = 0;
    int depth = 0;
    QByteArray alpha;
};

struct AbrDescriptorBrush {
    QString brushId;
    QString name;
    QVariantMap settings;
};

class AbrReader {
public:
    explicit AbrReader(QByteArray data)
        : m_data(std::move(data))
    {
    }

    qsizetype position() const { return m_pos; }
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
        if (remaining() < 1)
            return false;
        out = static_cast<quint8>(m_data.at(m_pos++));
        return true;
    }

    bool readU16(quint16& out)
    {
        if (remaining() < 2)
            return false;
        const auto* p = reinterpret_cast<const uchar*>(m_data.constData() + m_pos);
        out = static_cast<quint16>((p[0] << 8) | p[1]);
        m_pos += 2;
        return true;
    }

    bool readU32(quint32& out)
    {
        if (remaining() < 4)
            return false;
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

QString cleanDescriptorString(QString value)
{
    while (!value.isEmpty() && value.back() == QChar(u'\0')) {
        value.chop(1);
    }
    return value;
}

QString readCompactString(AbrReader& reader)
{
    quint32 length = 0;
    if (!reader.readU32(length) || length > 4096) {
        return {};
    }
    if (length == 0) {
        length = 4;
    }
    QByteArray bytes;
    if (!reader.readBytes(length, bytes)) {
        return {};
    }
    return cleanDescriptorString(QString::fromLatin1(bytes));
}

QString readUnicodeString(AbrReader& reader)
{
    quint32 charCount = 0;
    if (!reader.readU32(charCount) || charCount > 100000) {
        return {};
    }
    QByteArray bytes;
    if (!reader.readBytes(static_cast<qsizetype>(charCount) * 2, bytes)) {
        return {};
    }
    QVector<char16_t> chars;
    chars.reserve(static_cast<int>(charCount));
    for (quint32 i = 0; i < charCount; ++i) {
        const auto hi = static_cast<uchar>(bytes.at(static_cast<int>(i * 2)));
        const auto lo = static_cast<uchar>(bytes.at(static_cast<int>(i * 2 + 1)));
        chars.append(static_cast<char16_t>((hi << 8) | lo));
    }
    return cleanDescriptorString(QString::fromUtf16(chars.constData(), chars.size()));
}

QVariant parseTypedValue(AbrReader& reader);

QVariantMap parseDescriptor(AbrReader& reader)
{
    QVariantMap out;
    readUnicodeString(reader);
    readCompactString(reader);

    quint32 itemCount = 0;
    if (!reader.readU32(itemCount) || itemCount > kMaxDescriptorItems) {
        return out;
    }

    for (quint32 i = 0; i < itemCount; ++i) {
        const QString key = readCompactString(reader);
        if (key.isEmpty()) {
            break;
        }
        out.insert(key, parseTypedValue(reader));
    }
    return out;
}

QVariantList parseDescriptorList(AbrReader& reader)
{
    QVariantList out;
    quint32 itemCount = 0;
    if (!reader.readU32(itemCount) || itemCount > 100000) {
        return out;
    }
    out.reserve(static_cast<int>(itemCount));
    for (quint32 i = 0; i < itemCount; ++i) {
        out.append(parseTypedValue(reader));
    }
    return out;
}

QVariant parseTypedValue(AbrReader& reader)
{
    quint32 type = 0;
    if (!reader.readU32(type)) {
        return {};
    }

    switch (type) {
    case 0x4f626a63: // Objc
        return parseDescriptor(reader);
    case 0x566c4c73: // VlLs
        return parseDescriptorList(reader);
    case 0x54455854: // TEXT
        return readUnicodeString(reader);
    case 0x556e7446: { // UntF
        quint32 unit = 0;
        double value = 0.0;
        if (!reader.readU32(unit) || !reader.readDouble(value)) {
            return {};
        }
        return value;
    }
    case 0x626f6f6c: { // bool
        quint8 value = 0;
        return reader.readU8(value) ? QVariant(value != 0) : QVariant();
    }
    case 0x6c6f6e67: { // long
        qint32 value = 0;
        return reader.readI32(value) ? QVariant(static_cast<int>(value)) : QVariant();
    }
    case 0x646f7562: { // doub
        double value = 0.0;
        return reader.readDouble(value) ? QVariant(value) : QVariant();
    }
    case 0x656e756d: // enum
        readCompactString(reader);
        return readCompactString(reader);
    case 0x976c6973: { // alias
        quint32 length = 0;
        QByteArray value;
        if (!reader.readU32(length) || !reader.readBytes(length, value)) {
            return {};
        }
        return cleanDescriptorString(QString::fromLatin1(value));
    }
    case 0x74647461: { // tdta
        quint32 length = 0;
        QByteArray value;
        if (!reader.readU32(length) || length > static_cast<quint32>(reader.remaining())
            || !reader.readBytes(length, value)) {
            return {};
        }
        return value;
    }
    case 0x6f626a20: // obj
    case 0x476c624f: // GlbO
    case 0x476c6243: // GlbC
        return parseDescriptor(reader);
    default:
        return {};
    }
}

QString variantString(const QVariant& value)
{
    if (value.userType() == QMetaType::QString) {
        return value.toString();
    }
    if (value.userType() == QMetaType::QByteArray) {
        return cleanDescriptorString(QString::fromLatin1(value.toByteArray()));
    }
    return {};
}

bool descriptorNumber(const QVariantMap& descriptor, const QString& key, double& out)
{
    QVariant value = descriptor.value(key);
    if (!value.isValid()) {
        return false;
    }
    if (value.userType() == QMetaType::QVariantMap) {
        value = value.toMap().value(QStringLiteral("value"));
    }

    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return false;
    }
    out = number;
    return true;
}

double unitPercent(double value)
{
    return std::clamp(value / 100.0, 0.0, 1.0);
}

void insertSetting(QVariantMap& settings, const QString& key, double value)
{
    if (std::isfinite(value)) {
        settings.insert(key, value);
    }
}

QVariantMap randomBinding(double amount)
{
    const double clampedAmount = std::max(0.0, amount);
    return {
        { QStringLiteral("mode"), QStringLiteral("add") },
        { QStringLiteral("enabled"), true },
        { QStringLiteral("points"),
            QVariantList {
                QVariantMap {
                    { QStringLiteral("x"), 0.0 },
                    { QStringLiteral("y"), -clampedAmount },
                    { QStringLiteral("smoothness"), 0.65 },
                },
                QVariantMap {
                    { QStringLiteral("x"), 1.0 },
                    { QStringLiteral("y"), clampedAmount },
                    { QStringLiteral("smoothness"), 0.65 },
                },
            } },
    };
}

void insertDynamicsBinding(QVariantMap& bindings, const QString& settingKey,
    const QString& sourceKey, const QVariantMap& binding)
{
    QVariantMap sourceBindings = bindings.value(settingKey).toMap();
    sourceBindings.insert(sourceKey, binding);
    bindings.insert(settingKey, sourceBindings);
}

void insertRandomBinding(QVariantMap& bindings, const QString& settingKey, double amount)
{
    if (amount <= 0.0 || !std::isfinite(amount)) {
        return;
    }
    insertDynamicsBinding(
        bindings, settingKey, QStringLiteral("randomValue"), randomBinding(amount));
}

QVariantList extractPressureCurve(const QVariantMap& descriptor, const QString& key)
{
    const QVariantMap variation = descriptor.value(key).toMap();
    const QVariantList points = variation.value(QStringLiteral("Crv ")).toList();
    QVariantList out;
    out.reserve(points.size());

    for (const QVariant& pointValue : points) {
        const QVariantMap pointMap = pointValue.toMap();
        double input = 0.0;
        double output = 0.0;
        if (!descriptorNumber(pointMap, QStringLiteral("Hrzn"), input)
            || !descriptorNumber(pointMap, QStringLiteral("Vrtc"), output)) {
            continue;
        }
        if (input > 1.0) {
            input /= 255.0;
        }
        if (output > 1.0) {
            output /= 255.0;
        }
        out.append(QVariantMap {
            { QStringLiteral("x"), std::clamp(input, 0.0, 1.0) },
            { QStringLiteral("y"), std::clamp(output, 0.0, 1.0) },
            { QStringLiteral("smoothness"), 0.65 },
        });
    }

    return out;
}

void insertPressureCurveBinding(
    QVariantMap& bindings, const QString& settingKey, const QVariantList& points)
{
    if (points.isEmpty()) {
        return;
    }
    insertDynamicsBinding(bindings, settingKey, QStringLiteral("tabletPressure"),
        QVariantMap {
            { QStringLiteral("mode"), QStringLiteral("multiply") },
            { QStringLiteral("enabled"), true },
            { QStringLiteral("points"), points },
        });
}

void collectDescriptorSettings(
    QVariantMap& settings, const QVariantMap& descriptor, bool allowHardness)
{
    double value = 0.0;
    if (descriptorNumber(descriptor, QStringLiteral("Spcn"), value)) {
        insertSetting(settings, QStringLiteral("shape.spacing"), value / 100.0);
    }
    if (allowHardness && descriptorNumber(descriptor, QStringLiteral("Hrdn"), value)) {
        insertSetting(settings, QStringLiteral("shape.hardness"), unitPercent(value));
    }
    if (descriptorNumber(descriptor, QStringLiteral("Angl"), value)) {
        insertSetting(settings, QStringLiteral("shape.angle"), value);
    }
    if (descriptorNumber(descriptor, QStringLiteral("Rndn"), value)) {
        insertSetting(settings, QStringLiteral("shape.roundness"), unitPercent(value));
    }

    const QVariantMap transfer = descriptor.value(QStringLiteral("Trns")).toMap();
    if (descriptorNumber(transfer, QStringLiteral("Flw "), value)
        || descriptorNumber(descriptor, QStringLiteral("Flw "), value)) {
        insertSetting(settings, QStringLiteral("shape.flow"), unitPercent(value));
    }

    const QVariantMap scatter = descriptor.value(QStringLiteral("Sctr")).toMap();
    if (descriptorNumber(scatter, QStringLiteral("Sctr"), value)
        || descriptorNumber(descriptor, QStringLiteral("Sctr"), value)) {
        insertSetting(settings, QStringLiteral("scatter.position"), unitPercent(value));
    }

    QVariantMap bindings = settings.value(QStringLiteral("dynamics.bindings")).toMap();
    const QVariantMap shapeDynamics = descriptor.value(QStringLiteral("ShpD")).toMap();
    if (descriptorNumber(shapeDynamics, QStringLiteral("SzJt"), value)) {
        insertRandomBinding(bindings, QStringLiteral("radius.multiplier"), unitPercent(value));
    }
    if (descriptorNumber(shapeDynamics, QStringLiteral("AnJt"), value)) {
        insertRandomBinding(
            bindings, QStringLiteral("shape.angle"), value <= 100.0 ? value * 3.6 : value);
    }
    if (descriptorNumber(shapeDynamics, QStringLiteral("RnJt"), value)) {
        insertRandomBinding(bindings, QStringLiteral("shape.roundness"), unitPercent(value));
    }

    insertPressureCurveBinding(bindings, QStringLiteral("radius.multiplier"),
        extractPressureCurve(shapeDynamics, QStringLiteral("szVr")));
    insertPressureCurveBinding(bindings, QStringLiteral("opacity.multiplier"),
        extractPressureCurve(transfer, QStringLiteral("opVr")));
    insertPressureCurveBinding(bindings, QStringLiteral("shape.flow"),
        extractPressureCurve(transfer, QStringLiteral("flVr")));

    if (!bindings.isEmpty()) {
        settings.insert(QStringLiteral("dynamics.bindings"), bindings);
    }
}

QVariantMap descriptorSettings(const QVariantMap& descriptor)
{
    QVariantMap settings;
    const QVariantMap sampledBrush = descriptor.value(QStringLiteral("Brsh")).toMap();
    const bool hasCustomDab
        = !variantString(sampledBrush.value(QStringLiteral("sampledData"))).isEmpty();
    if (!sampledBrush.isEmpty()) {
        collectDescriptorSettings(settings, sampledBrush, !hasCustomDab);
    }
    collectDescriptorSettings(settings, descriptor, !hasCustomDab);
    if (hasCustomDab) {
        settings.insert(QStringLiteral("shape.hardness"), 1.0);
    }
    return settings;
}

QVector<AbrDescriptorBrush> parseDescriptorBrushes(const QByteArray& body)
{
    QVector<AbrDescriptorBrush> brushes;
    AbrReader reader(body);
    if (!reader.skip(18)) {
        return brushes;
    }

    QVariantMap root;
    quint32 count = 0;
    if (!reader.readU32(count) || count > kMaxDescriptorItems) {
        return brushes;
    }
    for (quint32 i = 0; i < count; ++i) {
        const QString key = readCompactString(reader);
        if (key.isEmpty()) {
            break;
        }
        root.insert(key, parseTypedValue(reader));
    }

    const QVariantList brushItems = root.value(QStringLiteral("Brsh")).toList();
    for (const QVariant& item : brushItems) {
        const QVariantMap brush = item.toMap();
        AbrDescriptorBrush descriptorBrush;
        descriptorBrush.name = variantString(brush.value(QStringLiteral("Nm  "))).trimmed();
        const QVariantMap sampled = brush.value(QStringLiteral("Brsh")).toMap();
        descriptorBrush.brushId = variantString(sampled.value(QStringLiteral("sampledData")));
        descriptorBrush.settings = descriptorSettings(brush);
        if (!descriptorBrush.brushId.isEmpty() || !descriptorBrush.name.isEmpty()
            || !descriptorBrush.settings.isEmpty()) {
            brushes.append(std::move(descriptorBrush));
        }
    }

    if (brushes.isEmpty()) {
        AbrDescriptorBrush descriptorBrush;
        descriptorBrush.name = variantString(root.value(QStringLiteral("Nm  "))).trimmed();
        descriptorBrush.brushId = variantString(
            root.value(QStringLiteral("Brsh")).toMap().value(QStringLiteral("sampledData")));
        descriptorBrush.settings = descriptorSettings(root);
        if (!descriptorBrush.brushId.isEmpty() || !descriptorBrush.name.isEmpty()
            || !descriptorBrush.settings.isEmpty()) {
            brushes.append(std::move(descriptorBrush));
        }
    }

    return brushes;
}

bool decodePackBitsRow(const QByteArray& input, int rowBytes, QByteArray& out)
{
    int offset = 0;
    while (offset < input.size() && out.size() < rowBytes) {
        qint8 header = static_cast<qint8>(input.at(offset++));
        if (header >= 0) {
            const int count = header + 1;
            if (offset + count > input.size())
                return false;
            const int appendCount = std::min(count, rowBytes - static_cast<int>(out.size()));
            out.append(input.constData() + offset, appendCount);
            offset += count;
        } else if (header > -128) {
            const int count = 1 - header;
            if (offset >= input.size())
                return false;
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
        if (bitmap.size() < expectedBytes)
            return false;
        decoded = bitmap.left(expectedBytes);
    } else if (compression == 1) {
        AbrReader reader(bitmap);
        QVector<quint16> rowSizes;
        rowSizes.reserve(height);
        for (int y = 0; y < height; ++y) {
            quint16 rowSize = 0;
            if (!reader.readU16(rowSize))
                return false;
            rowSizes.append(rowSize);
        }
        decoded.reserve(expectedBytes);
        for (int y = 0; y < height; ++y) {
            QByteArray packedRow;
            if (!reader.readBytes(rowSizes[y], packedRow))
                return false;
            QByteArray row;
            row.reserve(rowBytes);
            if (!decodePackBitsRow(packedRow, rowBytes, row))
                return false;
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
    if (right <= left || bottom <= top || depth != 8 && depth != 16 || compression > 1) {
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
    tip.depth = depth;
    tip.alpha = std::move(alpha);
    return true;
}

bool parseSample(const QByteArray& data, int subversion, AbrTipBitmap& tip)
{
    AbrReader reader(data);
    quint8 idLength = 0;
    QByteArray idBytes;
    if (!reader.readU8(idLength) || !reader.readBytes(idLength, idBytes)) {
        return false;
    }
    tip.brushId = cleanDescriptorString(QString::fromLatin1(idBytes));

    if (subversion == 1) {
        if (!reader.skip(10)) {
            return false;
        }
        return parseImageData(reader, tip);
    }

    quint16 metaLength = 0;
    quint16 metaA = 0;
    quint32 version = 0;
    quint32 length = 0;
    QByteArray bounds;
    quint32 channelCount = 0;
    if (!reader.readU16(metaLength) || !reader.readU16(metaA) || !reader.readU32(version)
        || !reader.readU32(length) || !reader.readBytes(16, bounds)
        || !reader.readU32(channelCount)) {
        return false;
    }
    if (channelCount == 0 || channelCount > 64) {
        return false;
    }

    for (quint32 i = 0; i < channelCount; ++i) {
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
    int index = 0;

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
            ++index;
            tip.name = QObject::tr("Brush %1").arg(index);
            tips.append(std::move(tip));
        }

        const qsizetype padding = (4 - (sampleLength % 4)) % 4;
        if (padding > 0 && !reader.skip(padding)) {
            break;
        }
    }

    return tips;
}

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

bool saveImportedTips(
    const QVector<AbrTipBitmap>& bitmaps, QVector<AbrImportedTip>& tips, QString* errorMessage)
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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

    QVector<AbrImportedTip> savedTips;
    savedTips.reserve(bitmaps.size());
    for (int i = 0; i < bitmaps.size(); ++i) {
        const AbrTipBitmap& bitmap = bitmaps[i];
        const QString displayName = bitmap.name.trimmed().isEmpty()
            ? QObject::tr("Brush %1").arg(i + 1)
            : bitmap.name.trimmed();
        const QString fileName = QStringLiteral("%1_%2.png")
                                     .arg(i + 1, 3, 10, QLatin1Char('0'))
                                     .arg(safeFileName(displayName));
        const QString imagePath = QDir(targetDir).absoluteFilePath(fileName);
        if (!saveTipPng(bitmap, imagePath)) {
            setError(errorMessage,
                QObject::tr("Cannot save imported brush tip image: %1").arg(imagePath));
            return false;
        }

        AbrImportedTip tip;
        tip.name = displayName;
        tip.imagePath = QFileInfo(imagePath).absoluteFilePath();
        tip.settings = bitmap.settings;
        savedTips.append(std::move(tip));
    }

    tips = std::move(savedTips);
    return true;
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
    QVector<AbrDescriptorBrush> descriptorBrushes;
    while (reader.remaining() >= 12) {
        quint32 tag = 0;
        quint32 subtag = 0;
        quint32 bodyLength = 0;
        if (!reader.readU32(tag) || !reader.readU32(subtag) || !reader.readU32(bodyLength)
            || bodyLength > static_cast<quint32>(reader.remaining())) {
            setError(errorMessage, QObject::tr("Corrupted ABR section header."));
            return false;
        }

        QByteArray body;
        if (!reader.readBytes(bodyLength, body)) {
            setError(errorMessage, QObject::tr("Corrupted ABR section body."));
            return false;
        }

        if (tag == kTag8Bim && subtag == kSubtagSamples) {
            bitmaps += parseSamples(body, subversion);
        } else if (tag == kTag8Bim && subtag == kSubtagDescriptors) {
            descriptorBrushes += parseDescriptorBrushes(body);
        }

        const qsizetype padding = (4 - (bodyLength % 4)) % 4;
        if (padding > 0 && reader.remaining() >= padding && !reader.atEnd()) {
            reader.skip(padding);
        }
    }

    if (bitmaps.isEmpty()) {
        setError(
            errorMessage, QObject::tr("ABR file does not contain supported grayscale brush tips."));
        return false;
    }

    for (int i = 0; i < bitmaps.size(); ++i) {
        const AbrDescriptorBrush* descriptorBrush = nullptr;
        for (const AbrDescriptorBrush& candidate : descriptorBrushes) {
            if (!candidate.brushId.isEmpty() && candidate.brushId == bitmaps[i].brushId) {
                descriptorBrush = &candidate;
                break;
            }
        }
        if (!descriptorBrush && i < descriptorBrushes.size()) {
            descriptorBrush = &descriptorBrushes[i];
        }

        if (descriptorBrush) {
            const QString name = descriptorBrush->name.trimmed();
            if (!name.isEmpty()) {
                bitmaps[i].name = name;
            }
            bitmaps[i].settings = descriptorBrush->settings;
        }

        if (bitmaps[i].name.trimmed().isEmpty()) {
            bitmaps[i].name = QObject::tr("Brush %1").arg(i + 1);
        }
    }

    if (!saveImportedTips(bitmaps, tips, errorMessage)) {
        return false;
    }
    return true;
}

} // namespace ruwa::core::brushes
