// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_BRUSHENGINETYPES_H
#define RUWA_CORE_BRUSHENGINE_BRUSHENGINETYPES_H

namespace aether {

struct BrushExecutionOptions {
    bool preferGpu = true;
};

struct BrushEngineCapabilities {
    bool gpuStamping = false;
    bool gpuFlatten = false;
    bool asyncReadback = false;
};

} // namespace aether

#endif // RUWA_CORE_BRUSHENGINE_BRUSHENGINETYPES_H
