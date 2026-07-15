// SPDX-License-Identifier: MPL-2.0

#include "services/updates/SemanticVersion.h"

namespace ruwa::services {

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool isAsciiDigitString(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    for (const QChar ch : value) {
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}

bool isValidIdentifier(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    for (const QChar ch : value) {
        const bool asciiDigit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        const bool asciiLower = ch >= QLatin1Char('a') && ch <= QLatin1Char('z');
        const bool asciiUpper = ch >= QLatin1Char('A') && ch <= QLatin1Char('Z');
        if (!asciiDigit && !asciiLower && !asciiUpper && ch != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

int compareNumericIdentifier(const QString& left, const QString& right)
{
    if (left.size() != right.size()) {
        return left.size() < right.size() ? -1 : 1;
    }
    const int result = QString::compare(left, right, Qt::CaseSensitive);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::parse(QString text, QString* errorMessage)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        text.remove(0, 1);
    }
    if (text.isEmpty()) {
        setError(errorMessage, QStringLiteral("Version is empty"));
        return std::nullopt;
    }

    const qsizetype plusIndex = text.indexOf(QLatin1Char('+'));
    QString buildMetadata;
    if (plusIndex >= 0) {
        buildMetadata = text.mid(plusIndex + 1);
        text = text.left(plusIndex);
        if (buildMetadata.isEmpty() || buildMetadata.contains(QLatin1Char('+'))) {
            setError(errorMessage, QStringLiteral("Invalid build metadata"));
            return std::nullopt;
        }
        const QStringList identifiers = buildMetadata.split(QLatin1Char('.'), Qt::KeepEmptyParts);
        for (const QString& identifier : identifiers) {
            if (!isValidIdentifier(identifier)) {
                setError(errorMessage, QStringLiteral("Invalid build metadata identifier"));
                return std::nullopt;
            }
        }
    }

    QString prereleaseText;
    const qsizetype dashIndex = text.indexOf(QLatin1Char('-'));
    if (dashIndex >= 0) {
        prereleaseText = text.mid(dashIndex + 1);
        text = text.left(dashIndex);
        if (prereleaseText.isEmpty()) {
            setError(errorMessage, QStringLiteral("Prerelease identifier is empty"));
            return std::nullopt;
        }
    }

    const QStringList core = text.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    if (core.size() != 3) {
        setError(errorMessage, QStringLiteral("Version must contain major, minor and patch"));
        return std::nullopt;
    }
    for (const QString& identifier : core) {
        if (!isAsciiDigitString(identifier)
            || (identifier.size() > 1 && identifier.startsWith(QLatin1Char('0')))) {
            setError(errorMessage, QStringLiteral("Invalid numeric version identifier"));
            return std::nullopt;
        }
    }

    QStringList prerelease;
    if (!prereleaseText.isEmpty()) {
        prerelease = prereleaseText.split(QLatin1Char('.'), Qt::KeepEmptyParts);
        for (const QString& identifier : prerelease) {
            if (!isValidIdentifier(identifier)) {
                setError(errorMessage, QStringLiteral("Invalid prerelease identifier"));
                return std::nullopt;
            }
            if (isAsciiDigitString(identifier) && identifier.size() > 1
                && identifier.startsWith(QLatin1Char('0'))) {
                setError(errorMessage, QStringLiteral("Numeric prerelease identifiers cannot have leading zeros"));
                return std::nullopt;
            }
        }
    }

    SemanticVersion version;
    version.m_core = core;
    version.m_prerelease = prerelease;
    return version;
}

int SemanticVersion::comparePrecedence(const SemanticVersion& other) const
{
    for (qsizetype i = 0; i < m_core.size(); ++i) {
        const int comparison = compareNumericIdentifier(m_core.at(i), other.m_core.at(i));
        if (comparison != 0) {
            return comparison;
        }
    }

    if (m_prerelease.isEmpty() || other.m_prerelease.isEmpty()) {
        if (m_prerelease.isEmpty() == other.m_prerelease.isEmpty()) {
            return 0;
        }
        return m_prerelease.isEmpty() ? 1 : -1;
    }

    const qsizetype sharedCount = qMin(m_prerelease.size(), other.m_prerelease.size());
    for (qsizetype i = 0; i < sharedCount; ++i) {
        const QString& left = m_prerelease.at(i);
        const QString& right = other.m_prerelease.at(i);
        const bool leftNumeric = isAsciiDigitString(left);
        const bool rightNumeric = isAsciiDigitString(right);
        if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        }
        const int comparison = leftNumeric ? compareNumericIdentifier(left, right)
                                           : QString::compare(left, right, Qt::CaseSensitive);
        if (comparison != 0) {
            return comparison < 0 ? -1 : 1;
        }
    }

    if (m_prerelease.size() == other.m_prerelease.size()) {
        return 0;
    }
    return m_prerelease.size() < other.m_prerelease.size() ? -1 : 1;
}

} // namespace ruwa::services
