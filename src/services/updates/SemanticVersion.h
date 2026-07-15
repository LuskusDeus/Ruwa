// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_UPDATES_SEMANTICVERSION_H
#define RUWA_SERVICES_UPDATES_SEMANTICVERSION_H

#include <QString>
#include <QStringList>

#include <optional>

namespace ruwa::services {

class SemanticVersion {
public:
    static std::optional<SemanticVersion> parse(QString text, QString* errorMessage = nullptr);

    int comparePrecedence(const SemanticVersion& other) const;

private:
    QStringList m_core;
    QStringList m_prerelease;
};

} // namespace ruwa::services

#endif // RUWA_SERVICES_UPDATES_SEMANTICVERSION_H
