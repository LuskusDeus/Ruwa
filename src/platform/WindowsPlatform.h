// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_PLATFORM_WINDOWSPLATFORM_H
#define RUWA_PLATFORM_WINDOWSPLATFORM_H

#include "platform/Platform.h"

namespace ruwa::platform {

class WindowsPlatform : public Platform {
public:
    void disableWindowAnimations(QWidget* window) override;
    void enableWindowAnimations(QWidget* window) override;
    bool copyImageToClipboard(const QImage& image) override;
};

} // namespace ruwa::platform

#endif // RUWA_PLATFORM_WINDOWSPLATFORM_H
