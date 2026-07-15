// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_PLATFORM_PLATFORM_H
#define RUWA_PLATFORM_PLATFORM_H

#include <QImage>
#include <QWidget>

namespace ruwa::platform {

class Platform {
public:
    virtual ~Platform() = default;

    static Platform* create();

    virtual void disableWindowAnimations(QWidget* window) = 0;
    virtual void enableWindowAnimations(QWidget* window) = 0;
    virtual bool copyImageToClipboard(const QImage& image) = 0;
};

} // namespace ruwa::platform

#endif // RUWA_PLATFORM_PLATFORM_H
