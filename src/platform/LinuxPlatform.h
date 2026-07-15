// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_PLATFORM_LINUXPLATFORM_H
#define RUWA_PLATFORM_LINUXPLATFORM_H

#include "platform/Platform.h"

namespace ruwa::platform {

class LinuxPlatform : public Platform {
public:
    void disableWindowAnimations(QWidget* window) override;
    void enableWindowAnimations(QWidget* window) override;
    bool copyImageToClipboard(const QImage& image) override;
};

} // namespace ruwa::platform

#endif // RUWA_PLATFORM_LINUXPLATFORM_H
