// SPDX-License-Identifier: MPL-2.0

#include "platform/LinuxPlatform.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QWidget>

namespace ruwa::platform {

void LinuxPlatform::disableWindowAnimations(QWidget* window)
{
    Q_UNUSED(window);
    // No-op on Linux - compositor handles animations
}

void LinuxPlatform::enableWindowAnimations(QWidget* window)
{
    Q_UNUSED(window);
    // No-op on Linux
}

bool LinuxPlatform::copyImageToClipboard(const QImage& image)
{
    if (image.isNull()) {
        return false;
    }

    auto* mimeData = new QMimeData();
    QByteArray pngBytes;
    QBuffer pngBuffer(&pngBytes);
    if (pngBuffer.open(QIODevice::WriteOnly)) {
        image.save(&pngBuffer, "PNG");
    }
    if (!pngBytes.isEmpty()) {
        mimeData->setData(QStringLiteral("image/png"), pngBytes);
    } else {
        mimeData->setImageData(image);
    }
    QGuiApplication::clipboard()->setMimeData(mimeData);
    return true;
}

} // namespace ruwa::platform
