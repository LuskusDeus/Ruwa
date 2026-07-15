// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H
#define RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H

#include <QVariantMap>
#include <QString>
#include <QVector>

namespace ruwa::core::brushes {

struct AbrImportedTip {
    QString name;
    QString imagePath;
    QVariantMap settings;
};

bool importAbrBrushTips(
    const QString& filePath, QVector<AbrImportedTip>& tips, QString* errorMessage = nullptr);

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H
