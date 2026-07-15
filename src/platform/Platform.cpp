// SPDX-License-Identifier: MPL-2.0

#include "platform/Platform.h"

#if defined(Q_OS_WIN)
#include "platform/WindowsPlatform.h"
#elif defined(Q_OS_LINUX)
#include "platform/LinuxPlatform.h"
#else
#include "platform/LinuxPlatform.h" // fallback
#endif

namespace ruwa::platform {

Platform* Platform::create()
{
#if defined(Q_OS_WIN)
    return new WindowsPlatform();
#else
    return new LinuxPlatform();
#endif
}

} // namespace ruwa::platform
